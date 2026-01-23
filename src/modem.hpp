#pragma once

#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <modem/modem_key_mgmt.h>
#include <zephyr/kernel.h>

namespace alc
{
  class App; // Forward declaration.

  class Modem
  {
    public:
      enum class CfunMode : uint8_t {
        POWER_OFF = 0,       // Minimum functionality
        NORMAL = 1,          // Full functionality
        FLIGHT_MODE = 4,     // No RF
        RX_ONLY = 20,        // Receive only (rare)
        TX_ONLY = 21,        // Transmit only (rare)
        GNSS_ONLY = 31,      // GNSS receiver only
        UNKNOWN = 0xFF       // Parse error or unexpected value
      };

      enum Events : uint32_t {
        M_EVENT_CONNECTED = BIT(0),     // LTE connected (home or roaming).
        M_EVENT_DISCONNECTED = BIT(1),  // LTE disconnected/searching.
        M_EVENT_RRC_IDLE = BIT(2),      // Radio in idle mode (low power).
        M_EVENT_RRC_CONNECTED = BIT(3), // Radio actively transmitting.
        M_EVENT_PSM_ENTERED = BIT(4),   // Entered PSM (if used).
        M_EVENT_OFFLINE = BIT(5),       // Modem went offline (CFUN=4).
        M_EVENT_ONLINE = BIT(6),        // Modem came back online (CFUN=1).
        M_EVENT_MODE_CHANGE = BIT(7),
        M_EVENT_CELL_UPDATE = BIT(8),
        M_EVENT_MODEM_EVENT = BIT(9),
        M_EVENT_NEIGHBOR_CELL_MEAS = BIT(10)
      };

      // For use with calls to AT command functions.
      constexpr static const char* M_AT_CFUN { "AT+CFUN?" };
      constexpr static const char* M_CFUN_0 { "0" };  // Power off.
      constexpr static const char* M_CFUN_1 { "0" };  // Normal.
      constexpr static const char* M_CFUN_4 { "4" };  // Flight mode.
      constexpr static const char* M_CFUN_31 { "0" }; // GNSS.


      constexpr static const char* M_AT_CEREG { "AT+CEREG?" };
      constexpr static const char* M_AT_CGATT { "AT+CGATT?" };
      constexpr static const char* M_AT_CGSN { "AT+CGSN" };
      constexpr static const char* M_AT_COPS { "AT+COPS?" };
      constexpr static const char* M_AT_SYSMODE { "ATXSYSTEMMODE?" };

      Modem(App& app);

      bool Init();
      bool Connect();
      bool ConnectAsync();
      bool Restart();
      bool Disconnect();
      bool SetOffline();
      bool SetOnline();
      bool SetGnss();
      bool IsConnected() const { return m_connected; }
      const char* GetImei() const { return m_imei; }
      void GetNrfCloudDeviceId(char* buffer, size_t size) const;;
      k_event* GetEvents() { return &m_events; }
      bool SendAtCommand(const char* command, 
                         const char* expectedResponse = nullptr,
                         char* responseBuffer = nullptr,
                         size_t responseBufferSize = 0,
                         int timeoutMs = 1000);
      bool CheckAtResponse(const char* command, const char* expectedResponse);
      bool ReadyForData(uint32_t timeoutSeconds = 30);
      int GetCfun();
      
    private:
      static void lteHandler(const lte_lc_evt* const event);
      int certificateProvision(void);
      bool getImei();
      void modemConnected(const bool flag);

      k_event m_events;
      
      // Members.
      App& m_app;
      char m_imei[16];
      k_sem m_lte_connected_sem;
      bool m_connected;

      static Modem* s_instance;

      constexpr static int M_CONNECT_TIMEOUT { 180 }; // 3 mins.
      constexpr static size_t M_IMEI_LEN { 15 };
      constexpr static int M_SECS_TO_MS { 1000 };
  };
}
