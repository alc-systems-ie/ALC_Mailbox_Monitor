#include "app.hpp"
#include "retained.hpp"
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_power.h>
#include <hal/nrf_regulators.h>

LOG_MODULE_REGISTER(application, LOG_LEVEL_INF);

namespace alc
{
  // ========== I2C Device ==========

  const struct device* i2c_dev { DEVICE_DT_GET(DT_NODELABEL(i2c2)) };

  // ========== Singleton ==========

  App* App::s_instance { nullptr };

  // ========== Free Function ==========

  const char* wakeSourceToString(WakeSource source)
  {
    switch (source) {
      case WakeSource::PowerOn:       return "POWER_ON";
      case WakeSource::Accelerometer: return "ACCELEROMETER";
      case WakeSource::Timer:         return "TIMER";
      case WakeSource::HallSensor:    return "HALL_SENSOR";
      default:                        return "UNKNOWN";
    }
  }

  // ========== Constructor ==========

  App::App()
      : m_modem(*this)
      , m_motion(i2c_dev, Adxl367::I2cAddress::AddrLow)
      , m_mqtt(*this)
      , m_pmic { DEVICE_DT_GET(DT_NODELABEL(pmic_main)), DEVICE_DT_GET(DT_NODELABEL(npm1300_charger)) }
      , m_boot_count(0)
  {
    s_instance = this;
    m_config.setDefaults();
  }

  // ========== Start (Main Entry Point) ==========

  void App::Start(WakeSource wake, bool awakeAtBoot)
  {
    m_awakeAtBoot = awakeAtBoot;
    LOG_INF("╔════════════════════════════════════════╗");
    LOG_INF("║     ALC MAILBOX MONITOR v0.3.0         ║");
    LOG_INF("║     (Simplified Timer Logic)           ║");
    LOG_INF("╚════════════════════════════════════════╝");

    // =========================================================================
    // Initialise hardware.
    // =========================================================================
    if (!initHardware()) {
      LOG_ERR("Hardware initialisation failed!");
      // Brief error indication then reset.
      k_sleep(K_SECONDS(5));
      NVIC_SystemReset();
    }

    // =========================================================================
    // Initialise event buffer (loads from NVS flash).
    // =========================================================================
    initEventBuffer();

    LOG_INF("════════════════════════════════════════");

    // =========================================================================
    // Handle the wake event.
    // =========================================================================
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

    // =========================================================================
    // Prepare for System OFF.
    // =========================================================================
    LOG_INF("════════════════════════════════════════");
    LOG_INF("Preparing for System OFF...");

    configureWakeSources();
    shutdownModem();

    // Small delay to allow logs to flush.
    k_msleep(100);

    // Enter System OFF - does not return.
    enterSystemOff();
  }

  // ========== Hardware Initialisation ==========

  bool App::initHardware()
  {
    LOG_INF("Device ID: %s", M_DEVICE_ID);

    // =========================================================================
    // Essential hardware only - PMIC and accelerometer.
    // Modem/MQTT initialised lazily on CLOSE events to speed up OPEN events.
    // =========================================================================

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

    LOG_INF("Hardware initialisation complete.");
    return true;
  }

  bool App::initNetworkHardware()
  {
    LOG_INF("Initialising network hardware...");

    // Initialise modem.
    if (!m_modem.Init()) {
      LOG_ERR("Modem init failed!");
      return false;
    }
    LOG_INF("Modem initialised.");

    // Initialise MQTT client.
    if (!m_mqtt.Init()) {
      LOG_ERR("MQTT init failed!");
      return false;
    }
    LOG_INF("MQTT initialised.");

    // Ensure modem is disconnected for clean state.
    m_modem.Disconnect();

    m_networkInitialised = true;
    LOG_INF("Network hardware initialisation complete.");
    return true;
  }

  // ========== Motion Wake Handler ==========

  void App::handleMotionWake()
  {
    LOG_INF("╔════════════════════════════════════════╗");
    LOG_INF("║         MOTION WAKE DETECTED           ║");
    LOG_INF("╚════════════════════════════════════════╝");

    // =========================================================================
    // Check if device is enabled for normal operation.
    // =========================================================================
    if (!isEnabled()) {
      LOG_INF("Device disabled - motion wake ignored.");
      LOG_INF("Entering provisioning mode...");
      handleProvisioningMode();
      // Does not return (loops until enabled, then reboots).
    }

    // =========================================================================
    // Motion wake event classification (pre-init AWAKE capture):
    //
    // The AWAKE pin state is captured in main.cpp BEFORE any driver init
    // (which resets the ADXL367). This tells us if motion was still ongoing
    // when the MCU booted (~250ms after wake).
    //
    // Case 1 (Bump):
    //   AWAKE was LOW at boot → motion ended before MCU started → ignore
    //
    // Case 2 (Mail delivery):
    //   AWAKE was HIGH at boot → real movement → poll until rest → send event
    //
    // Case 3 (Door left open):
    //   AWAKE was HIGH at boot → poll times out after 30s → not home →
    //   start incremental timer.
    //   TODO: Implement with PMIC timer wake (P0.02).
    // =========================================================================

    constexpr uint32_t M_AWAKE_TIMEOUT_MS { 30000 };  // 30 seconds.
    constexpr uint32_t M_AWAKE_POLL_MS { 200 };

    // Home position (flat on desk — adjust for mounted orientation).
    constexpr int16_t M_HOME_X { 0 };
    constexpr int16_t M_HOME_Y { 0 };
    constexpr int16_t M_HOME_Z { -1000 };  // Gravity on Z-axis when flat.

    LOG_INF("AWAKE at boot: %s", m_awakeAtBoot ? "YES (motion ongoing)" : "NO (bump)");

    // =========================================================================
    // Case 1: AWAKE was already LOW at boot — brief bump.
    // =========================================================================
    if (!m_awakeAtBoot) {
      LOG_INF("Classification: BUMP (AWAKE cleared before boot)");
      LOG_INF("Ignoring spurious wake - returning to sleep.");
      return;
    }

    // =========================================================================
    // AWAKE was HIGH at boot — real movement. Poll until it clears.
    // Note: configureMotionSensor() has already run (resets ADXL367), so we
    // poll the freshly-configured sensor. AWAKE will reassert if the device
    // is still displaced from its new reference point.
    // =========================================================================
    LOG_INF("Polling AWAKE until device returns to rest...");

    int64_t startTime { k_uptime_get() };
    uint32_t elapsed { 0 };

    while (m_motion.IsAwake() && (elapsed < M_AWAKE_TIMEOUT_MS)) {
      k_msleep(M_AWAKE_POLL_MS);
      elapsed = static_cast<uint32_t>(k_uptime_get() - startTime);

      // Log progress every 5 seconds.
      if ((elapsed % 5000) < M_AWAKE_POLL_MS) {
        int16_t x { 0 }, y { 0 }, z { 0 };
        m_motion.ReadAxes(x, y, z);
        LOG_INF("[%5u ms] AWAKE=1 | X=%d Y=%d Z=%d mg", elapsed, x, y, z);
      }
    }

    LOG_INF("AWAKE polling complete: %u ms", elapsed);

    // Small delay for readings to settle.
    k_msleep(100);

    // Read settled position.
    int16_t x { 0 }, y { 0 }, z { 0 };
    m_motion.ReadAxes(x, y, z);
    LOG_INF("Settled position: X=%d Y=%d Z=%d mg", x, y, z);

    // Check if device is at home position.
    int16_t threshold { static_cast<int16_t>(m_config.activityThresholdMg) };
    bool atHome { (abs(x - M_HOME_X) < threshold) &&
                  (abs(y - M_HOME_Y) < threshold) &&
                  (abs(z - M_HOME_Z) < threshold) };

    LOG_INF("Position: %s", atHome ? "HOME" : "NOT HOME");

    // =========================================================================
    // Case 3: AWAKE timeout — door left open.
    // =========================================================================
    if (elapsed >= M_AWAKE_TIMEOUT_MS) {
      if (!atHome) {
        LOG_INF("╔════════════════════════════════════════╗");
        LOG_INF("║       DOOR LEFT OPEN                   ║");
        LOG_INF("╚════════════════════════════════════════╝");
        // TODO: Send door_open event.
        // TODO: Start incremental timer (4min → 1hr → 2hr) via nPM1300.
        // TODO: Recalibrate ADXL367 to allow System OFF.
        // TODO: Stop incremental timer at every close event.
        LOG_INF("Door-open handling not yet implemented - sleeping.");
        return;
      }

      // AWAKE stuck but device is at home — recalibrate to clear.
      LOG_INF("AWAKE stuck at home - recalibrating ADXL367...");
      int recalResult { configureMotionSensor() };
      if (recalResult < 0) {
        LOG_ERR("Recalibration failed: %d", recalResult);
      } else {
        LOG_INF("Recalibration complete.");
      }
    }

    if (!atHome) {
      LOG_WRN("Device at rest but NOT at home position - ignoring.");
      return;
    }

    // =========================================================================
    // Case 2: Mail delivery — AWAKE was HIGH at boot, device now at home.
    // =========================================================================

    // TODO: Stop incremental timer if running (case 3 recovery).

    // ===== MAIL DELIVERY =====
    LOG_INF("╔════════════════════════════════════════╗");
    LOG_INF("║       MAIL DELIVERED!                  ║");
    LOG_INF("╚════════════════════════════════════════╝");

    uint32_t timestamp = static_cast<uint32_t>(k_uptime_get() / 1000);
    bool ownerIntervened { false };  // Placeholder for future Hall sensor.

    // Buffer the event first (ensures it's not lost if connection fails).
    bufferMailEvent(timestamp, ownerIntervened);

    // Initialise network hardware (modem, MQTT).
    if (!initNetworkHardware()) {
      LOG_ERR("Network init failed - event buffered for later.");
      LOG_INF("Buffered events: %d", getBufferedEventCount());
      return;
    }

    // Attempt to connect and send all buffered events.
    if (connectToCloud()) {
      if (sendBufferedEvents()) {
        LOG_INF("All buffered events sent successfully.");
        sendBatteryStatus();
        collectMqttCommands();
      } else {
        LOG_ERR("Failed to send some buffered events!");
      }
      disconnectFromCloud();
    } else {
      LOG_ERR("Failed to connect to cloud - events buffered for later.");
      LOG_INF("Buffered events: %d", getBufferedEventCount());
    }

    LOG_INF("Motion wake handling complete.");
  }

  // ========== Timer Wake Handler ==========

  void App::handleTimerWake()
  {
    // Clear the timer event.
    m_pmic.TimerClearEvent();

    // Check if we're in provisioning mode.
    // Note: This shouldn't happen since provisioning mode no longer uses System OFF,
    // but handle it gracefully by entering provisioning loop.
    if (!isEnabled()) {
      LOG_INF("╔════════════════════════════════════════╗");
      LOG_INF("║      TIMER WAKE (DISABLED)             ║");
      LOG_INF("╚════════════════════════════════════════╝");
      LOG_INF("Device disabled - entering provisioning mode.");
      handleProvisioningMode();
      // Does not return (loops until enabled, then reboots).
    }

    // Normal heartbeat mode.
    LOG_INF("╔════════════════════════════════════════╗");
    LOG_INF("║          TIMER WAKE (HEARTBEAT)        ║");
    LOG_INF("╚════════════════════════════════════════╝");

    // Initialise network hardware for heartbeat.
    if (!initNetworkHardware()) {
      LOG_ERR("Network init failed for heartbeat!");
      return;
    }

    // Connect and send heartbeat (also sends any buffered events).
    if (connectToCloud()) {
      // Send any buffered events first.
      if (hasBufferedEvents()) {
        LOG_INF("Sending %d buffered events...", getBufferedEventCount());
        sendBufferedEvents();
      }

      sendHeartbeat();
      sendBatteryStatus();
      collectMqttCommands();
      disconnectFromCloud();
    } else {
      LOG_ERR("Failed to connect for heartbeat!");
    }

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
  }

  // ========== Fresh Boot Handler ==========

  void App::handleFreshBoot()
  {
    LOG_INF("╔════════════════════════════════════════╗");
    LOG_INF("║           FRESH BOOT / RESET           ║");
    LOG_INF("╚════════════════════════════════════════╝");

    // =========================================================================
    // Check if device is enabled for normal operation.
    // =========================================================================
    if (!isEnabled()) {
      LOG_INF("Device DISABLED - entering provisioning mode.");
      handleProvisioningMode();
      // handleProvisioningMode() loops until enabled, then reboots.
      // Does not return.
    }

  }

  // ========== Provisioning Mode Handler ==========

  void App::handleProvisioningMode()
  {
    LOG_INF("╔════════════════════════════════════════╗");
    LOG_INF("║         PROVISIONING MODE              ║");
    LOG_INF("╚════════════════════════════════════════╝");
    LOG_INF("Polling every %u seconds for enable command.", getPollInterval());

    // Initialise network hardware once.
    if (!initNetworkHardware()) {
      LOG_ERR("Network init failed in provisioning mode!");
      // Still enter the loop - will retry on next iteration.
    }

    // =========================================================================
    // Provisioning Loop: Stay awake, poll periodically for enable command.
    // =========================================================================
    // Unlike normal operation, provisioning mode does NOT use System OFF.
    // This allows faster response to enable commands and simpler state management.
    // The device will reboot when enabled to ensure clean timer state.
    // =========================================================================

    while (!isEnabled()) {
      LOG_INF("────────────────────────────────────────");
      LOG_INF("Provisioning poll cycle starting...");

      // Connect and send status/battery, check for commands.
      if (connectToCloud()) {
        sendConfigStatus();  // Includes enabled:false, provisioning:true.
        sendBatteryStatus();
        collectMqttCommands();  // Will process enable command if present.
        disconnectFromCloud();
      } else {
        LOG_ERR("Failed to connect in provisioning mode.");
      }

      // Check if we were enabled by a command.
      if (isEnabled()) {
        LOG_INF("Device ENABLED - rebooting into normal mode...");
        k_msleep(100);  // Allow logs to flush.
        sys_reboot(SYS_REBOOT_COLD);
        // Does not return.
      }

      // Sleep for poll interval (staying awake, not System OFF).
      LOG_INF("Still disabled - waiting %u seconds...", getPollInterval());
      k_sleep(K_SECONDS(getPollInterval()));
    }

    // Should not reach here, but if we do, reboot.
    LOG_INF("Exiting provisioning mode - rebooting...");
    k_msleep(100);
    sys_reboot(SYS_REBOOT_COLD);
  }

  // ========== Event Buffer Initialisation ==========

  void App::initEventBuffer()
  {
    LOG_INF("Initialising event buffer from NVS flash...");

    int result = retainedInit();
    if (result < 0) {
      LOG_ERR("Failed to initialise event buffer: %d", result);
      // Continue anyway - will use defaults.
    }

    if (hasBufferedEvents()) {
      LOG_INF("Found %d buffered events from previous session.", getBufferedEventCount());
    }
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

    // Power up modem and connect (with timeout).
    if (!m_modem.ConnectAsync()) {
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

  bool App::sendMailDeliveredEvent(uint32_t timestamp, bool ownerIntervened)
  {
    char topic[64];
    char message[256];

    // TODO: When RTC is fitted, timestamp will be Unix epoch.
    // For now it's seconds since boot.
    int len { snprintf(message, sizeof(message),
                       "{\"event\":\"mail_delivered\","
                       "\"timestamp\":%u,"
                       "\"owner_intervened\":%s}",
                       timestamp,
                       ownerIntervened ? "true" : "false") };

    buildTopic(topic, sizeof(topic), M_SUFFIX_EVENTS);

    LOG_INF("Sending mail delivered event: %s", message);

    // TODO: Re-enable when FIFO testing complete.
    // if (!m_mqtt.Publish(topic, message, len, false)) {
    //   LOG_ERR("Failed to publish mail event!");
    //   return false;
    // }
    LOG_INF("(MQTT publish suppressed for FIFO testing)");

    return true;
  }

  bool App::sendBufferedEvents()
  {
    uint8_t count = getBufferedEventCount();
    if (count == 0) {
      return true;  // Nothing to send.
    }

    LOG_INF("Sending %d buffered events...", count);

    bool allSent = true;

    for (uint8_t i = 0; i < count; i++) {
      BufferedEvent event;
      if (!getBufferedEvent(i, event)) {
        LOG_ERR("Failed to get buffered event %d", i);
        allSent = false;
        continue;
      }

      // For older events (all except the last), set owner_intervened=true
      // to suppress SMS notifications and prevent flooding carers.
      bool suppressSms = (i < count - 1);
      bool ownerIntervened = suppressSms ? true : event.owner_intervened;

      if (suppressSms) {
        LOG_INF("Event %d/%d: timestamp=%u (catch-up, SMS suppressed)",
                i + 1, count, event.timestamp);
      } else {
        LOG_INF("Event %d/%d: timestamp=%u, owner_intervened=%d",
                i + 1, count, event.timestamp, event.owner_intervened);
      }

      if (!sendMailDeliveredEvent(event.timestamp, ownerIntervened)) {
        LOG_ERR("Failed to send buffered event %d", i);
        allSent = false;
        // Continue trying to send remaining events.
      }
    }

    // Clear buffer only if all events were sent.
    if (allSent) {
      clearBufferedEvents();
      LOG_INF("All buffered events sent and cleared.");
    } else {
      LOG_WRN("Some events failed to send - buffer NOT cleared.");
    }

    return allSent;
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

    // Convert charge status enum to string for JSON.
    const char* chargeStatusStr;
    switch (sensorData.chargeStatus) {
      case Npm1300::ChargeStatus::Trickle:
        chargeStatusStr = "trickle";
        break;
      case Npm1300::ChargeStatus::ConstantCurrent:
        chargeStatusStr = "cc";
        break;
      case Npm1300::ChargeStatus::ConstantVoltage:
        chargeStatusStr = "cv";
        break;
      case Npm1300::ChargeStatus::Complete:
        chargeStatusStr = "complete";
        break;
      case Npm1300::ChargeStatus::Idle:
      default:
        chargeStatusStr = "idle";
        break;
    }

    // Estimate SoC from voltage (simple linear approximation).
    // Li-Po: 3.0V = 0%, 4.2V = 100%.
    int level { static_cast<int>((sensorData.voltage - 3.0f) / 1.2f * 100.0f) };
    if (level < 0) { level = 0; }
    if (level > 100) { level = 100; }

    // Convert to integer units for standardised format.
    int voltage_mv { static_cast<int>(sensorData.voltage * 1000.0f) };
    int current_ma { static_cast<int>(sensorData.current * 1000.0f) };
    int temperature_c { static_cast<int>(sensorData.temperature) };

    // VBUS connected indicates charging capability.
    bool charging { m_pmic.IsVbusConnected() };

    int len { snprintf(message, sizeof(message),
                       "{\"level\":%d,"
                       "\"voltage_mv\":%d,"
                       "\"current_ma\":%d,"
                       "\"temperature_c\":%d,"
                       "\"charging\":%s,"
                       "\"charge_status\":\"%s\"}",
                       level,
                       voltage_mv,
                       current_ma,
                       temperature_c,
                       charging ? "true" : "false",
                       chargeStatusStr) };

    buildTopic(topic, sizeof(topic), M_SUFFIX_BATTERY);

    LOG_INF("Sending battery status: %s", message);

    if (!m_mqtt.Publish(topic, message, len, false)) {
      LOG_ERR("Failed to publish battery status!");
      return false;
    }

    return true;
  }

  bool App::sendConfigStatus()
  {
    char topic[64];
    char message[320];

    bool enabled = isEnabled();

    int len { snprintf(message, sizeof(message),
                       "{\"enabled\":%s,"
                       "\"provisioning\":%s,"
                       "\"poll_interval\":%u,"
                       "\"mail_window\":%u,"
                       "\"activity_threshold\":%u,"
                       "\"activity_time\":%u,"
                       "\"inactivity_threshold\":%u,"
                       "\"inactivity_time\":%u,"
                       "\"max_buffered_events\":%u,"
                       "\"buffered_events\":%u}",
                       enabled ? "true" : "false",
                       enabled ? "false" : "true",
                       getPollInterval(),
                       m_config.mailWindowSecs,
                       m_config.activityThresholdMg,
                       m_config.activityTime,
                       m_config.inactivityThresholdMg,
                       m_config.inactivityTime,
                       getMaxBufferedEvents(),
                       getBufferedEventCount()) };

    buildTopic(topic, sizeof(topic), M_SUFFIX_STATUS);

    LOG_INF("Sending config status: %s", message);

    if (!m_mqtt.Publish(topic, message, len, false)) {
      LOG_ERR("Failed to publish config status!");
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

    // Subscribe to commands topic.
    char topic[64];
    buildTopic(topic, sizeof(topic), M_SUFFIX_COMMANDS);
    int result = m_mqtt.Subscribe(topic);
    if (result < 0) {
      LOG_ERR("Failed to subscribe to commands topic: %d", result);
    } else {
      LOG_INF("Subscribed to: %s", topic);
    }
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
    // Ignore empty messages (e.g., cleared retained messages).
    if (length == 0 || message == nullptr || message[0] == '\0') {
      return MqttCommand::UNKNOWN;
    }

    // Check for reset first (takes priority).
    if (strstr(message, "\"reset_config\"")) {
      return MqttCommand::RESET_CONFIG;
    }

    // Timer configuration.
    if (strstr(message, "\"mail_window\"")) {
      return MqttCommand::SET_MAIL_WINDOW;
    }

    // ADXL367 configuration.
    if (strstr(message, "\"activity_threshold\"")) {
      return MqttCommand::SET_ACTIVITY_THRESHOLD;
    }
    if (strstr(message, "\"activity_time\"")) {
      return MqttCommand::SET_ACTIVITY_TIME;
    }
    if (strstr(message, "\"inactivity_threshold\"")) {
      return MqttCommand::SET_INACTIVITY_THRESHOLD;
    }
    if (strstr(message, "\"inactivity_time\"")) {
      return MqttCommand::SET_INACTIVITY_TIME;
    }

    // Event buffering configuration.
    if (strstr(message, "\"max_buffered_events\"")) {
      return MqttCommand::SET_MAX_BUFFERED_EVENTS;
    }

    // Other commands.
    if (strstr(message, "\"status_request\"")) {
      return MqttCommand::REQUEST_STATUS;
    }
    if (strstr(message, "\"firmware_update\"")) {
      return MqttCommand::FIRMWARE_UPDATE;
    }
    if (strstr(message, "\"reset_device\"")) {
      return MqttCommand::DEVICE_RESET;
    }

    // Provisioning mode commands.
    if (strstr(message, "\"enable\"")) {
      return MqttCommand::ENABLE;
    }
    if (strstr(message, "\"disable\"")) {
      return MqttCommand::DISABLE;
    }
    if (strstr(message, "\"poll_interval\"")) {
      return MqttCommand::SET_POLL_INTERVAL;
    }

    return MqttCommand::UNKNOWN;
  }

  void App::executeCommand(MqttCommand cmd, const char* message, size_t length)
  {
    int value;
    bool configChanged { false };

    switch (cmd) {
      case MqttCommand::RESET_CONFIG:
        LOG_INF("Resetting configuration to defaults...");
        m_config.setDefaults();
        configChanged = true;
        LOG_INF("Configuration reset: mail_window=%u, act_thresh=%u, act_time=%u, inact_thresh=%u, inact_time=%u",
                m_config.mailWindowSecs, m_config.activityThresholdMg, m_config.activityTime,
                m_config.inactivityThresholdMg, m_config.inactivityTime);
        break;

      case MqttCommand::SET_MAIL_WINDOW:
        value = extractIntValue(message, "mail_window");
        if (value > 0 && value <= 86400) {  // Max 24 hours.
          m_config.mailWindowSecs = static_cast<uint32_t>(value);
          LOG_INF("Mail window set to %d seconds.", value);
        } else {
          LOG_WRN("Invalid mail_window value: %d (must be 1-86400).", value);
        }
        break;

      case MqttCommand::SET_ACTIVITY_THRESHOLD:
        value = extractIntValue(message, "activity_threshold");
        if (value > 0 && value <= 8000) {  // Max 8g in mg.
          m_config.activityThresholdMg = static_cast<uint16_t>(value);
          configChanged = true;
          LOG_INF("Activity threshold set to %d mg.", value);
        } else {
          LOG_WRN("Invalid activity_threshold value: %d (must be 1-8000 mg).", value);
        }
        break;

      case MqttCommand::SET_ACTIVITY_TIME:
        value = extractIntValue(message, "activity_time");
        if (value > 0 && value <= 255) {
          m_config.activityTime = static_cast<uint8_t>(value);
          configChanged = true;
          LOG_INF("Activity time set to %d samples.", value);
        } else {
          LOG_WRN("Invalid activity_time value: %d (must be 1-255).", value);
        }
        break;

      case MqttCommand::SET_INACTIVITY_THRESHOLD:
        value = extractIntValue(message, "inactivity_threshold");
        if (value > 0 && value <= 8000) {  // Max 8g in mg.
          m_config.inactivityThresholdMg = static_cast<uint16_t>(value);
          configChanged = true;
          LOG_INF("Inactivity threshold set to %d mg.", value);
        } else {
          LOG_WRN("Invalid inactivity_threshold value: %d (must be 1-8000 mg).", value);
        }
        break;

      case MqttCommand::SET_INACTIVITY_TIME:
        value = extractIntValue(message, "inactivity_time");
        if (value > 0 && value <= 255) {
          m_config.inactivityTime = static_cast<uint8_t>(value);
          configChanged = true;
          LOG_INF("Inactivity time set to %d samples.", value);
        } else {
          LOG_WRN("Invalid inactivity_time value: %d (must be 1-255).", value);
        }
        break;

      case MqttCommand::SET_MAX_BUFFERED_EVENTS:
        value = extractIntValue(message, "max_buffered_events");
        if (value >= 1 && value <= static_cast<int>(BUFFER_HARDWARE_MAX)) {
          setMaxBufferedEvents(static_cast<uint8_t>(value));
          LOG_INF("Max buffered events set to %d.", value);
        } else {
          LOG_WRN("Invalid max_buffered_events value: %d (must be 1-%d).",
                  value, BUFFER_HARDWARE_MAX);
        }
        break;

      case MqttCommand::REQUEST_STATUS:
        LOG_INF("Status request received.");
        sendConfigStatus();
        break;

      case MqttCommand::FIRMWARE_UPDATE:
        LOG_INF("Firmware update requested - not implemented yet.");
        break;

      case MqttCommand::DEVICE_RESET:
        LOG_INF("Device reset requested.");
        // Clear the retained command by publishing empty message.
        {
          char cmdTopic[M_MQTT_TOPIC_LENGTH];
          buildTopic(cmdTopic, sizeof(cmdTopic), M_SUFFIX_COMMANDS);
          LOG_INF("Clearing retained reset command...");
          m_mqtt.Publish(cmdTopic, "", 0, true);  // Empty retained message clears it.
          k_msleep(500);  // Allow time for publish to complete.
        }
        // Disable device so it returns to provisioning mode after reset.
        setEnabled(false);
        executeDeviceReset();
        // Does not return.
        break;

      case MqttCommand::ENABLE:
        LOG_INF("Enable command received.");
        // Clear the retained command.
        {
          char cmdTopic[M_MQTT_TOPIC_LENGTH];
          buildTopic(cmdTopic, sizeof(cmdTopic), M_SUFFIX_COMMANDS);
          LOG_INF("Clearing retained enable command...");
          m_mqtt.Publish(cmdTopic, "", 0, true);
          k_msleep(500);
        }
        setEnabled(true);
        break;

      case MqttCommand::DISABLE:
        LOG_INF("Disable command received.");
        // Clear the retained command.
        {
          char cmdTopic[M_MQTT_TOPIC_LENGTH];
          buildTopic(cmdTopic, sizeof(cmdTopic), M_SUFFIX_COMMANDS);
          LOG_INF("Clearing retained disable command...");
          m_mqtt.Publish(cmdTopic, "", 0, true);
          k_msleep(500);
        }
        setEnabled(false);
        break;

      case MqttCommand::SET_POLL_INTERVAL:
        value = extractIntValue(message, "poll_interval");
        if (value >= M_MIN_POLL_INTERVAL && value <= M_MAX_POLL_INTERVAL) {
          setPollInterval(static_cast<uint16_t>(value));
          LOG_INF("Poll interval set to %d seconds.", value);
        } else {
          LOG_WRN("Invalid poll_interval value: %d (must be %d-%d).",
                  value, M_MIN_POLL_INTERVAL, M_MAX_POLL_INTERVAL);
        }
        break;

      default:
        LOG_WRN("Unknown command received.");
        break;
    }

    // Reconfigure ADXL367 if parameters changed.
    if (configChanged) {
      LOG_INF("Reconfiguring ADXL367 with new parameters...");
      int result = configureMotionSensor();
      if (result < 0) {
        LOG_ERR("Failed to reconfigure ADXL367: %d", result);
      } else {
        LOG_INF("ADXL367 reconfigured successfully.");
      }
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

    // Device is in Standby after Init() soft reset.
    // All register changes (0x00-0x2D) must be made in Standby per datasheet.

    // =========================================================================
    // Loop mode startup sequence (from datasheet):
    // 1. Configure dummy thresholds to force activity→inactivity cycle.
    // 2. Enter measurement + autosleep.
    // 3. Wait for AWAKE to clear.
    // 4. Reconfigure with real thresholds (0x20-0x26 only, NOT 0x27).
    // =========================================================================

    // Step 1: Dummy config — act threshold below noise, inact threshold > 1g.
    LOG_INF("Loop mode init: setting dummy thresholds...");
    Adxl367::ActivityConfig dummyConfig {
      .activityMode = Adxl367::ActivityMode::Referenced,
      .inactivityMode = Adxl367::ActivityMode::Referenced,
      .linkLoop = Adxl367::LinkLoopMode::Loop,
      .activityThreshold = 1,       // 1mg — below noise, triggers immediately.
      .activityTime = 0,            // No delay.
      .inactivityThreshold = 8000,  // Max range — triggers immediately when still.
      .inactivityTime = 0           // No delay.
    };

    result = m_motion.ConfigureActivity(dummyConfig);
    if (result < 0) { return result; }

    // FIFO (0x28-0x29) - stream mode, XYZ channels.
    result = m_motion.ConfigureFifo(Adxl367::FifoMode::Stream);
    if (result < 0) { return result; }

    // Interrupt mapping (0x2A-0x2B).
    result = m_motion.ConfigureInterrupt(Adxl367::IntPin::Int1, true, false);
    if (result < 0) { return result; }

    // Filter control (0x2C) - range and ODR.
    result = m_motion.SetRange(Adxl367::Range::Range2g);
    if (result < 0) { return result; }

    result = m_motion.SetOdr(Adxl367::ODR::Hz50);
    if (result < 0) { return result; }

    // Wake-up rate (TIMER_CTL 0x39) — 12.5 SPS for fastest FIFO capture.
    result = m_motion.SetWakeupRate(Adxl367::WakeupRate::Rate12Sps);
    if (result < 0) { return result; }

    // Step 2: Enter measurement mode with autosleep (POWER_CTL = 0x06).
    result = m_motion.EnableMeasurementAutosleep();
    if (result < 0) { return result; }

    // Step 3: Wait for AWAKE to clear (~100ms + 1/ODR per datasheet).
    LOG_INF("Waiting for AWAKE to clear (loop mode init)...");
    {
      constexpr uint32_t M_INIT_TIMEOUT_MS { 2000 };
      constexpr uint32_t M_INIT_POLL_MS { 10 };
      uint32_t elapsed { 0 };

      while (m_motion.IsAwake() && (elapsed < M_INIT_TIMEOUT_MS)) {
        k_msleep(M_INIT_POLL_MS);
        elapsed += M_INIT_POLL_MS;
      }

      if (m_motion.IsAwake()) {
        LOG_ERR("AWAKE did not clear during loop mode init (timeout %u ms)!", M_INIT_TIMEOUT_MS);
        return -ETIMEDOUT;
      }

      LOG_INF("AWAKE cleared after %u ms — loop mode initialised.", elapsed);
    }

    // Step 4: Reconfigure with real thresholds (0x20-0x26 only, NOT 0x27).
    LOG_INF("Reconfiguring real thresholds...");

    result = m_motion.SetActivityThreshold(m_config.activityThresholdMg);
    if (result < 0) { return result; }

    result = m_motion.SetInactivityThreshold(m_config.inactivityThresholdMg);
    if (result < 0) { return result; }

    result = m_motion.SetInactivityTime(m_config.inactivityTime);
    if (result < 0) { return result; }

    LOG_INF("ADXL367 config: act=%umg/%u, inact=%umg/%u",
            m_config.activityThresholdMg, m_config.activityTime,
            m_config.inactivityThresholdMg, m_config.inactivityTime);

    LOG_INF("ADXL367 configured: loop mode + autosleep, AWAKE→INT1.");
    return 0;
  }

  // ========== Power Management ==========

  void App::configureWakeSources()
  {
    LOG_INF("Configuring wake sources for System OFF...");

    // Clear any pending latches.
    nrf_gpio_pin_latch_clear(PIN_ACCEL_INT);
    nrf_gpio_pin_latch_clear(PIN_PMIC_INT);

    // Read ADXL367 status to clear any pending interrupt event flags.
    Adxl367::Status status;
    m_motion.ReadStatus(status);
    LOG_INF("ADXL367 status: AWAKE=%d", status.awake);

    // Wait for ADXL367 to return to inactive state (AWAKE=0).
    // This is critical: the GPIO latch only captures rising edges.
    // If we enter System OFF while AWAKE=1, and it goes low during boot,
    // no rising edge occurs and the latch won't be set on wake.
    if (status.awake) {
      LOG_INF("Waiting for ADXL367 to return to inactive state...");

      constexpr uint32_t POLL_INTERVAL_MS { 100 };
      constexpr uint32_t TIMEOUT_MS { 5000 };  // 5 second timeout.
      uint32_t elapsed { 0 };

      while (m_motion.IsAwake() && (elapsed < TIMEOUT_MS)) {
        k_msleep(POLL_INTERVAL_MS);
        elapsed += POLL_INTERVAL_MS;
      }

      if (elapsed >= TIMEOUT_MS) {
        LOG_WRN("Timeout waiting for AWAKE to clear - proceeding anyway.");
      } else {
        LOG_INF("ADXL367 returned to inactive state after %u ms.", elapsed);
      }
    }

    // Small delay for INT1 to settle after AWAKE clears.
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
    // Only shutdown if network was initialised (CLOSE events, heartbeat, etc.)
    if (m_networkInitialised) {
      LOG_INF("Shutting down modem...");
      m_modem.Disconnect();
      LOG_INF("Modem shutdown complete.");
    }
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

  void App::executeDeviceReset()
  {
    LOG_INF("════════════════════════════════════════");
    LOG_INF("DEVICE RESET REQUESTED");
    LOG_INF("════════════════════════════════════════");

    // Check if timer is running (mid-cycle).
    bool timerExpired = m_pmic.TimerIsExpired();

    if (!timerExpired) {
      LOG_INF("Timer running - waiting for expiry before reset...");
      LOG_INF("Maximum wait: %u seconds (mail window)", m_config.mailWindowSecs);

      // Poll for timer expiry with mail_window as absolute timeout.
      constexpr uint32_t POLL_INTERVAL_MS { 1000 };
      uint32_t elapsed_ms { 0 };
      uint32_t timeout_ms { m_config.mailWindowSecs * 1000 };

      while (!m_pmic.TimerIsExpired() && elapsed_ms < timeout_ms) {
        k_msleep(POLL_INTERVAL_MS);
        elapsed_ms += POLL_INTERVAL_MS;

        // Log progress every 30 seconds.
        if ((elapsed_ms % 30000) == 0) {
          LOG_INF("Waiting for timer... %u/%u seconds",
                  elapsed_ms / 1000, m_config.mailWindowSecs);
        }
      }

      if (elapsed_ms >= timeout_ms) {
        LOG_WRN("Timeout waiting for timer - resetting anyway.");
      } else {
        LOG_INF("Timer expired after %u seconds.", elapsed_ms / 1000);
      }
    } else {
      LOG_INF("Timer already expired - resetting immediately.");
    }

    // Shutdown modem cleanly before reset.
    LOG_INF("Shutting down modem before reset...");
    shutdownModem();

    LOG_INF("Executing system reset...");
    LOG_INF("════════════════════════════════════════");

    // Small delay to allow logs to flush.
    k_msleep(100);

    // Perform system reset - this will not return.
    sys_reboot(SYS_REBOOT_COLD);

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
}
