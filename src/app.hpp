#pragma once

/**
 * @file app.hpp
 * @brief ALC Mailbox Monitor Application.
 *
 * Single-wake state machine for detecting mail delivery:
 *
 * Logic:
 * 1. Device sleeps in System OFF (nA power consumption).
 * 2. Motion detected → ADXL367 AWAKE signal wakes device.
 * 3. MCU polls AWAKE until it clears (door closed / device at rest).
 * 4. Send MQTT "mail_delivered" notification.
 * 5. Return to System OFF.
 *
 * ADXL367 uses referenced activity/inactivity in loop mode with autosleep.
 * AWAKE stays HIGH while device is displaced from its rest position and
 * only clears when returned to the home orientation.
 *
 * Active wake sources:
 * - ADXL367 INT1 (P0.11): Motion detection
 *
 * Future wake sources (stubs):
 * - nPM1300 Timer GPIO (P0.02): Nightly heartbeat at 3am
 * - Hall Sensor (TBD): Owner interaction
 *
 * MQTT-configurable parameters:
 * - Mail detection window (default 240 seconds / 4 mins)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include "adxl367.hpp"
#include "modem.hpp"
#include "mqtt.hpp"
#include "npm1300.hpp"
// #include "retained.hpp"

namespace alc
{
  // ========== Configuration Constants ==========

  // Timing defaults (configurable via MQTT).
  constexpr uint32_t M_MAIL_WINDOW_SECS { 240 };        // 4 minutes - defines open/close cycle.
  constexpr uint32_t M_HEARTBEAT_HOUR { 3 };            // 3am for nightly heartbeat.

  // ADXL367 defaults (configurable via MQTT).
  constexpr uint16_t M_ACTIVITY_THRESHOLD_MG { 250 };   // Activity threshold in mg.
  constexpr uint8_t M_ACTIVITY_TIME { 1 };              // Activity time in samples.
  constexpr uint16_t M_INACTIVITY_THRESHOLD_MG { 250 }; // Inactivity threshold in mg (referenced mode).
  constexpr uint8_t M_INACTIVITY_TIME { 10 };           // Inactivity time in samples.

  // Provisioning mode poll interval limits (seconds).
  constexpr uint16_t M_DEFAULT_POLL_INTERVAL { 60 };
  constexpr uint16_t M_MIN_POLL_INTERVAL { 10 };
  constexpr uint16_t M_MAX_POLL_INTERVAL { 300 };

  // MQTT settings.
  constexpr size_t M_MQTT_MESSAGE_LENGTH { 256 };
  constexpr size_t M_MQTT_TOPIC_LENGTH { 128 };
  constexpr size_t M_MQTT_CONNECTION_RETRIES { 100 };

  // Modem timeout (NB-IoT can be slow).
  constexpr int M_MODEM_TIMEOUT_SEC { 90 };

  // LED flash period.
  constexpr uint32_t M_LED_FLASH_PERIOD_MS { 250 };

  // ========== Wake Source Identification ==========

  enum class WakeSource {
    Unknown,
    PowerOn,          // Fresh boot / reset.
    Accelerometer,    // ADXL367 motion detected (P0.11).
    Timer,            // nPM1300 timer (P0.02) - future heartbeat.
    HallSensor        // Owner interaction - future.
  };

  // Free function for wake source string conversion.
  const char* wakeSourceToString(WakeSource source);

  // ========== MQTT Command Types ==========

  enum class MqttCommand {
    SET_MAIL_WINDOW,
    SET_ACTIVITY_THRESHOLD,
    SET_ACTIVITY_TIME,
    SET_INACTIVITY_THRESHOLD,
    SET_INACTIVITY_TIME,
    SET_MAX_BUFFERED_EVENTS,
    RESET_CONFIG,
    REQUEST_STATUS,
    FIRMWARE_UPDATE,
    DEVICE_RESET,
    ENABLE,
    DISABLE,
    SET_POLL_INTERVAL,
    UNKNOWN
  };

  // ========== Runtime Configuration ==========

  struct MailboxConfig {
    // Timer settings.
    uint32_t mailWindowSecs;

    // ADXL367 activity/inactivity settings.
    uint16_t activityThresholdMg;
    uint8_t activityTime;
    uint16_t inactivityThresholdMg;
    uint8_t inactivityTime;

    void setDefaults() {
      mailWindowSecs = M_MAIL_WINDOW_SECS;
      activityThresholdMg = M_ACTIVITY_THRESHOLD_MG;
      activityTime = M_ACTIVITY_TIME;
      inactivityThresholdMg = M_INACTIVITY_THRESHOLD_MG;
      inactivityTime = M_INACTIVITY_TIME;
    }
  };

  // ========== Application Class ==========

  class App
  {
    public:
      App();

      /**
       * @brief Start the application.
       *
       * Initialises hardware, handles the wake event, then enters System OFF.
       * This function does not return.
       *
       * @param wake The wake source (detected in main before App construction).
       * @param awakeAtBoot True if ADXL367 INT1 pin was HIGH at boot (before init).
       */
      void Start(WakeSource wake, bool awakeAtBoot = false);

      // MQTT callbacks.
      void OnMqttConnected();
      void OnMqttDisconnected();
      void OnMqttMessageReceived(const char* message, size_t length);

    private:
      // ========== Wake Handling ==========

      /**
       * @brief Handle motion wake - single-wake mail detection.
       *
       * Polls AWAKE until device returns to rest, then sends mail event.
       * MCU stays awake during the entire open period.
       */
      void handleMotionWake();

      /**
       * @brief Handle timer wake - nightly heartbeat (future).
       */
      void handleTimerWake();

      /**
       * @brief Handle Hall sensor wake - owner interaction (future stub).
       */
      void handleHallSensorWake();

      /**
       * @brief Handle fresh boot / power-on.
       */
      void handleFreshBoot();

      /**
       * @brief Handle provisioning mode when device is disabled.
       *
       * Connects to cloud, sends status, checks for enable command, then
       * sleeps for poll_interval. Does not return until device is enabled.
       */
      void handleProvisioningMode();

      /**
       * @brief Initialise the event buffer subsystem.
       *
       * Loads buffered events from NVS flash.
       */
      void initEventBuffer();

      // ========== Hardware Initialisation ==========

      /**
       * @brief Initialise essential hardware (PMIC, accelerometer).
       *
       * This is the minimal init needed for OPEN events.
       * @return true on success.
       */
      bool initHardware();

      /**
       * @brief Initialise network hardware (modem, MQTT).
       *
       * Called lazily on CLOSE events when network access is needed.
       * @return true on success.
       */
      bool initNetworkHardware();

      // ========== MQTT Operations ==========

      /**
       * @brief Connect to network and MQTT broker.
       */
      bool connectToCloud();

      /**
       * @brief Disconnect from MQTT and network.
       */
      void disconnectFromCloud();

      /**
       * @brief Send mail delivered event.
       * @param timestamp Event timestamp (seconds since boot, TODO: RTC epoch).
       * @param ownerIntervened Whether owner signalled (placeholder for future).
       */
      bool sendMailDeliveredEvent(uint32_t timestamp, bool ownerIntervened);

      /**
       * @brief Send all buffered mail events.
       *
       * Sends oldest events first with owner_intervened=true (to suppress SMS).
       * The most recent event is sent with its actual owner_intervened value.
       *
       * @return true if all events were sent successfully.
       */
      bool sendBufferedEvents();

      /**
       * @brief Send heartbeat / status report.
       */
      bool sendHeartbeat();

      /**
       * @brief Send battery status.
       */
      bool sendBatteryStatus();

      /**
       * @brief Send current configuration status.
       */
      bool sendConfigStatus();

      /**
       * @brief Collect any pending MQTT commands.
       */
      void collectMqttCommands();

      // ========== Command Handling ==========

      MqttCommand parseCommand(const char* message, size_t length);
      void executeCommand(MqttCommand cmd, const char* message, size_t length);
      int extractIntValue(const char* message, const char* key);

      // ========== Sensor Configuration ==========

      /**
       * @brief Configure ADXL367 for motion wake-up.
       */
      int configureMotionSensor();

      // ========== Timer Configuration ==========

      /**
       * @brief Initialise nPM1300 GP Timer for mail window timing.
       */
      int configureMailWindowTimer();

      // ========== Power Management ==========

      /**
       * @brief Configure GPIO pins for System OFF wake.
       */
      void configureWakeSources();

      /**
       * @brief Shutdown modem for lowest power.
       */
      void shutdownModem();

      /**
       * @brief Enter System OFF mode.
       *
       * Device will wake on configured GPIO events.
       * This function does not return.
       */
      void enterSystemOff();

      /**
       * @brief Execute device reset via NVIC_SystemReset().
       *
       * Waits for nPM1300 timer to expire (or mail_window timeout) before
       * resetting, to avoid interrupting an in-progress open/close cycle.
       * This function does not return.
       */
      void executeDeviceReset();

      // ========== Utility ==========

      void buildTopic(char* buffer, size_t size, const char* suffix);

      // ========== Constants ==========

      // Device ID (cccc prefix = Mailbox Monitor series).
      static constexpr const char* M_DEVICE_ID { "ccccddddeeeeffff0000111122221111" };

      // Topic suffixes.
      static constexpr const char* M_SUFFIX_EVENTS { "events" };
      static constexpr const char* M_SUFFIX_STATUS { "status" };
      static constexpr const char* M_SUFFIX_BATTERY { "battery" };
      static constexpr const char* M_SUFFIX_HEARTBEAT { "heartbeat" };
      static constexpr const char* M_SUFFIX_COMMANDS { "commands" };

      // GPIO pins for wake sources.
      static constexpr uint32_t PIN_ACCEL_INT { 11 };   // ADXL367 INT1.
      static constexpr uint32_t PIN_PMIC_INT { 2 };     // nPM1300 GPIO (future timer wake).

      // ========== Hardware Objects ==========

      Modem m_modem;
      Adxl367 m_motion;
      MqttClient m_mqtt;
      Npm1300 m_pmic;

      // ========== Runtime State ==========

      MailboxConfig m_config;

      // Track if network hardware has been initialised.
      bool m_networkInitialised { false };

      // ADXL367 AWAKE state captured before init (motion still ongoing at boot).
      bool m_awakeAtBoot { false };

      // Boot counter for debugging.
      uint32_t m_boot_count;

      // ========== Singleton ==========

      static App* s_instance;
  };
}
