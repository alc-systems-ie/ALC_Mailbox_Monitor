#pragma once

/**
 * @file app.hpp
 * @brief ALC Mailbox Monitor Application.
 *
 * Simplified state machine for detecting mail delivery using nPM1300 GP Timer:
 *
 * Logic:
 * 1. Device sleeps in System OFF (nA power consumption).
 * 2. Motion detected → ADXL367 AWAKE signal wakes device.
 * 3. Check if nPM1300 timer is running:
 *    - If timer NOT running: Start 4-minute timer → OPEN event → sleep
 *    - If timer IS running: Stop timer → CLOSE event → send MQTT → sleep
 * 4. Return to System OFF.
 *
 * The nPM1300 timer persists across MCU System OFF, eliminating the need
 * for retained RAM or settings-based time persistence.
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
#include "led.hpp"
#include "modem.hpp"
#include "mqtt.hpp"
#include "npm1300.hpp"

namespace alc
{
  // ========== Configuration Constants ==========

  // Timing defaults (configurable via MQTT).
  constexpr uint32_t M_MAIL_WINDOW_SECS { 240 };        // 4 minutes - defines open/close cycle.
  constexpr uint32_t M_HEARTBEAT_HOUR { 3 };            // 3am for nightly heartbeat.

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
    REQUEST_STATUS,
    FIRMWARE_UPDATE,
    UNKNOWN
  };

  // ========== Runtime Configuration ==========

  struct MailboxConfig {
    uint32_t mailWindowSecs;

    void setDefaults() {
      mailWindowSecs = M_MAIL_WINDOW_SECS;
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
       */
      void Start(WakeSource wake);

      // MQTT callbacks.
      void OnMqttConnected();
      void OnMqttDisconnected();
      void OnMqttMessageReceived(const char* message, size_t length);

    private:
      // ========== Wake Handling ==========

      /**
       * @brief Handle motion wake - core state machine logic.
       *
       * Uses nPM1300 timer to determine event type:
       * - Timer not running → OPEN event → start timer
       * - Timer running → CLOSE event → stop timer, send MQTT
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

      // ========== Hardware Initialisation ==========

      /**
       * @brief Initialise all hardware subsystems.
       * @return true on success.
       */
      bool initHardware();

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
       * @param ownerIntervened Whether owner signalled (placeholder for future).
       */
      bool sendMailDeliveredEvent(bool ownerIntervened);

      /**
       * @brief Send heartbeat / status report.
       */
      bool sendHeartbeat();

      /**
       * @brief Send battery status.
       */
      bool sendBatteryStatus();

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

      Led m_led;
      Modem m_modem;
      Adxl367 m_motion;
      MqttClient m_mqtt;
      Npm1300 m_pmic;

      // ========== Runtime State ==========

      MailboxConfig m_config;

      // Boot counter for debugging.
      uint32_t m_boot_count;

      // ========== Singleton ==========

      static App* s_instance;
  };
}
