#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/npm13xx_charger.h>
#include <zephyr/drivers/mfd/npm13xx.h>
#include <zephyr/drivers/gpio.h>

namespace alc 
{

  /**
   * @brief nPM1300 Power Management IC Driver.
   * 
   * Provides a C++ interface to the Nordic nPM1300 PMIC, including:
   * - Battery voltage, current, and temperature sensing.
   * - Charging status monitoring.
   * - VBUS connection detection.
   * - Event callbacks for power events.
   * - General Purpose Timer for timed wakeups.
   * - BUCK regulator control.
   */

  class Npm1300 
  {
    public:
      /**
       * @brief GPIO selection for control.
       */
      enum class GpioPin : uint8_t {
        Gpio0 = 0,
        Gpio1 = 1,
        Gpio2 = 2,
        Gpio3 = 3,
        Gpio4 = 4,
        NotUsed = 7     ///< GPIO control disabled.
      };

      /**
       * @brief GPIO control configuration.
       */
      struct GpioControl {
        GpioPin enablePin;      ///< GPIO pin for enable control.
        bool enableInverted;    ///< Invert enable pin polarity.
        GpioPin voltagePin;     ///< GPIO pin for voltage mode selection.
        bool voltageInverted;   ///< Invert voltage pin polarity.
        GpioPin pwmPin;         ///< GPIO pin for PWM mode control.
        bool pwmInverted;       ///< Invert PWM pin polarity.
      };

      /**
       * @brief BUCK identifier.
       */
      enum class BuckId : uint8_t {
        Buck1 = 1,
        Buck2 = 2
      };

      /**
       * @brief Power mode selection.
       */
      enum class BuckPowerMode : uint8_t {
        Auto = 0,       ///< Automatic PFM/PWM switching (best efficiency).
        ForcePwm = 1    ///< Force PWM mode (low ripple, higher quiescent current).
      };

      /**
       * @brief Control source selection.
       */
      enum class BuckControlSource : uint8_t {
        Register = 0,   ///< Controlled by register writes.
        Gpio = 1        ///< Controlled by GPIO pin.
      };

      /**
       * @brief Voltage control mode.
       */
      enum class BuckVoltageMode : uint8_t {
        Normal = 0,     ///< Use normal voltage setpoint.
        Retention = 1   ///< Use retention voltage setpoint (low power mode).
      };

      /**
       * @brief BUCK status information.
       */
      struct BuckStatus {
        bool powered;               ///< BUCK is powered on.
        bool powerGood;             ///< BUCK output is in regulation.
        BuckPowerMode currentMode;  ///< Current operating mode (PWM/Auto).
        uint16_t targetVoltage;     ///< Target output voltage in mV.
      };

      /**
       * @brief Complete BUCK configuration
       */
      struct BuckConfiguration {
        uint16_t normalVoltage;           ///< Normal mode voltage (1000-3300 mV).
        uint16_t retentionVoltage;        ///< Retention mode voltage (1000-3300 mV).
        BuckPowerMode powerMode;          ///< PWM/Auto mode.
        bool pullDownEnabled;             ///< Enable pull-down for fast discharge.
        BuckControlSource voltageControl; ///< Voltage controlled by register or GPIO.
        BuckControlSource enableControl;  ///< Enable controlled by register or GPIO.
      };

      /**
       * @brief Charging status enumeration.
       */
      enum class ChargeStatus {
        Idle,
        Trickle,
        ConstantCurrent,
        ConstantVoltage,
        Complete
      };

      /**
       * @brief Timer mode enumeration.
       * 
       * Selects the operating mode for the nPM1300's multi-function timer.
       * See nPM1300 datasheet section 7.3.6.5 TIMERCONFIG.
       */
      enum class TimerMode : uint8_t {
        BootMonitor         = 0,  ///< Boot monitor mode - power cycles if no TWI traffic.
        WatchdogWarning     = 1,  ///< Watchdog with warning interrupt before reset.
        WatchdogReset       = 2,  ///< Watchdog with immediate reset on timeout.
        GeneralPurpose      = 3,  ///< General purpose timer - fires event on expiry.
        Wakeup              = 4   ///< Wakeup timer - exits hibernate mode on expiry.
      };

      /**
       * @brief Timer prescaler enumeration.
       * 
       * Selects the timer tick period. The nPM1300 timer uses a 64 Hz base clock.
       * See nPM1300 datasheet section 7.3.6.5 TIMERCONFIG and Table 31.
       */
      enum class TimerPrescaler : uint8_t {
        Slow = 0,   ///< 16 ms per tick. Max duration ~3 days.
        Fast = 1    ///< 2 ms per tick. Max duration ~9 hours.
      };

      /**
       * @brief Sensor data structure.
       */
      struct SensorData {
        float voltage;      // Battery voltage in volts.
        float current;      // Battery current in amps (positive = charging).
        float temperature;  // Battery temperature in celsius.
        ChargeStatus chargeStatus;
      };

      /**
       * @brief VBUS event callback type.
       * @param connected True if VBUS connected, false if disconnected.
       */
      using VbusCallback = void (*)(bool connected, void* userData);

      /**
       * @brief Constructor.
       * @param pmic_device Pointer to PMIC device (typically DEVICE_DT_GET(DT_NODELABEL(pmic_main))).
       * @param charger_device Pointer to charger device (typically DEVICE_DT_GET(DT_NODELABEL(npm1300_charger))).
       */
      Npm1300(const struct device* pmicDevice, const struct device* chargerDevice);

      /**
       * @brief Initialize the PMIC.
       * @return 0 on success, negative error code on failure.
       */
      int Init();

      /**
       * @brief Read all sensor values.
       * @param data Reference to SensorData structure to populate.
       * @return 0 on success, negative error code on failure.
       */
      int ReadSensors(SensorData& data);

      /**
       * @brief Get current VBUS connection status.
       * @return True if VBUS is connected.
       */
      bool IsVbusConnected();

      /**
       * @brief Register callback for VBUS events.
       * @param callback Function to call on VBUS events.
       * @param user_data User data pointer passed to callback.
       * @return 0 on success, negative error code on failure.
       */
      int RegisterVbusCallback(VbusCallback callback, void* userData);

      /**
       * @brief Get maximum charge current setting.
       * @return Maximum charge current in amps.
       */
      float GetMaxChargeCurrent();

      /**
       * @brief Get termination charge current (typically 10% of max).
       * @return Termination charge current in amps.
       */
      float GetTermChargeCurrent();

      // =========================================================================
      // General Purpose Timer
      // =========================================================================
      //
      // The nPM1300 GP Timer provides timing that persists across MCU System OFF.
      // This is ideal for mailbox monitor timing where we need to detect if two
      // motion events occur within a time window (e.g., 4 minutes).
      //
      // ╔═══════════════════════════════════════════════════════════════════════╗
      // ║  NOTE: TIMERSTATUS Register Limitation                                ║
      // ║                                                                       ║
      // ║  The TIMERSTATUS register does not reliably indicate running state.   ║
      // ║  Use TimerIsExpired() instead for reliable state detection:           ║
      // ║                                                                       ║
      // ║    TimerIsExpired() = TRUE  → Timer expired or never started          ║
      // ║    TimerIsExpired() = FALSE → Timer is running                        ║
      // ║                                                                       ║
      // ║  The expired event flag (EVENTSSHPHLDSET Bit3) persists across        ║
      // ║  System OFF and reliably indicates timer state.                       ║
      // ╚═══════════════════════════════════════════════════════════════════════╝
      //
      // Typical usage:
      //   1. TimerConfigure() - Set mode and prescaler (once at init)
      //   2. TimerSetDuration() - Set countdown duration
      //   3. TimerStart() - Begin countdown
      //   4. TimerIsExpired() - Check if timer has fired (RECOMMENDED)
      //   5. TimerClearEvent() - Acknowledge the event
      //   6. TimerStop() - Halt the timer if needed
      //

      /**
       * @brief Configure the timer mode and prescaler.
       * 
       * Must be called before starting the timer. Typically only needs to be
       * called once during initialisation.
       * 
       * @param mode Timer operating mode (typically GeneralPurpose).
       * @param prescaler Tick rate (Slow=16ms/tick, Fast=2ms/tick).
       * @return 0 on success, negative error code on failure.
       */
      int TimerConfigure(TimerMode mode, TimerPrescaler prescaler);

      /**
       * @brief Set the timer duration in seconds.
       * 
       * Converts seconds to timer ticks based on current prescaler and loads
       * the value into the timer registers.
       * 
       * @param durationSecs Timer period in seconds.
       * @return 0 on success, negative error code on failure.
       * 
       * @note Maximum duration depends on prescaler:
       *       - Slow (16ms): ~3 days (268,435 seconds)
       *       - Fast (2ms):  ~9 hours (33,554 seconds)
       */
      int TimerSetDuration(uint32_t durationSecs);

      /**
       * @brief Start the timer countdown.
       * 
       * Timer must be configured and duration set before calling.
       * 
       * @return 0 on success, negative error code on failure.
       */
      int TimerStart();

      /**
       * @brief Stop the timer.
       * 
       * Halts the countdown. Timer can be restarted with TimerStart().
       * 
       * @return 0 on success, negative error code on failure.
       */
      int TimerStop();

      /**
       * @brief Check if the timer is currently running.
       * 
       * Reads TIMERSTATUS register to determine timer state.
       * 
       * @warning UNRELIABLE: TIMERSTATUS does not reliably indicate running state.
       *          Use TimerIsExpired() instead for reliable state detection:
       *          - TimerIsExpired() = FALSE means timer is running
       *          - TimerIsExpired() = TRUE means timer has expired or was never started
       * 
       * @return True if timer appears to be running, false otherwise.
       */
      bool TimerIsRunning();

      /**
       * @brief Check if the timer has expired. (RECOMMENDED for state detection)
       * 
       * Reads EVENTSSHPHLDSET register bit 3 (EVENTWATCHDOGWARN).
       * This bit is set when the GP Timer countdown reaches zero.
       * 
       * This is the RECOMMENDED method for detecting timer state:
       *   - Returns TRUE if timer has expired or was never started
       *   - Returns FALSE if timer is currently running
       * 
       * The expired flag persists across System OFF.
       * 
       * @return True if timer has expired, false if still running.
       */
      bool TimerIsExpired();

      /**
       * @brief Clear the timer expiry event.
       * 
       * Writes to EVENTSSHPHLDCLR register bit 3 to acknowledge the event.
       * Must be called to clear the event flag before the next timer cycle.
       * 
       * @return 0 on success, negative error code on failure.
       */
      int TimerClearEvent();

      /**
       * @brief Configure GPIO as interrupt output for timer events.
       * 
       * Sets up the specified GPIO to output high when the timer expires.
       * This allows the nPM1300 to wake the MCU from System OFF.
       * 
       * @param gpioNum GPIO number (0-4).
       * @return 0 on success, negative error code on failure.
       */
      int TimerConfigureGpioInterrupt(uint8_t gpioNum);

      /**
       * @brief Enable timer event routing to GPIO interrupt output.
       * 
       * Routes the timer expiry event to any GPIO configured as interrupt output.
       * 
       * @return 0 on success, negative error code on failure.
       */
      int TimerEnableInterrupt();

      /**
       * @brief Disable timer event routing to GPIO interrupt output.
       * 
       * @return 0 on success, negative error code on failure.
       */
      int TimerDisableInterrupt();

      // =========================================================================
      // BUCK Regulator Control
      // =========================================================================

      /**
       * @brief Enable or disable BUCK
       * @param buckId BUCK identifier (1 or 2)
       * @param enable True to enable, false to disable
       * @return 0 on success, negative error code on failure
       */
      int BuckSetEnable(BuckId buckId, bool enable);

      /**
       * @brief Set normal mode output voltage
       * @param buckId BUCK identifier (1 or 2)
       * @param voltageMillivolts Voltage in mV (1000-3300, steps of 100mV)
       * @return 0 on success, negative error code on failure
       */
      int BuckSetNormalVoltage(BuckId buckId, uint16_t voltageMillivolts);

      /**
       * @brief Set retention mode output voltage
       * @param buckId BUCK identifier (1 or 2)
       * @param voltageMillivolts Voltage in mV (1000-3300, steps of 100mV)
       * @return 0 on success, negative error code on failure
       */
      int BuckSetRetentionVoltage(BuckId buckId, uint16_t voltageMillivolts);

      /**
       * @brief Set power mode (Auto PFM/PWM or Force PWM)
       * @param buckId BUCK identifier (1 or 2)
       * @param mode Power mode selection
       * @return 0 on success, negative error code on failure
       */
      int BuckSetPowerMode(BuckId buckId, BuckPowerMode mode);

      /**
       * @brief Enable/disable pull-down resistor
       * @param buckId BUCK identifier (1 or 2)
       * @param enable True to enable pull-down (fast discharge)
       * @return 0 on success, negative error code on failure
       */
      int BuckSetPullDown(BuckId buckId, bool enable);

      /**
       * @brief Configure GPIO pin for enable control
       * @param buckId BUCK identifier (1 or 2)
       * @param pin GPIO pin (0-4) or NotUsed
       * @param inverted Invert GPIO polarity
       * @return 0 on success, negative error code on failure
       */
      int BuckSetEnableGpioControl(BuckId buckId, GpioPin pin, bool inverted = false);

      /**
       * @brief Configure GPIO pin for voltage mode control
       * @param buckId BUCK identifier (1 or 2)
       * @param pin GPIO pin (0-4) or NotUsed
       * @param inverted Invert GPIO polarity
       * @return 0 on success, negative error code on failure
       */
      int BuckSetVoltageGpioControl(BuckId buckId, GpioPin pin, bool inverted = false);

      /**
       * @brief Configure GPIO pin for PWM mode control
       * @param buckId BUCK identifier (1 or 2)
       * @param pin GPIO pin (0-4) or NotUsed
       * @param inverted Invert GPIO polarity
       * @return 0 on success, negative error code on failure
       */
      int BuckSetPwmGpioControl(BuckId buckId, GpioPin pin, bool inverted = false);

      /**
       * @brief Configure all GPIO controls at once
       * @param buckId BUCK identifier (1 or 2)
       * @param config Complete GPIO configuration
       * @return 0 on success, negative error code on failure
       */
      int BuckSetGpioControl(BuckId buckId, const GpioControl& config);

      /**
       * @brief Select voltage control source (register or GPIO)
       * @param buckId BUCK identifier (1 or 2)
       * @param source Control source selection
       * @return 0 on success, negative error code on failure
       */
      int BuckSetVoltageControlSource(BuckId buckId, BuckControlSource source);

      /**
       * @brief Get current BUCK status
       * @param buckId BUCK identifier (1 or 2)
       * @param status Reference to status structure to populate
       * @return 0 on success, negative error code on failure
       */
      int BuckGetStatus(BuckId buckId, BuckStatus& status);

      /**
       * @brief Get current target voltage
       * @param buckId BUCK identifier (1 or 2)
       * @param voltageMillivolts Reference to store voltage in mV
       * @return 0 on success, negative error code on failure
       */
      int BuckGetTargetVoltage(BuckId buckId, uint16_t& voltageMillivolts);

      /**
       * @brief Check if BUCK is enabled
       * @param buckId BUCK identifier (1 or 2)
       * @return True if enabled, false if disabled
       */
      bool BuckIsEnabled(BuckId buckId);

      /**
       * @brief Check if BUCK output is good (in regulation)
       * @param buckId BUCK identifier (1 or 2)
       * @return True if power good, false otherwise
       */
      bool BuckIsPowerGood(BuckId buckId);

      /**
       * @brief Apply complete configuration to BUCK
       * @param buckId BUCK identifier (1 or 2)
       * @param config Complete configuration
       * @return 0 on success, negative error code on failure
       */
      int BuckConfigure(BuckId buckId, const BuckConfiguration& config);

      /**
       * @brief Get complete configuration from BUCK
       * @param buckId BUCK identifier (1 or 2)
       * @param config Reference to configuration structure to populate
       * @return 0 on success, negative error code on failure
       */
      int BuckGetConfiguration(BuckId buckId, BuckConfiguration& config);

      /**
       * @brief Reset BUCK to default configuration
       * @param buckId BUCK identifier (1 or 2)
       * @return 0 on success, negative error code on failure
       */
      int BuckResetToDefaults(BuckId buckId);

      /**
       * @brief Print current BUCK configuration (for debugging)
       * @param buckId BUCK identifier (1 or 2)
       */
      void BuckPrintConfiguration(BuckId buckId);

    private:
      const struct device* m_pmic;
      const struct device* m_charger;
      volatile bool m_vbus_connected;
      struct gpio_callback m_event_callback;
      VbusCallback m_user_vbus_callback;
      void* m_user_callback_data;
      static Npm1300* s_instance;

      /// Current timer prescaler (needed for duration calculations).
      TimerPrescaler m_timerPrescaler { TimerPrescaler::Slow };

      /**
       * @brief Internal GPIO event handler.
       */
      static void eventCallbackHandler(const struct device* device, struct gpio_callback* callback, uint32_t pins);

      /**
       * @brief Convert charge status register to enum.
       */
      static ChargeStatus parseChargeStatus(int32_t statusReg);

      /**
       * @brief Convert sensor_value to float.
       */ 
      static float sensorValueToFloat(const struct sensor_value& value);

      // General helper methods.
      uint8_t voltageToRegister(uint16_t millivolts);
      uint16_t registerToVoltage(uint8_t regValue);
      bool validateVoltage(uint16_t millivolts);
      bool validateBuckId(BuckId buckId);
    
      // Register access helpers (use constants from npm1300_const.hpp).
      int writeTask(uint8_t base, uint8_t offset);
      int readRegister(uint8_t base, uint8_t offset, uint8_t& value);
      int writeRegister(uint8_t base, uint8_t offset, uint8_t value);
      int updateRegister(uint8_t base, uint8_t offset, uint8_t value, uint8_t mask);

      // =========================================================================
      // Timer Constants
      // =========================================================================

      /// Milliseconds per tick in Slow prescaler mode.
      static constexpr uint32_t TIMER_SLOW_MS_PER_TICK { 16 };

      /// Milliseconds per tick in Fast prescaler mode.
      static constexpr uint32_t TIMER_FAST_MS_PER_TICK { 2 };

      /// Maximum 24-bit timer value.
      static constexpr uint32_t TIMER_MAX_TICKS { 0xFFFFFF };

      /// Timer event bit in EVENTSSHPHLDSET/CLR registers (bit 3).
      static constexpr uint8_t TIMER_EVENT_BIT { BIT(3) };

      /// GPIO mode value for interrupt output.
      static constexpr uint8_t GPIO_MODE_IRQ { 5 };

      // =========================================================================
      // BUCK Constants
      // =========================================================================
      
      // Task trigger value.
      static constexpr uint8_t TASK_TRIGGER { 0x01 };
      
      // Voltage encoding.
      static constexpr uint16_t VOLT_MIN_MV  { 1000 };
      static constexpr uint16_t VOLT_MAX_MV  { 3300 };
      static constexpr uint16_t VOLT_STEP_MV { 100 };
      static constexpr uint8_t  VOLT_MASK    { 0x1F };  // 5 bits.
      
      // BUCKENCTRL register bits.
      static constexpr uint8_t BUCK1_ENGPISEL_POS  { 0 };
      static constexpr uint8_t BUCK1_ENGPISEL_MASK { 0x07 };
      static constexpr uint8_t BUCK2_ENGPISEL_POS  { 3 };
      static constexpr uint8_t BUCK2_ENGPISEL_MASK { 0x38 };
      static constexpr uint8_t BUCK1_ENGPIINV_BIT  { BIT(6) };
      static constexpr uint8_t BUCK2_ENGPIINV_BIT  { BIT(7) };
      
      // BUCKVRETCTRL register bits.
      static constexpr uint8_t BUCK1_VRETGPISEL_POS  { 0 };
      static constexpr uint8_t BUCK1_VRETGPISEL_MASK { 0x07 };
      static constexpr uint8_t BUCK2_VRETGPISEL_POS  { 3 };
      static constexpr uint8_t BUCK2_VRETGPISEL_MASK { 0x38 };
      static constexpr uint8_t BUCK1_VRETGPIINV_BIT  { BIT(6) };
      static constexpr uint8_t BUCK2_VRETGPIINV_BIT  { BIT(7) };
      
      // BUCKPWMCTRL register bits.
      static constexpr uint8_t BUCK1_PWMGPISEL_POS  { 0 };
      static constexpr uint8_t BUCK1_PWMGPISEL_MASK { 0x07 };
      static constexpr uint8_t BUCK2_PWMGPISEL_POS  { 3 };
      static constexpr uint8_t BUCK2_PWMGPISEL_MASK { 0x38 };
      static constexpr uint8_t BUCK1_PWMGPIINV_BIT  { BIT(6) };
      static constexpr uint8_t BUCK2_PWMGPIINV_BIT  { BIT(7) };
      
      // BUCKSWCTRLSEL register bits.
      static constexpr uint8_t BUCK1_SWCTRLSEL_POS  { 0 };
      static constexpr uint8_t BUCK1_SWCTRLSEL_MASK { 0x03 };
      static constexpr uint8_t BUCK2_SWCTRLSEL_POS  { 2 };
      static constexpr uint8_t BUCK2_SWCTRLSEL_MASK { 0x0C };
      
      // BUCKCTRL0 register bits.
      static constexpr uint8_t BUCK1_AUTOCTRLSEL_BIT { BIT(0) };
      static constexpr uint8_t BUCK2_AUTOCTRLSEL_BIT { BIT(1) };
      static constexpr uint8_t BUCK1_ENPULLDOWN_BIT  { BIT(2) };
      static constexpr uint8_t BUCK2_ENPULLDOWN_BIT  { BIT(3) };
      
      // BUCKSTATUS register bits.
      static constexpr uint8_t BUCK1_MODE_BIT      { BIT(0) };
      static constexpr uint8_t BUCK1_PWRGOOD_BIT   { BIT(1) };
      static constexpr uint8_t BUCK1_PWMOK_BIT     { BIT(2) };
      static constexpr uint8_t BUCK2_MODE_BIT      { BIT(4) };
      static constexpr uint8_t BUCK2_PWRGOOD_BIT   { BIT(5) };
      static constexpr uint8_t BUCK2_PWMOK_BIT     { BIT(6) };
    };
}
