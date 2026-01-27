#include <zephyr/logging/log.h>

#include "modem.hpp"
#include "app.hpp"
#include "certificate.h"
#include "modem/lte_lc.h"
#include "modem/nrf_modem_lib.h"
#include "nrf_modem.h"
#include "nrf_modem_at.h"

LOG_MODULE_REGISTER(modem, LOG_LEVEL_INF);

namespace alc
{
  Modem* Modem::s_instance { nullptr };

  void Modem::lteHandler(const lte_lc_evt* const event)
  {
    const auto type { event->type };
    const auto regStatus { event->nw_reg_status };
    const auto rrcMode { event->rrc_mode };

    switch (type) 
    {
      // Network Registration Status.
      case LTE_LC_EVT_NW_REG_STATUS:
        if ((regStatus == LTE_LC_NW_REG_REGISTERED_HOME) || (regStatus == LTE_LC_NW_REG_REGISTERED_ROAMING)) 
        { 
		      LOG_INF("1-Connection - %s", (regStatus == LTE_LC_NW_REG_REGISTERED_HOME ? "Home Network" : "Roaming"));

          s_instance->m_connected = true;
          k_event_post(&s_instance->m_events, M_EVENT_CONNECTED);
          k_event_clear(&s_instance->m_events, M_EVENT_DISCONNECTED);

     	    k_sem_give(&s_instance->m_lte_connected_sem);
        }
        break;

      // RRC Status.
	    case LTE_LC_EVT_RRC_UPDATE:
      { 
        bool idle { (rrcMode == LTE_LC_RRC_MODE_IDLE) };
        
        if (idle) {
          LOG_INF("2-RRC mode: Idle.");
          k_event_post(&s_instance->m_events, M_EVENT_RRC_IDLE);
          k_event_clear(&s_instance->m_events, M_EVENT_RRC_CONNECTED);
          break;
        } 
        LOG_INF("2-RRC mode: Connected.");
        k_event_post(&s_instance->m_events, M_EVENT_RRC_CONNECTED);
        k_event_clear(&s_instance->m_events, M_EVENT_RRC_IDLE);
		    break;
      }
      // Modem event.
      case LTE_LC_EVT_CELL_UPDATE:
        LOG_INF("LTE CELL Update Occurred.");
        k_event_post(&s_instance->m_events, M_EVENT_CELL_UPDATE);
        break;

      // Modem event.
      case LTE_LC_EVT_LTE_MODE_UPDATE:
        LOG_INF("LTE Mode Update Occurred.");
        k_event_post(&s_instance->m_events, M_EVENT_MODE_CHANGE);
        break;

      // Modem event.
      case LTE_LC_EVT_MODEM_EVENT:
        LOG_INF("Modem Event Occurred.");
        k_event_post(&s_instance->m_events, M_EVENT_MODEM_EVENT);
        break;
      // Default
      default:
        LOG_WRN("Unhandled LTE event:  %d!", static_cast<int>(event->type));
        break;
    }
  }
  
  Modem::Modem(App& app):
    m_app(app)
  {
    s_instance = this;

    m_connected = false;

    constexpr int count { 0 };
    constexpr int limit { 1 }; 
    k_sem_init(&m_lte_connected_sem, count, limit); 

    k_event_init(&m_events);
  }

  bool Modem::Init()
  {
    int result { nrf_modem_lib_init() };
    if (result) {
		  LOG_ERR("Failed to initialise the modem library, error: %d.", result);
		  return false;
	  }
    
    // Get IMEI during initialisation.
    if (!getImei()) {
        LOG_ERR("Failed to get IMEI!");
        return false;
    }
    
    // LOG_INF("Modem initialized with IMEI: %s", m_imei);

    Disconnect();

    return true;
  }
	
  bool Modem::Connect()
  {
	  // LOG_INF("C-Connecting to LTE network.");
   
    if (!nrf_modem_is_initialized()) { Init(); }

    // If init fails, restart modem. 
    if (!nrf_modem_is_initialized()) { Restart(); }

    int result = lte_lc_connect();
	  if (result) {
		  LOG_ERR("C-Error in lte_lc_connect, error: %d!", result);
	    m_connected = false;
      return m_connected;
	  }

	  LOG_INF("C-Connected to LTE network.");
    // logModemState();
    
    m_connected = true;
  	return m_connected;
  }

  bool Modem::ConnectAsync()
  {
	  // LOG_INF("CA-Connecting to LTE network.");

    if (!nrf_modem_is_initialized()) { Init(); }

    // If init fails, restart modem. 
    if (!nrf_modem_is_initialized()) { Restart(); }

	  int result = lte_lc_connect_async(lteHandler);
	  if (result) {
		  LOG_ERR("CA-Error in lte_lc_connect_async, error: %d!", result);
      m_connected = false;
      k_event_post(&s_instance->m_events, M_EVENT_CONNECTED);
      k_event_clear(&s_instance->m_events, M_EVENT_DISCONNECTED);

		  return m_connected;;
	  }

    // Wait with timeout for network attach.
    if (k_sem_take(&m_lte_connected_sem, K_SECONDS(M_CONNECT_TIMEOUT)) != 0) {
      LOG_WRN("CA-Timeout waiting for LTE connection after %d seconds.", M_CONNECT_TIMEOUT);

      // Clean shutdown sequence after timeout.
      // lte_lc_offline() may fail here as async connect was interrupted.
      int offlineResult = lte_lc_offline();
      if (offlineResult) {
        LOG_DBG("CA-lte_lc_offline returned %d (expected after timeout).", offlineResult);
      }

      // Shutdown modem library completely.
      nrf_modem_lib_shutdown();
      m_connected = false;
      return m_connected;
    }

    m_connected = true;
	  LOG_INF("CA-Connected to LTE network.");
  	return m_connected;
  }

  bool Modem::Restart()
  {
    int result { nrf_modem_shutdown() };
    if (result != 0) {
      LOG_ERR("R-Unable to shutdown modem!");
      return false;
    }

    result = nrf_modem_lib_init();
    if (result != 0) {
      LOG_ERR("R-Unable to initialise modem!");
      return false;
    }
  
    return true;
  }
 
  bool Modem::Disconnect()
  {
    // Check if modem is even initialized before trying to disconnect.
    if (!nrf_modem_is_initialized()) {
      LOG_INF("D-Modem not initialized, skipping disconnect.");
      m_connected = false;
      return true;
    }

    // Try to go offline - may fail if modem is in unexpected state.
    int result { lte_lc_offline() };
    if (result) {
      // Log as warning, not error - this can happen after timeout recovery.
      LOG_WRN("D-lte_lc_offline returned %d (may be expected after timeout).", result);
      // Continue to power_off anyway.
    }

    result = lte_lc_power_off();
    if (result) {
      LOG_WRN("D-lte_lc_power_off returned %d.", result);
      // Not fatal - modem may already be powered off.
    }

    k_sleep(K_SECONDS(1));

    LOG_INF("D-Disconnected from LTE network.");
    m_connected = false;
    return true;
  }
  
  bool Modem::SetOffline()
  {
    constexpr bool reset { false };
    constexpr k_timeout_t timeout { K_SECONDS(10) };
    constexpr uint32_t eventMask { (Modem::M_EVENT_DISCONNECTED | Modem::M_EVENT_RRC_IDLE) };

    int result { lte_lc_offline() };
    if (result != 0)
    {
      LOG_ERR("Unable to set modem offline! Error: %d.", result);
      return false;
    }

    if (0 == k_event_wait(GetEvents(), eventMask, reset, timeout)) {
      LOG_ERR("Modem offline check timed out!");
      return false;
    }

    return true;
  }

  bool Modem::SetOnline()
  {
    constexpr bool reset { false };
    constexpr k_timeout_t timeout { K_SECONDS(10) };
    constexpr uint32_t eventMask { (Modem::M_EVENT_CONNECTED | Modem::M_EVENT_RRC_CONNECTED) };

    int result { lte_lc_normal() };
    if (result != 0)
    {
      LOG_ERR("Unable to set modem online! Error: %d.", result);
      return false;
    }

    if (0 == k_event_wait(GetEvents(), eventMask, reset, timeout)) {
      LOG_ERR("Modem online check timed out!");
      return false;
    }

    return true;
  }

  bool Modem::SetGnss()
  {
    constexpr int RETURN_DELAY_SECS { 2 }; 

    int result { lte_lc_func_mode_set(LTE_LC_FUNC_MODE_ACTIVATE_GNSS) };
    if (result < 0) {
  	  LOG_ERR("Failed to activate GNSS functional mode: error=%d!", result);
      return false;
    }

    k_sleep(K_SECONDS(RETURN_DELAY_SECS));

    return true;
  }

  void Modem::GetNrfCloudDeviceId(char* buffer, size_t size) const
  {
    // Format: "nrf-" + IMEI = 4 + 15 + 1 = 20.
    snprintf(buffer, size, "nrf-%s", m_imei);
  }

  bool Modem::SendAtCommand(const char* command, 
                            const char* expectedResponse,
                            char* responseBuffer,
                            size_t responseBufferSize,
                            int timeoutMs)
  {
    // Use internal buffer if none provided.
    char internalBuffer[256];
    char* buffer = responseBuffer ? responseBuffer : internalBuffer;
    size_t bufferSize = responseBuffer ? responseBufferSize : sizeof(internalBuffer);
  
    LOG_INF("Sending AT command: %s", command);
  
    // Send AT command.
    int result = nrf_modem_at_cmd(buffer, bufferSize, "%s", command);
    if (result < 0) {
      LOG_ERR("AT command failed: %d!", result);
      return false;
    }
  
    LOG_INF("AT response: %s.", buffer);
  
    // If no expected response, just check for "OK".
    if (expectedResponse == nullptr) {
      return (strstr(buffer, "OK") != nullptr);
    }
  
    // Check if response contains expected substring.
    return (strstr(buffer, expectedResponse) != nullptr);
  }

  bool Modem::CheckAtResponse(const char* command, const char* expectedResponse)
  {
    // Short version of SendAtCommand.
    return SendAtCommand(command, expectedResponse);
  }

  bool Modem::ReadyForData(uint32_t timeoutSeconds)
  {
    // Uses AT+CGATT to detect if the modem is connected tothe packet network.

    // LOG_INF("Checking for packet data connectivity (+CGATT 1)...");
    
    int64_t start { k_uptime_get() };
    int64_t timeoutMs { (timeoutSeconds * M_SECS_TO_MS) };
    constexpr const char* HOME { ",1" };
    constexpr const char* ROAMING { ",5" };
    constexpr const char* CGATT_1 { "+CGATT: 1" };
    constexpr int DNS_DELAY_MS { 500 };
    constexpr int LOOP_DELAY_SECS { 1 };

    while (k_uptime_delta(&start) < timeoutMs) 
    {
      // Check CEREG - EPS registration status
      char ceregBuffer[64];
      if (SendAtCommand(M_AT_CEREG, nullptr, ceregBuffer, sizeof(ceregBuffer)))
      {
        // LOG_INF("CEREG response: %s", ceregBuffer);
        
        // Look for +CEREG: n,1 (registered home) or +CEREG: n,5 (registered roaming).
        if (strstr(ceregBuffer, HOME) || strstr(ceregBuffer, ROAMING)) 
        {
          LOG_INF("EPS registration confirmed,");
          
          // Now check packet domain attachment.
          char cgattBuffer[32];
          if (SendAtCommand(M_AT_CGATT, nullptr, cgattBuffer, sizeof(cgattBuffer))) 
          {
            // Look for +CGATT: 1 (attached)
            if (strstr(cgattBuffer, CGATT_1)) 
            {
              LOG_INF("Packet domain attached - data ready!");
              
              // Give DNS subsystem a moment to initialise.
              k_sleep(K_MSEC(DNS_DELAY_MS));
              return true;
            }
          }
        }
      }
      // Not ready yet, wait and retry.
      k_sleep(K_SECONDS(LOOP_DELAY_SECS));
    }
    
    LOG_ERR("Timeout waiting for data connectivity!");
    return false;
  }

  int Modem::GetCfun()
  {
    constexpr int error { -1 };
    char responseBuffer[64];
  
    int result { nrf_modem_at_cmd(responseBuffer, sizeof(responseBuffer), M_AT_CFUN) };
    if (result < 0) {
      LOG_ERR("AT+CFUN? failed: %d!", result);
      return error;
    }
  
    LOG_INF("CFUN response: %s.", responseBuffer);
  
    // Response format: "+CFUN: <mode>" or "+CFUN: <mode>,<rst>"
    // Example: "+CFUN: 1" or "+CFUN: 4,0"
    constexpr int codeIndex { 7 };  // Location of <mode>.
    const char* cfunStr = strstr(responseBuffer, "+CFUN: ") + codeIndex;
    if (!cfunStr) {
      LOG_ERR("Invalid CFUN response format!");
      return error;
    }
  
    return atoi(cfunStr);
  }

  bool Modem::getImei()
  {
    constexpr int RESPONSE_LEN { M_IMEI_LEN + 6 + 1 };
    char imeiBuffer[RESPONSE_LEN];
    
    int result { nrf_modem_at_cmd(imeiBuffer, sizeof(imeiBuffer), M_AT_CGSN) };
    if (result) {
      LOG_ERR("Failed to get IMEI: %d!", result);
      return false;
    }
    
    // 15 characters and null terminator.
    strncpy(m_imei, imeiBuffer, M_IMEI_LEN);
    m_imei[M_IMEI_LEN] = '\0';

    return true;
  }

  void Modem::modemConnected(const bool flag)
  {
    // Helper method to manage internal state.

    if (flag){
      m_connected = true;
      k_event_post(&s_instance->m_events, M_EVENT_CONNECTED);
      k_event_clear(&s_instance->m_events, M_EVENT_DISCONNECTED);
      return;
    }
    m_connected = false;
    k_event_post(&s_instance->m_events, M_EVENT_DISCONNECTED);
    k_event_clear(&s_instance->m_events, M_EVENT_CONNECTED);
  }
}
