#include "app.hpp"
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_power.h>
#include <hal/nrf_regulators.h>

LOG_MODULE_REGISTER(application, LOG_LEVEL_INF);

namespace alc
{
  // ========== I2C and PMIC Devices ==========
  
  const struct device* i2c_dev { DEVICE_DT_GET(DT_NODELABEL(i2c2)) };
  // const struct device* pmic_dev { DEVICE_DT_GET(DT_NODELABEL(pmic_main)) };
  // const struct device* charger_dev { DEVICE_DT_GET(DT_NODELABEL(npm1300_charger)) };

  // ========== Singleton ==========
  
  App* App::s_instance { nullptr };

  // ========== Constructor ==========

  App::App()
      : m_led()
      , m_modem(*this)
      , m_motion(i2c_dev, Adxl367::I2cAddress::AddrLow)
      , m_mqtt(*this)
      , m_pmic { DEVICE_DT_GET(DT_NODELABEL(pmic_main)), DEVICE_DT_GET(DT_NODELABEL(npm1300_charger)) }
      , m_boot_count(0)
  {
    s_instance = this;
    m_config.setDefaults();
  }

  // ========== Initialisation ==========

  bool App::Init()
  {
    LOG_INF("╔════════════════════════════════════════╗");
    LOG_INF("║     ALC MAILBOX MONITOR v0.3.0         ║");
    LOG_INF("║     (Simplified Timer Logic)           ║");
    LOG_INF("╚════════════════════════════════════════╝");
    LOG_INF("Device ID: %s", M_DEVICE_ID);
    
    // Log reset reason.
    // logResetReason();
    
    // Increment boot counter.
    // m_boot_count++;
    // LOG_INF("Boot count: %u", m_boot_count);
    
    // Initialise LED.
    if (!m_led.Init()) {
      LOG_ERR("LED init failed!");
      return false;
    }
    LOG_INF("LED initialised.");
    
    // Brief LED flash to indicate boot.
    m_led.SetColour(LedColours::BLUE);
    k_msleep(100);
    m_led.SetColour(LedColours::OFF);
    
    // Check PMIC device.
    if (!device_is_ready(DEVICE_DT_GET(DT_NODELABEL(pmic_main)))) {
      LOG_ERR("PMIC device not ready!");
      return false;
    }
    if (!device_is_ready(DEVICE_DT_GET(DT_NODELABEL(npm1300_charger)))) {
      LOG_ERR("Charger device not ready!");
      return false;
    }
    
    if (m_pmic.Init() != 0) {
      LOG_ERR("nPM1300 init failed!");
      return false;
    }
    LOG_INF("nPM1300 PMIC initialised.");
    
    // Configure the mail window timer (one-time setup).
    // int result = configureMailWindowTimer();
    // if (result < 0) {
    //   LOG_ERR("Mail window timer configuration failed: %d!", result);
    //   return false;
    // }
    // LOG_INF("Mail window timer configured.");
    
    // Initialise modem.
    if (!m_modem.Init()) {
      LOG_ERR("Modem init failed!");
      return false;
    }
    LOG_INF("Modem initialised.");
    
    // Initialise ADXL367.
    int result = m_motion.Init();
    if (result < 0) {
      LOG_ERR("ADXL367 init failed: %d!", result);
      return false;
    }
    LOG_INF("ADXL367 initialised.");
    
    // Configure ADXL367 for motion wake-up.
    result = configureMotionSensor();
    if (result < 0) {
      LOG_ERR("ADXL367 configuration failed: %d!", result);
      return false;
    }
    LOG_INF("ADXL367 configured for wake-up mode.");
    
    // Initialise MQTT client (requires modem but not connection).
    if (!m_mqtt.Init()) {
      LOG_ERR("MQTT init failed!");
      return false;
    }
    LOG_INF("MQTT initialised.");
    
    // Ensure modem is disconnected for clean state.
    m_modem.Disconnect();
    
    LOG_INF("Initialisation complete.");
    return true;
  }

  // ========== Main Run Loop ==========

  void App::Run()
  {
    LOG_INF("════════════════════════════════════════");
    LOG_INF("Entering main run loop...");
    
    // Identify what woke us.
    WakeSource wake { identifyWakeSource() };
    LOG_INF("Wake source: %s", wakeSourceToString(wake));
    
    // Handle the wake event.
    switch (wake) {
      case WakeSource::Accelerometer:
        handleMotionWake();
        break;
        
      case WakeSource::Timer:
        handleTimerWake();
        break;
        
      case WakeSource::HallSensor:
        handleHallSensorWake();
        break;
        
      case WakeSource::PowerOn:
      default:
        handleFreshBoot();
        break;
    }
    
    // Prepare for System OFF.
    LOG_INF("════════════════════════════════════════");
    LOG_INF("Preparing for System OFF...");
    
    // Configure wake sources.
    configureWakeSources();
    
    // Shutdown modem.
    shutdownModem();
    
    // Small delay to allow logs to flush.
    k_msleep(100);
    
    // Enter System OFF - does not return.
    enterSystemOff();
  }

  // ========== Wake Source Identification ==========

  WakeSource App::identifyWakeSource()
  {
    uint32_t resetReason { nrf_power_resetreas_get(NRF_POWER_NS) };
    
    // Clear reset reason after reading.
    nrf_power_resetreas_clear(NRF_POWER_NS, resetReason);
    
    // If not from System OFF, it's a fresh boot.
    if (!(resetReason & POWER_RESETREAS_OFF_Msk)) {
      LOG_INF("Not a System OFF wake - fresh boot.");
      return WakeSource::PowerOn;
    }
    
    LOG_INF("System OFF wake detected. Checking GPIO latches...");
    
    // Check accelerometer latch (P0.11).
    if (nrf_gpio_pin_latch_get(PIN_ACCEL_INT)) {
      LOG_INF("  Accelerometer latch SET (P0.%d)", PIN_ACCEL_INT);
      // nrf_gpio_pin_latch_clear(PIN_ACCEL_INT);
      return WakeSource::Accelerometer;
    }
    
    // Check PMIC/Timer latch (P0.02) - future.
    if (nrf_gpio_pin_latch_get(PIN_PMIC_INT)) {
      LOG_INF("  PMIC/Timer latch SET (P0.%d)", PIN_PMIC_INT);
      // nrf_gpio_pin_latch_clear(PIN_PMIC_INT);
      return WakeSource::Timer;
    }
    
    // No latch identified.
    LOG_WRN("System OFF wake but no GPIO latch set!");
    return WakeSource::Unknown;
  }

  const char* App::wakeSourceToString(WakeSource source)
  {
    switch (source) {
      case WakeSource::PowerOn:       return "POWER_ON";
      case WakeSource::Accelerometer: return "ACCELEROMETER";
      case WakeSource::Timer:         return "TIMER";
      case WakeSource::HallSensor:    return "HALL_SENSOR";
      default:                        return "UNKNOWN";
    }
  }

  // ========== Motion Wake Handler ==========

  void App::handleMotionWake()
  {
    LOG_INF("╔════════════════════════════════════════╗");
    LOG_INF("║         MOTION WAKE DETECTED           ║");
    LOG_INF("╚════════════════════════════════════════╝");
    
    // Visual indication.
    m_led.SetColour(LedColours::AMBER);
    
    // =========================================================================
    // Core Logic: Use ONLY TimerIsExpired() to determine event type
    // =========================================================================
    //
    // This approach relies on the timer expired event flag (EVENTSSHPHLDSET Bit3)
    // which persists across System OFF and reliably indicates timer state:
    //
    //   Expired = TRUE  → Timer has expired (or never started)
    //                   → This is an OPEN event (or timeout/spurious)
    //                   → Clear the flag, start mail window timer
    //
    //   Expired = FALSE → Timer is still running
    //                   → This is a CLOSE event (second motion within window)
    //                   → Send MQTT notification, leave timer running
    //
    // State transitions:
    //   Fresh boot: Run short timer, wait for expiry → Expired=TRUE (ready)
    //   OPEN event: Clear expired flag, start timer → Expired=FALSE (waiting)
    //   CLOSE event: Send MQTT, timer keeps running → Expired=FALSE
    //   Timer expires naturally: → Expired=TRUE (ready for next cycle)
    //
    // Edge case: If mailbox opened twice within window, both trigger CLOSE
    // events. Server-side deduplication handles this.
    // =========================================================================
    
    bool timerExpired = m_pmic.TimerIsExpired();
    
    LOG_INF("────────────────────────────────────────");
    LOG_INF("TIMER STATE:");
    LOG_INF("  Timer expired: %s", timerExpired ? "YES (ready for OPEN)" : "NO (waiting for CLOSE)");
    LOG_INF("  Mail window:   %u seconds", m_config.mailWindowSecs);
    LOG_INF("────────────────────────────────────────");
    
    if (timerExpired) {
      // ===== OPEN EVENT (or timeout/spurious) =====
      LOG_INF("╔════════════════════════════════════════╗");
      LOG_INF("║  OPEN EVENT - STARTING TIMER           ║");
      LOG_INF("╚════════════════════════════════════════╝");
      
      // Clear the expired flag first.
      m_pmic.TimerClearEvent();
      
      // Set duration and start the timer.
      int result = m_pmic.TimerSetDuration(m_config.mailWindowSecs);
      if (result < 0) {
        LOG_ERR("Failed to set timer duration: %d", result);
      }
      
      result = m_pmic.TimerStart();
      if (result < 0) {
        LOG_ERR("Failed to start timer: %d", result);
      } else {
        LOG_INF("Timer started: %u second window.", m_config.mailWindowSecs);
      }
      
      // No network activity needed for open event.
      LOG_INF("Open event recorded. Waiting for close event...");
      
    } else {
      // ===== CLOSE EVENT - MAIL DELIVERED =====
      LOG_INF("╔════════════════════════════════════════╗");
      LOG_INF("║  CLOSE EVENT - MAIL DELIVERED!         ║");
      LOG_INF("╚════════════════════════════════════════╝");
      
      // Don't touch the timer - let it expire naturally.
      // This resets the state to "ready for OPEN" automatically.
      LOG_INF("Timer left running - will expire and reset state.");
      
      // LED indication - green for mail delivery.
      m_led.SetColour(LedColours::GREEN);
      
      // Connect and send mail delivery notification.
      if (connectToCloud()) {
        bool ownerIntervened { false };  // Placeholder for future Hall sensor.
        
        if (sendMailDeliveredEvent(ownerIntervened)) {
          LOG_INF("Mail delivery event sent successfully.");
          
          // Also send battery status while connected.
          sendBatteryStatus();
          
          // Collect any pending commands.
          collectMqttCommands();
        } else {
          LOG_ERR("Failed to send mail delivery event!");
          m_led.SetColour(LedColours::RED);
          k_msleep(500);
        }
        
        disconnectFromCloud();
      } else {
        LOG_ERR("Failed to connect to cloud!");
        m_led.SetColour(LedColours::RED);
        k_msleep(500);
      }
    }
    
    // Turn off LED.
    m_led.SetColour(LedColours::OFF);
    
    LOG_INF("Motion wake handling complete.");
  }

  // ========== Timer Wake Handler (Future - Heartbeat) ==========

  void App::handleTimerWake()
  {
    LOG_INF("╔════════════════════════════════════════╗");
    LOG_INF("║          TIMER WAKE (HEARTBEAT)        ║");
    LOG_INF("╚════════════════════════════════════════╝");
    
    // Clear the timer event.
    m_pmic.TimerClearEvent();
    
    // Visual indication.
    m_led.SetColour(LedColours::BLUE);
    
    // Connect and send heartbeat.
    if (connectToCloud()) {
      sendHeartbeat();
      sendBatteryStatus();
      collectMqttCommands();
      disconnectFromCloud();
    } else {
      LOG_ERR("Failed to connect for heartbeat!");
      m_led.SetColour(LedColours::RED);
      k_msleep(500);
    }
    
    m_led.SetColour(LedColours::OFF);
    
    // TODO: Restart timer for next heartbeat (24 hours).
    // For now, heartbeat timer is not implemented.
    
    LOG_INF("Timer wake handling complete.");
  }

  // ========== Hall Sensor Wake Handler (Future Stub) ==========

  void App::handleHallSensorWake()
  {
    LOG_INF("╔════════════════════════════════════════╗");
    LOG_INF("║       HALL SENSOR WAKE (STUB)          ║");
    LOG_INF("╚════════════════════════════════════════╝");
    
    // TODO: Implement owner interaction logic.
    // For now, just log and return to sleep.
    
    LOG_INF("Hall sensor wake not yet implemented.");
    
    m_led.SetColour(LedColours::ORANGE);
    k_msleep(200);
    m_led.SetColour(LedColours::OFF);
  }

  // ========== Fresh Boot Handler ==========

  void App::handleFreshBoot()
  {
    LOG_INF("╔════════════════════════════════════════╗");
    LOG_INF("║           FRESH BOOT / RESET           ║");
    LOG_INF("╚════════════════════════════════════════╝");
    
    // Visual indication - blue pulse.
    m_led.SetColour(LedColours::BLUE);
    k_msleep(500);
    m_led.SetColour(LedColours::OFF);
    
    // =========================================================================
    // Initialise timer state: Run short timer and wait for expiry
    // =========================================================================
    //
    // This establishes the "ready for OPEN" state by ensuring TimerIsExpired()
    // returns TRUE after fresh boot. The expired flag persists across System OFF.
    //
    // Cost: One-time 3-second blocking wait on fresh boot only.
    // =========================================================================
    
    constexpr uint32_t INIT_TIMER_SECS { 3 };
    constexpr uint32_t POLL_INTERVAL_MS { 100 };
    constexpr uint32_t TIMEOUT_MS { 5000 };  // Safety timeout.
    
    LOG_INF("Initialising timer state (one-time %u second wait)...", INIT_TIMER_SECS);
    
    // Clear any stale state.
    m_pmic.TimerStop();
    m_pmic.TimerClearEvent();
    
    // Start short initialisation timer.
    int result = m_pmic.TimerSetDuration(INIT_TIMER_SECS);
    if (result < 0) {
      LOG_ERR("Failed to set init timer duration: %d", result);
    }
    
    result = m_pmic.TimerStart();
    if (result < 0) {
      LOG_ERR("Failed to start init timer: %d", result);
    }
    
    // Wait for timer to expire.
    uint32_t elapsed { 0 };
    while (!m_pmic.TimerIsExpired() && (elapsed < TIMEOUT_MS)) {
      k_msleep(POLL_INTERVAL_MS);
      elapsed += POLL_INTERVAL_MS;
    }
    
    if (m_pmic.TimerIsExpired()) {
      LOG_INF("Init timer expired - state ready (Expired=TRUE).");
      // IMPORTANT: Do NOT clear the expired flag!
      // Leaving it SET means TimerIsExpired() returns TRUE = ready for OPEN.
    } else {
      LOG_ERR("Init timer did not expire within timeout!");
      // Force the expired state by clearing and leaving.
      m_pmic.TimerStop();
    }
    
    // // Send a startup notification.
    // LOG_INF("Sending startup notification...");
    // 
    // if (connectToCloud()) {
    //   // Send startup/heartbeat.
    //   sendHeartbeat();
    //   sendBatteryStatus();
    //   collectMqttCommands();
    //   disconnectFromCloud();
    // } else {
    //   LOG_WRN("Failed to connect on fresh boot. Will try on next wake.");
    // }
    // 
    // LOG_INF("Fresh boot handling complete.");
  }

  // ========== Timer Configuration ==========

  int App::configureMailWindowTimer()
  {
    LOG_INF("Configuring nPM1300 GP Timer for mail window...");
    
    // Configure timer: GP mode, slow prescaler (16ms/tick).
    // This only needs to be done once - the configuration persists.
    int result = m_pmic.TimerConfigure(
      Npm1300::TimerMode::GeneralPurpose,
      Npm1300::TimerPrescaler::Slow
    );
    
    if (result < 0) {
      LOG_ERR("Failed to configure timer: %d", result);
      return result;
    }
    
    LOG_INF("Timer configured: GP mode, Slow prescaler (16ms/tick).");
    LOG_INF("Mail window duration: %u seconds.", m_config.mailWindowSecs);
    
    return 0;
  }

  // ========== MQTT Operations ==========

  bool App::connectToCloud()
  {
    LOG_INF("Connecting to cloud...");
    
    // Power up modem and connect.
    if (!m_modem.Connect()) {
      LOG_ERR("Modem connect failed!");
      return false;
    }
    
    // Connect MQTT.
    if (!m_mqtt.Connect()) {
      LOG_ERR("MQTT connect failed!");
      m_modem.Disconnect();
      return false;
    }
    
    LOG_INF("Cloud connection established.");
    return true;
  }

  void App::disconnectFromCloud()
  {
    LOG_INF("Disconnecting from cloud...");
    
    m_mqtt.Disconnect();
    m_modem.Disconnect();
    
    LOG_INF("Disconnected.");
  }

  bool App::sendHeartbeat()
  {
    char topic[64];
    char message[128];
    
    int len { snprintf(message, sizeof(message),
                       "{\"event\":\"heartbeat\","
                       "\"boot_count\":%u,"
                       "\"mail_window_secs\":%u}",
                       m_boot_count, m_config.mailWindowSecs) };
    
    buildTopic(topic, sizeof(topic), M_SUFFIX_HEARTBEAT);
    
    LOG_INF("Sending heartbeat: %s", message);
    
    if (!m_mqtt.Publish(topic, message, len, false)) {
      LOG_ERR("Failed to publish heartbeat!");
      return false;
    }
    
    return true;
  }

  bool App::sendMailDeliveredEvent(bool ownerIntervened)
  {
    char topic[64];
    char message[128];
    
    int len { snprintf(message, sizeof(message),
                       "{\"event\":\"mail_delivered\","
                       "\"owner_intervened\":%s}",
                       ownerIntervened ? "true" : "false") };
    
    buildTopic(topic, sizeof(topic), M_SUFFIX_EVENTS);
    
    LOG_INF("Sending mail delivered event: %s", message);
    
    if (!m_mqtt.Publish(topic, message, len, false)) {
      LOG_ERR("Failed to publish mail event!");
      return false;
    }
    
    return true;
  }

  bool App::sendBatteryStatus()
  {
    char topic[64];
    char message[256];
    
    // Read actual battery values from nPM1300.
    Npm1300::SensorData sensorData;
    int result = m_pmic.ReadSensors(sensorData);
    
    if (result < 0) {
      LOG_WRN("Failed to read battery sensors: %d", result);
      // Send placeholder values.
      sensorData.voltage = 0.0f;
      sensorData.current = 0.0f;
      sensorData.temperature = 0.0f;
      sensorData.chargeStatus = Npm1300::ChargeStatus::Idle;
    }
    
    bool isCharging = (sensorData.chargeStatus != Npm1300::ChargeStatus::Idle &&
                       sensorData.chargeStatus != Npm1300::ChargeStatus::Complete);
    
    int len { snprintf(message, sizeof(message),
                       "{\"voltage_v\":%.2f,"
                       "\"current_ma\":%.1f,"
                       "\"temp_c\":%.1f,"
                       "\"charging\":%s,"
                       "\"vbus\":%s}",
                       static_cast<double>(sensorData.voltage),
                       static_cast<double>(sensorData.current * 1000.0f),
                       static_cast<double>(sensorData.temperature),
                       isCharging ? "true" : "false",
                       m_pmic.IsVbusConnected() ? "true" : "false") };
    
    buildTopic(topic, sizeof(topic), M_SUFFIX_BATTERY);
    
    LOG_INF("Sending battery status: %s", message);
    
    if (!m_mqtt.Publish(topic, message, len, false)) {
      LOG_ERR("Failed to publish battery status!");
      return false;
    }
    
    return true;
  }

  void App::collectMqttCommands()
  {
    LOG_INF("Collecting MQTT commands...");
    
    constexpr int COLLECT_TIME_MS { 2000 };
    constexpr int POLL_INTERVAL_MS { 100 };
    
    int elapsed { 0 };
    while (elapsed < COLLECT_TIME_MS) {
      m_mqtt.KeepAlive(POLL_INTERVAL_MS);
      m_mqtt.ProcessEvents();
      k_sleep(K_MSEC(POLL_INTERVAL_MS));
      elapsed += POLL_INTERVAL_MS;
    }
    
    LOG_INF("Command collection complete.");
  }

  // ========== MQTT Callbacks ==========

  void App::OnMqttConnected()
  {
    LOG_INF("MQTT connected callback.");
  }

  void App::OnMqttDisconnected()
  {
    LOG_INF("MQTT disconnected callback.");
  }

  void App::OnMqttMessageReceived(const char* message, size_t length)
  {
    LOG_INF("MQTT message received: %.*s", static_cast<int>(length), message);
    
    MqttCommand cmd { parseCommand(message, length) };
    executeCommand(cmd, message, length);
  }

  // ========== Command Handling ==========

  MqttCommand App::parseCommand(const char* message, size_t length)
  {
    if (strstr(message, "\"mail_window\"")) {
      return MqttCommand::SET_MAIL_WINDOW;
    }
    if (strstr(message, "\"status_request\"")) {
      return MqttCommand::REQUEST_STATUS;
    }
    if (strstr(message, "\"firmware_update\"")) {
      return MqttCommand::FIRMWARE_UPDATE;
    }
    
    return MqttCommand::UNKNOWN;
  }

  void App::executeCommand(MqttCommand cmd, const char* message, size_t length)
  {
    int value;
    
    switch (cmd) {
      case MqttCommand::SET_MAIL_WINDOW:
        value = extractIntValue(message, "mail_window");
        if (value > 0) {
          m_config.mailWindowSecs = static_cast<uint32_t>(value);
          LOG_INF("Mail window set to %d seconds.", value);
        }
        break;
        
      case MqttCommand::REQUEST_STATUS:
        LOG_INF("Status request received - will send on next connection.");
        break;
        
      case MqttCommand::FIRMWARE_UPDATE:
        LOG_INF("Firmware update requested - not implemented yet.");
        break;
        
      default:
        LOG_WRN("Unknown command received.");
        break;
    }
  }

  int App::extractIntValue(const char* message, const char* key)
  {
    char searchKey[64];
    snprintf(searchKey, sizeof(searchKey), "\"%s\":", key);
    
    const char* pos { strstr(message, searchKey) };
    if (!pos) { return -1; }
    
    pos += strlen(searchKey);
    while (*pos == ' ' || *pos == '\t') { pos++; }
    
    return atoi(pos);
  }

  // ========== Sensor Configuration ==========

  int App::configureMotionSensor()
  {
    int result;
    
    LOG_INF("Configuring ADXL367 for motion wake-up...");
    
    // Put in standby for configuration.
    result = m_motion.SetOperatingMode(Adxl367::OperatingMode::Standby);
    if (result < 0) { return result; }
    
    // Configure activity detection.
    Adxl367::ActivityConfig actConfig {
      .activityMode = Adxl367::ActivityMode::Referenced,
      .inactivityMode = Adxl367::ActivityMode::Absolute,
      .linkLoop = Adxl367::LinkLoopMode::Loop,
      .activityThreshold = 250,     // 250mg - detect lid movement.
      .activityTime = 1,            // 1 sample.
      .inactivityThreshold = 1200,  // 1.2g - above gravity for absolute mode.
      .inactivityTime = 10          // ~1.6s at 6 SPS.
    };
    
    result = m_motion.ConfigureActivity(actConfig);
    if (result < 0) { return result; }
    
    // Map AWAKE signal to INT1 (active high for System OFF wake).
    result = m_motion.ConfigureInterrupt(Adxl367::IntPin::Int1, true, false);
    if (result < 0) { return result; }
    
    // Set range.
    result = m_motion.SetRange(Adxl367::Range::Range2g);
    if (result < 0) { return result; }
    
    // Enable wake-up mode (180 nA operation).
    result = m_motion.EnableWakeupMode(Adxl367::WakeupRate::Rate6Sps);
    if (result < 0) { return result; }
    
    // Enter measurement mode.
    result = m_motion.SetOperatingMode(Adxl367::OperatingMode::Measurement);
    if (result < 0) { return result; }
    
    LOG_INF("ADXL367 configured: wake-up mode, AWAKE→INT1.");
    return 0;
  }

  // ========== Power Management ==========

  void App::configureWakeSources()
  {
    LOG_INF("Configuring wake sources for System OFF...");
    
    // Clear any pending latches.
    nrf_gpio_pin_latch_clear(PIN_ACCEL_INT);
    nrf_gpio_pin_latch_clear(PIN_PMIC_INT);
    
    // Read ADXL367 status to clear any pending interrupt.
    Adxl367::Status status;
    m_motion.ReadStatus(status);
    LOG_INF("ADXL367 status: AWAKE=%d", status.awake);
    
    // Small delay for INT1 to settle.
    k_msleep(10);
    
    // Configure accelerometer INT1 - sense HIGH (AWAKE signal).
    nrf_gpio_cfg_input(PIN_ACCEL_INT, NRF_GPIO_PIN_PULLDOWN);
    uint32_t pinState { nrf_gpio_pin_read(PIN_ACCEL_INT) };
    LOG_INF("INT1 (P0.%d) state: %d (must be 0 for wake to work)", PIN_ACCEL_INT, pinState);
    
    if (pinState != 0) {
      LOG_WRN("INT1 is HIGH - device may wake immediately!");
    }
    
    nrf_gpio_cfg_sense_set(PIN_ACCEL_INT, NRF_GPIO_PIN_SENSE_HIGH);
    
    // Future: Configure PMIC GPIO for timer wake (heartbeat).
    // nrf_gpio_cfg_input(PIN_PMIC_INT, NRF_GPIO_PIN_PULLDOWN);
    // nrf_gpio_cfg_sense_set(PIN_PMIC_INT, NRF_GPIO_PIN_SENSE_HIGH);
    
    LOG_INF("Wake sources configured: Accel (P0.%d)", PIN_ACCEL_INT);
  }

  void App::shutdownModem()
  {
    LOG_INF("Shutting down modem...");
    m_modem.Disconnect();
    LOG_INF("Modem shutdown complete.");
  }

  void App::enterSystemOff()
  {
    LOG_INF("Entering System OFF...");
    LOG_INF("Goodbye! Waiting for wake event...");
    
    // Small delay to allow final log output.
    k_msleep(50);
    
    // Enter System OFF.
    NRF_REGULATORS->SYSTEMOFF = 1;
    
    // Should never reach here.
    while (true) {
      k_sleep(K_FOREVER);
    }
  }

  // ========== Utility ==========

  void App::buildTopic(char* buffer, size_t size, const char* suffix)
  {
    snprintf(buffer, size, "alc/%s/%s", M_DEVICE_ID, suffix);
  }

  void App::logResetReason()
  {
    uint32_t reason { nrf_power_resetreas_get(NRF_POWER_NS) };
    
    LOG_INF("Reset reason flags: 0x%08X", reason);
    
    if (reason == 0) {
      LOG_INF("  Power-on reset");
    }
    if (reason & POWER_RESETREAS_RESETPIN_Msk) {
      LOG_INF("  Reset pin");
    }
    if (reason & POWER_RESETREAS_DOG_Msk) {
      LOG_INF("  Watchdog");
    }
    if (reason & POWER_RESETREAS_OFF_Msk) {
      LOG_INF("  System OFF wake");
    }
  }
}
