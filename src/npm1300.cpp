#include "npm1300_const.hpp"
#include "npm1300.hpp"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(npm1300, LOG_LEVEL_INF);

namespace alc {

  namespace Cmn = alc::npm1300::common;

  Npm1300* Npm1300::s_instance = nullptr; 

  Npm1300::Npm1300(const struct device* pmicDevice, const struct device* chargerDevice)
      : m_pmic(pmicDevice)
      , m_charger(chargerDevice)
      , m_vbus_connected(false)
      , m_user_vbus_callback(nullptr)
      , m_user_callback_data(nullptr)
  {
    s_instance = this;
  }
  
  int Npm1300::Init()
  {
    constexpr int OK { 0 };

    if (!device_is_ready(m_pmic)) {
      LOG_ERR("PMIC device not ready!");
      return -ENODEV;
    }

    if (!device_is_ready(m_charger)) {
      LOG_ERR("Charger device not ready!");
      return -ENODEV;
    }

    // Read actual VBUS status from VBUSINSTATUS register (base 0x02, offset 0x07).
    // Bit 0 (VBUSINPRESENT): 1 = VBUS connected, 0 = VBUS not connected.
    uint8_t vbusStatus { 0 };
    int result = mfd_npm13xx_reg_read(m_pmic, Cmn::M_VBUSIN_BASE, Cmn::M_VBUSINSTATUS_OFFSET, &vbusStatus);
    if (result < 0) {
      LOG_ERR("Failed to read VBUS status register: %d!", result);
      return result;
    }

    IsVbusConnected();

    LOG_INF("nPM1300 initialised, VBUS: %s.", m_vbus_connected ? "connected" : "disconnected");

    return OK;
  }

  int Npm1300::RegisterVbusCallback(VbusCallback callback, void* userData)
  {
    constexpr int OK { 0 };

    m_user_vbus_callback = callback;
    m_user_callback_data = userData;

    gpio_init_callback(&m_event_callback, eventCallbackHandler, (BIT(NPM13XX_EVENT_VBUS_DETECTED) | BIT(NPM13XX_EVENT_VBUS_REMOVED)));

    int result = mfd_npm13xx_add_callback(m_pmic, &m_event_callback);
    if (result) {
      LOG_ERR("Failed to add PMIC callback: %d!", result);
      return result;
    }

    LOG_DBG("VBUS callback registered");

    return OK;
  }

  bool Npm1300::IsVbusConnected()
  {
    // Register based implementation of VBUS check.
    uint8_t vbusStatus { 0 };
    int result = mfd_npm13xx_reg_read(m_pmic, Cmn::M_VBUSIN_BASE, Cmn::M_VBUSINSTATUS_OFFSET, &vbusStatus);
    if (result < 0) {
      LOG_ERR("Failed to read VBUS status register: %d!", result);
      return false;
    }

    m_vbus_connected = (vbusStatus & Cmn::M_BIT_0_MASK);
    return m_vbus_connected;
  }

  ////////////////////////////////////////////////// 
  // Fuel-Gauge related methods.
  ////////////////////////////////////////////////// 

  int Npm1300::ReadSensors(SensorData& data)
  {
    constexpr int OK { 0 };
    struct sensor_value sensorValue { };

    int result = sensor_sample_fetch(m_charger);
    if (result < 0) {
      LOG_ERR("Failed to fetch sensor samples: %d!", result);
      return result;
    }

    // Read voltage.
    sensor_channel_get(m_charger, SENSOR_CHAN_GAUGE_VOLTAGE, &sensorValue);
    data.voltage = sensorValueToFloat(sensorValue);

    // Read temperature.
    sensor_channel_get(m_charger, SENSOR_CHAN_GAUGE_TEMP, &sensorValue);
    data.temperature = sensorValueToFloat(sensorValue);

    // Read current (Zephyr convention: negative = discharging).
    sensor_channel_get(m_charger, SENSOR_CHAN_GAUGE_AVG_CURRENT, &sensorValue);
    data.current = sensorValueToFloat(sensorValue);

    // Read charge status (cast to standard enum type).
    sensor_channel_get(m_charger, static_cast<sensor_channel>(SENSOR_CHAN_NPM13XX_CHARGER_STATUS), &sensorValue);
    data.chargeStatus = parseChargeStatus(sensorValue.val1);

    return OK;
  }

  float Npm1300::GetMaxChargeCurrent()
  {
    struct sensor_value sensorValue;
    
    int result { sensor_channel_get(m_charger, SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT, &sensorValue) };
    if (result < 0) {
      LOG_ERR("Failed to get max charge current: %d", result);
      return 0.0f;
    }

    return sensorValueToFloat(sensorValue);
  }

  float Npm1300::GetTermChargeCurrent()
  {
    // Termination current is typically 10% of max charge current.
    constexpr float converstionFactor { 10.0f }; // 10.0%.
    return (GetMaxChargeCurrent() / converstionFactor);
  }

  void Npm1300::eventCallbackHandler(const struct device* device, struct gpio_callback* callback, uint32_t pins)
  {
    // Safety check.
    if (!s_instance) { return; }
    
    if ((pins & BIT(NPM13XX_EVENT_VBUS_DETECTED))) 
    {
      LOG_INF("VBUS connected.");
      s_instance->m_vbus_connected = true;
      
      if (s_instance->m_user_vbus_callback) { s_instance->m_user_vbus_callback(true, s_instance->m_user_callback_data); }
    }
    
    if ((pins & BIT(NPM13XX_EVENT_VBUS_REMOVED))) 
    {
      LOG_INF("VBUS removed.");
      s_instance->m_vbus_connected = false;
        
      if (s_instance->m_user_vbus_callback) { s_instance->m_user_vbus_callback(false, s_instance->m_user_callback_data); }
    }
  }

  Npm1300::ChargeStatus Npm1300::parseChargeStatus(int32_t statusReg)
  {
    if ((statusReg & Cmn::M_CHG_STATUS_COMPLETE_MASK)) { return ChargeStatus::Complete; }

    if ((statusReg & Cmn::M_CHG_STATUS_TRICKLE_MASK)) { return ChargeStatus::Trickle; }

    if ((statusReg & Cmn::M_CHG_STATUS_CC_MASK)) { return ChargeStatus::ConstantCurrent; }

    if ((statusReg & Cmn::M_CHG_STATUS_CV_MASK)) { return ChargeStatus::ConstantVoltage; }

    return ChargeStatus::Idle;
  }

  float Npm1300::sensorValueToFloat(const struct sensor_value& sensorValue)
  {
    constexpr float conversionFactor { 1'000'000.0f };

    return (static_cast<float>(sensorValue.val1) + ((static_cast<float>(sensorValue.val2) / conversionFactor)));
  }

  ////////////////////////////////////////////////// 
  // General Purpose Timer methods.
  ////////////////////////////////////////////////// 

  int Npm1300::TimerConfigure(TimerMode mode, TimerPrescaler prescaler)
  {
    constexpr int OK { 0 };

    // Store prescaler for duration calculations.
    m_timerPrescaler = prescaler;

    // Build TIMERCONFIG register value.
    // Bits [2:0] = mode, Bit [3] = prescaler.
    uint8_t configValue = static_cast<uint8_t>(mode) | (static_cast<uint8_t>(prescaler) << 3);

    int result = writeRegister(Cmn::M_TIMER_BASE, Cmn::M_TIMERCONFIG_OFFSET, configValue);
    if (result < 0) {
      LOG_ERR("Failed to configure timer: %d", result);
      return result;
    }

    LOG_INF("Timer configured: mode=%d, prescaler=%s", 
            static_cast<uint8_t>(mode),
            (prescaler == TimerPrescaler::Slow) ? "Slow (16ms)" : "Fast (2ms)");

    return OK;
  }

  int Npm1300::TimerSetDuration(uint32_t durationSecs)
  {
    constexpr int OK { 0 };

    // Calculate ticks based on current prescaler.
    uint32_t msPerTick = (m_timerPrescaler == TimerPrescaler::Slow) 
                         ? TIMER_SLOW_MS_PER_TICK 
                         : TIMER_FAST_MS_PER_TICK;

    uint32_t ticks = (durationSecs * 1000) / msPerTick;

    // Clamp to 24-bit maximum.
    if (ticks > TIMER_MAX_TICKS) {
      LOG_WRN("Timer duration %u secs exceeds maximum, clamping to %u ticks", 
              durationSecs, TIMER_MAX_TICKS);
      ticks = TIMER_MAX_TICKS;
    }

    // Split into 3 bytes (24-bit value).
    uint8_t hiByte  = (ticks >> 16) & 0xFF;
    uint8_t midByte = (ticks >> 8) & 0xFF;
    uint8_t loByte  = ticks & 0xFF;

    LOG_DBG("Timer duration: %u secs = %u ticks (HI=0x%02X, MID=0x%02X, LO=0x%02X)",
            durationSecs, ticks, hiByte, midByte, loByte);

    // Write timer target registers.
    int result = writeRegister(Cmn::M_TIMER_BASE, Cmn::M_TIMERHIBYTE_OFFSET, hiByte);
    if (result < 0) {
      LOG_ERR("Failed to write TIMERHIBYTE: %d", result);
      return result;
    }

    result = writeRegister(Cmn::M_TIMER_BASE, Cmn::M_TIMERMIDBYTE_OFFSET, midByte);
    if (result < 0) {
      LOG_ERR("Failed to write TIMERMIDBYTE: %d", result);
      return result;
    }

    result = writeRegister(Cmn::M_TIMER_BASE, Cmn::M_TIMERLOBYTE_OFFSET, loByte);
    if (result < 0) {
      LOG_ERR("Failed to write TIMERLOBYTE: %d", result);
      return result;
    }

    // Strobe to load the target value into the timer.
    result = writeTask(Cmn::M_TIMER_BASE, Cmn::M_TIMERTARGETSTROBE_OFFSET);
    if (result < 0) {
      LOG_ERR("Failed to strobe timer target: %d", result);
      return result;
    }

    LOG_INF("Timer duration set: %u seconds", durationSecs);

    return OK;
  }

  int Npm1300::TimerStart()
  {
    constexpr int OK { 0 };

    int result = writeTask(Cmn::M_TIMER_BASE, Cmn::M_TIMERSET_OFFSET);
    if (result < 0) {
      LOG_ERR("Failed to start timer: %d", result);
      return result;
    }

    LOG_INF("Timer started");

    return OK;
  }

  int Npm1300::TimerStop()
  {
    constexpr int OK { 0 };

    int result = writeTask(Cmn::M_TIMER_BASE, Cmn::M_TIMERCLR_OFFSET);
    if (result < 0) {
      LOG_ERR("Failed to stop timer: %d", result);
      return result;
    }

    LOG_DBG("Timer stopped");

    return OK;
  }

  bool Npm1300::TimerIsRunning()
  {
    // WARNING: TIMERSTATUS does not reliably indicate running state.
    // Use TimerIsExpired() instead for reliable state detection.
    uint8_t status { 0 };
    int result = readRegister(Cmn::M_TIMER_BASE, Cmn::M_TIMERSTATUS_OFFSET, status);
    if (result < 0) {
      LOG_ERR("Failed to read timer status: %d", result);
      return false;
    }

    // Bit 1 indicates timer is running (UNRELIABLE - always seems to be 1).
    bool running = (status & Cmn::M_BIT_1_MASK) != 0;
    LOG_DBG("Timer status: 0x%02X, running=%d (UNRELIABLE)", status, running);

    return running;
  }

  bool Npm1300::TimerIsExpired()
  {
    uint8_t eventReg { 0 };
    int result = readRegister(Cmn::M_MAIN_BASE, Cmn::M_EVENTSSHPHLDSET_OFFSET, eventReg);
    if (result < 0) {
      LOG_ERR("Failed to read timer event status: %d", result);
      return false;
    }

    // Bit 3 (TIMER_EVENT_BIT = 0x08) indicates timer/watchdog event.
    bool expired = (eventReg & TIMER_EVENT_BIT) != 0;

    if (expired) {
      LOG_DBG("Timer expired (EVENTSSHPHLDSET=0x%02X)", eventReg);
    }

    return expired;
  }

  int Npm1300::TimerClearEvent()
  {
    constexpr int OK { 0 };

    // Write 1 to bit 3 of EVENTSSHPHLDCLR to clear the timer event.
    // This is a W1C (write-1-to-clear) register.
    int result = writeRegister(Cmn::M_MAIN_BASE, Cmn::M_EVENTSSHPHLDCLR_OFFSET, TIMER_EVENT_BIT);
    if (result < 0) {
      LOG_ERR("Failed to clear timer event: %d", result);
      return result;
    }

    LOG_DBG("Timer event cleared");

    return OK;
  }

  void Npm1300::DebugTimerState()
  {
    uint8_t timerStatus { 0 };
    uint8_t eventReg { 0 };
    uint8_t timerHi { 0 }, timerMid { 0 }, timerLo { 0 };

    readRegister(Cmn::M_TIMER_BASE, Cmn::M_TIMERSTATUS_OFFSET, timerStatus);
    readRegister(Cmn::M_MAIN_BASE, Cmn::M_EVENTSSHPHLDSET_OFFSET, eventReg);
    readRegister(Cmn::M_TIMER_BASE, Cmn::M_TIMERHIBYTE_OFFSET, timerHi);
    readRegister(Cmn::M_TIMER_BASE, Cmn::M_TIMERMIDBYTE_OFFSET, timerMid);
    readRegister(Cmn::M_TIMER_BASE, Cmn::M_TIMERLOBYTE_OFFSET, timerLo);

    uint32_t timerValue = (static_cast<uint32_t>(timerHi) << 16) |
                          (static_cast<uint32_t>(timerMid) << 8) |
                          static_cast<uint32_t>(timerLo);

    LOG_INF("Timer debug: STATUS=0x%02X, EVENTS=0x%02X, VALUE=%u (0x%06X)",
            timerStatus, eventReg, timerValue, timerValue);
    LOG_INF("  Expired bit (bit3): %s", (eventReg & TIMER_EVENT_BIT) ? "SET" : "CLEAR");
  }

  int Npm1300::TimerConfigureGpioInterrupt(uint8_t gpioNum)
  {
    constexpr int OK { 0 };

    if (gpioNum > 4) {
      LOG_ERR("Invalid GPIO number: %d (must be 0-4)", gpioNum);
      return -EINVAL;
    }

    // Set GPIO mode to interrupt output (mode 5).
    // GPIOMODE registers are at base 0x06, offset 0x00 + gpioNum.
    uint8_t modeOffset = Cmn::M_GPIOMODE0_OFFSET + gpioNum;

    int result = writeRegister(Cmn::M_GPIOS_BASE, modeOffset, GPIO_MODE_IRQ);
    if (result < 0) {
      LOG_ERR("Failed to configure GPIO%d as IRQ output: %d", gpioNum, result);
      return result;
    }

    // Disable pull-down on the GPIO (offset 0x0F + gpioNum).
    uint8_t pdenOffset = Cmn::M_GPIOPDEN0_OFFSET + gpioNum;
    result = writeRegister(Cmn::M_GPIOS_BASE, pdenOffset, 0x00);
    if (result < 0) {
      LOG_ERR("Failed to disable GPIO%d pull-down: %d", gpioNum, result);
      return result;
    }

    LOG_INF("GPIO%d configured as interrupt output", gpioNum);

    return OK;
  }

  int Npm1300::TimerEnableInterrupt()
  {
    constexpr int OK { 0 };

    // Write 1 to bit 3 of INTENEVENTSSHPHLDSET to enable timer event interrupt.
    // This is a W1S (write-1-to-set) register.
    int result = writeRegister(Cmn::M_MAIN_BASE, Cmn::M_INTENEVENTSSHPHLDSET_OFFSET, TIMER_EVENT_BIT);
    if (result < 0) {
      LOG_ERR("Failed to enable timer interrupt: %d", result);
      return result;
    }

    LOG_INF("Timer interrupt enabled");

    return OK;
  }

  int Npm1300::TimerDisableInterrupt()
  {
    constexpr int OK { 0 };

    // Write 1 to bit 3 of INTENEVENTSSHPHLDCLR to disable timer event interrupt.
    // This is a W1C (write-1-to-clear) register.
    int result = writeRegister(Cmn::M_MAIN_BASE, Cmn::M_INTENEVENTSSHPHLDCLR_OFFSET, TIMER_EVENT_BIT);
    if (result < 0) {
      LOG_ERR("Failed to disable timer interrupt: %d", result);
      return result;
    }

    LOG_INF("Timer interrupt disabled");

    return OK;
  }

  ////////////////////////////////////////////////// 
  // Buck related methods.
  ////////////////////////////////////////////////// 

  // ========== Basic Control ==========

  int Npm1300::BuckSetEnable(BuckId buckId, bool enable)
  {
    constexpr int OK { 0 };

    if (!validateBuckId(buckId)) { return -EINVAL; }
      
    uint8_t offset { };
    if (buckId == BuckId::Buck1) {
      offset = enable ? Cmn::M_BUCK1_ENASET_OFFSET : Cmn::M_BUCK1_ENACLR_OFFSET;
    } else {
      offset = enable ? Cmn::M_BUCK2_ENASET_OFFSET : Cmn::M_BUCK2_ENACLR_OFFSET;
    }
    
    int result { writeTask(Cmn::M_BUCK_BASE, offset) };
    if (result < 0) {
      LOG_ERR("Failed to %s BUCK%d: %d!", enable ? "enable" : "disable", static_cast<uint8_t>(buckId), result);
      return result;
    }
    
    LOG_INF("BUCK%d %s", static_cast<uint8_t>(buckId), enable ? "enabled" : "disabled");

    return OK;
  }

  int Npm1300::BuckSetNormalVoltage(BuckId buckId, uint16_t voltageMillivolts)
  {
    constexpr int OK { 0 };

    if (!validateBuckId(buckId) || !validateVoltage(voltageMillivolts)) { return -EINVAL; }
    
    uint8_t regValue { voltageToRegister(voltageMillivolts) };
    uint8_t offset { ((buckId == BuckId::Buck1) ? Cmn::M_BUCK1_NORMVOUT_OFFSET : Cmn::M_BUCK2_NORMVOUT_OFFSET) };
    
    int result { writeRegister(Cmn::M_BUCK_BASE, offset, regValue) };
    if (result < 0) {
      LOG_ERR("Failed to set BUCK%d normal voltage: %d!", static_cast<uint8_t>(buckId), result);
      return result;
    }
    
    LOG_INF("BUCK%d normal voltage set to %d mV (reg: 0x%02X)", static_cast<uint8_t>(buckId), voltageMillivolts, regValue);

    return OK;
  }

  int Npm1300::BuckSetRetentionVoltage(BuckId buckId, uint16_t voltageMillivolts)
  {
    constexpr int OK { 0 };

    if (!validateBuckId(buckId) || !validateVoltage(voltageMillivolts)) { return -EINVAL; }
    
    uint8_t regValue { voltageToRegister(voltageMillivolts) };
    uint8_t offset { ((buckId == BuckId::Buck1) ? Cmn::M_BUCK1_RETVOUT_OFFSET : Cmn::M_BUCK2_RETVOUT_OFFSET) };
    
    int result { writeRegister(Cmn::M_BUCK_BASE, offset, regValue) };
    if (result < 0) {
      LOG_ERR("Failed to set BUCK%d retention voltage: %d!", static_cast<uint8_t>(buckId), result);
      return result;
    }
    
    LOG_INF("BUCK%d retention voltage set to %d mV (reg: 0x%02X)", static_cast<uint8_t>(buckId), voltageMillivolts, regValue);

    return OK;
  }

  // ========== Power Mode Control ==========

  int Npm1300::BuckSetPowerMode(BuckId buckId, BuckPowerMode mode)
  {
    constexpr int OK { 0 };

    if (!validateBuckId(buckId)) { return -EINVAL; }
    
    uint8_t offset { };
    if (buckId == BuckId::Buck1) {
      offset = (mode == BuckPowerMode::ForcePwm) ? Cmn::M_BUCK1_PWMSET_OFFSET : Cmn::M_BUCK1_PWMCLR_OFFSET;
    } else {
      offset = (mode == BuckPowerMode::ForcePwm) ? Cmn::M_BUCK2_PWMSET_OFFSET : Cmn::M_BUCK2_PWMCLR_OFFSET;
    }
    
    int result { writeTask(Cmn::M_BUCK_BASE, offset) };
    if (result < 0) {
      LOG_ERR("Failed to set BUCK%d power mode: %d!", static_cast<uint8_t>(buckId), result);
      return result;
    }
    
    LOG_DBG("BUCK%d power mode set to %s", static_cast<uint8_t>(buckId), (mode == BuckPowerMode::ForcePwm) ? "Force PWM" : "Auto");

    return OK;
  }

  int Npm1300::BuckSetPullDown(BuckId buckId, bool enable)
  {
    constexpr int OK { 0 };

    if (!validateBuckId(buckId)) { return -EINVAL; }
    
    uint8_t bit = (buckId == BuckId::Buck1) ? BUCK1_ENPULLDOWN_BIT : BUCK2_ENPULLDOWN_BIT;
    uint8_t value = enable ? bit : 0;
    
    int result { updateRegister(Cmn::M_BUCK_BASE, Cmn::M_BUCKCTRL0_OFFSET, value, bit) };
    if (result < 0) {
      LOG_ERR("Failed to set BUCK%d pull-down: %d!", static_cast<uint8_t>(buckId), result);
      return result;
    }
    
    LOG_DBG("BUCK%d pull-down %s", static_cast<uint8_t>(buckId), enable ? "enabled" : "disabled");

    return OK;
  }

  // ========== GPIO Control ==========

  int Npm1300::BuckSetEnableGpioControl(BuckId buckId, GpioPin pin, bool inverted)
  {
    constexpr int OK { 0 };

    if (!validateBuckId(buckId)) { return -EINVAL; }
    
    uint8_t gpiBits, gpiMask, invBit;
    if (buckId == BuckId::Buck1) {
      gpiBits = ((static_cast<uint8_t>(pin) << BUCK1_ENGPISEL_POS) & BUCK1_ENGPISEL_MASK);
      gpiMask = BUCK1_ENGPISEL_MASK | BUCK1_ENGPIINV_BIT;
      invBit = inverted ? BUCK1_ENGPIINV_BIT : 0;
    } else {
      gpiBits = ((static_cast<uint8_t>(pin) << BUCK2_ENGPISEL_POS) & BUCK2_ENGPISEL_MASK);
      gpiMask = BUCK2_ENGPISEL_MASK | BUCK2_ENGPIINV_BIT;
      invBit = inverted ? BUCK2_ENGPIINV_BIT : 0;
    }
    
    int result { updateRegister(Cmn::M_BUCK_BASE, Cmn::M_BUCKENCTRL_OFFSET, gpiBits | invBit, gpiMask) };
    if (result < 0) {
      LOG_ERR("Failed to set BUCK%d enable GPIO control: %d!", static_cast<uint8_t>(buckId), result);
      return result;
    }
    
    LOG_DBG("BUCK%d enable controlled by GPIO%d%s", static_cast<uint8_t>(buckId), static_cast<uint8_t>(pin), inverted ? " (inverted)" : "");

    return OK;
  }

  int Npm1300::BuckSetVoltageGpioControl(BuckId buckId, GpioPin pin, bool inverted)
  {
    constexpr int OK { 0 };

    if (!validateBuckId(buckId)) { return -EINVAL; }
    
    uint8_t gpiBits, gpiMask, invBit;
    if (buckId == BuckId::Buck1) {
      gpiBits = ((static_cast<uint8_t>(pin) << BUCK1_VRETGPISEL_POS) & BUCK1_VRETGPISEL_MASK);
      gpiMask = BUCK1_VRETGPISEL_MASK | BUCK1_VRETGPIINV_BIT;
      invBit = inverted ? BUCK1_VRETGPIINV_BIT : 0;
    } else {
      gpiBits = ((static_cast<uint8_t>(pin) << BUCK2_VRETGPISEL_POS) & BUCK2_VRETGPISEL_MASK);
      gpiMask = BUCK2_VRETGPISEL_MASK | BUCK2_VRETGPIINV_BIT;
      invBit = inverted ? BUCK2_VRETGPIINV_BIT : 0;
    }
    
    int result { updateRegister(Cmn::M_BUCK_BASE, Cmn::M_BUCKVRETCTRL_OFFSET, gpiBits | invBit, gpiMask) };
    if (result < 0) {
      LOG_ERR("Failed to set BUCK%d voltage GPIO control: %d!", static_cast<uint8_t>(buckId), result);
      return result;
    }
    
    LOG_DBG("BUCK%d voltage mode controlled by GPIO%d%s", static_cast<uint8_t>(buckId), static_cast<uint8_t>(pin), inverted ? " (inverted)" : "");

    return OK;
  }

  int Npm1300::BuckSetPwmGpioControl(BuckId buckId, GpioPin pin, bool inverted)
  {
    constexpr int OK { 0 };

    if (!validateBuckId(buckId)) { return -EINVAL; }
    
    uint8_t gpiBits, gpiMask, invBit;
    if (buckId == BuckId::Buck1) {
      gpiBits = ((static_cast<uint8_t>(pin) << BUCK1_PWMGPISEL_POS) & BUCK1_PWMGPISEL_MASK);
      gpiMask = BUCK1_PWMGPISEL_MASK | BUCK1_PWMGPIINV_BIT;
      invBit = inverted ? BUCK1_PWMGPIINV_BIT : 0;
    } else {
      gpiBits = ((static_cast<uint8_t>(pin) << BUCK2_PWMGPISEL_POS) & BUCK2_PWMGPISEL_MASK);
      gpiMask = BUCK2_PWMGPISEL_MASK | BUCK2_PWMGPIINV_BIT;
      invBit = inverted ? BUCK2_PWMGPIINV_BIT : 0;
    }
    
    int result { updateRegister(Cmn::M_BUCK_BASE, Cmn::M_BUCKPWMCTRL_OFFSET, gpiBits | invBit, gpiMask) };
    if (result < 0) {
      LOG_ERR("Failed to set BUCK%d PWM GPIO control: %d!", static_cast<uint8_t>(buckId), result);
      return result;
    }
    
    LOG_DBG("BUCK%d PWM mode controlled by GPIO%d%s", static_cast<uint8_t>(buckId), static_cast<uint8_t>(pin), inverted ? " (inverted)" : "");

    return OK;
  }

  int Npm1300::BuckSetGpioControl(BuckId buckId, const GpioControl& config)
  {
    constexpr int OK { 0 };
    int result;
    
    result = BuckSetEnableGpioControl(buckId, config.enablePin, config.enableInverted);
    if (result < 0) return result;
    
    result = BuckSetVoltageGpioControl(buckId, config.voltagePin, config.voltageInverted);
    if (result < 0) return result;
    
    result = BuckSetPwmGpioControl(buckId, config.pwmPin, config.pwmInverted);
    if (result < 0) return result;
    
    LOG_INF("BUCK%d GPIO control configured", static_cast<uint8_t>(buckId));

    return OK;
  }

  // ========== Control Source Selection ==========

  int Npm1300::BuckSetVoltageControlSource(BuckId buckId, BuckControlSource source)
  {
    constexpr int OK { 0 };

    if (!validateBuckId(buckId)) { return -EINVAL; }
    
    uint8_t sourceBits, sourceMask;
    if (buckId == BuckId::Buck1) {
      sourceBits = ((static_cast<uint8_t>(source) << BUCK1_SWCTRLSEL_POS) & BUCK1_SWCTRLSEL_MASK);
      sourceMask = BUCK1_SWCTRLSEL_MASK;
    } else {
      sourceBits = ((static_cast<uint8_t>(source) << BUCK2_SWCTRLSEL_POS) & BUCK2_SWCTRLSEL_MASK);
      sourceMask = BUCK2_SWCTRLSEL_MASK;
    }
    
    int result { updateRegister(Cmn::M_BUCK_BASE, Cmn::M_BUCKSWCTRLSEL_OFFSET, sourceBits, sourceMask) };
    if (result < 0) {
      LOG_ERR("Failed to set BUCK%d voltage control source: %d!", static_cast<uint8_t>(buckId), result);
      return result;
    }
    
    LOG_DBG("BUCK%d voltage controlled by %s", static_cast<uint8_t>(buckId), (source == BuckControlSource::Register) ? "register" : "GPIO");

    return OK;
  }

  // ========== Status & Monitoring ==========

  int Npm1300::BuckGetStatus(BuckId buckId, BuckStatus& status)
  {
    constexpr int OK { 0 };

    // Safety check.
    if (!validateBuckId(buckId)) { return -EINVAL; }
    
    uint8_t statusReg { };
    int result { readRegister(Cmn::M_BUCK_BASE, Cmn::M_BUCKSTATUS_OFFSET, statusReg) };
    if (result < 0) {
      LOG_ERR("Failed to read BUCK status: %d", result);
      return result;
    }
    
    if (buckId == BuckId::Buck1) {
      status.powered = ((statusReg & BUCK1_MODE_BIT) != 0);
      status.powerGood = ((statusReg & BUCK1_PWRGOOD_BIT) != 0);
      status.currentMode = ((statusReg & BUCK1_PWMOK_BIT) ? BuckPowerMode::ForcePwm : BuckPowerMode::Auto);
    } else {
      status.powered = ((statusReg & BUCK2_MODE_BIT) != 0);
      status.powerGood = ((statusReg & BUCK2_PWRGOOD_BIT) != 0);
      status.currentMode = ((statusReg & BUCK2_PWMOK_BIT) ? BuckPowerMode::ForcePwm : BuckPowerMode::Auto);
    }
    
    // Get target voltage.
    result = BuckGetTargetVoltage(buckId, status.targetVoltage);
    if (result < 0) {
      status.targetVoltage = 0;
    }
    
    return OK;
  }

  int Npm1300::BuckGetTargetVoltage(BuckId buckId, uint16_t& voltageMillivolts)
  {
    constexpr int OK { 0 };

    // Safety check.
    if (!validateBuckId(buckId)) { return -EINVAL; }
    
    uint8_t offset { (buckId == BuckId::Buck1) ? Cmn::M_BUCK1_VOUTSTATUS_OFFSET : Cmn::M_BUCK2_VOUTSTATUS_OFFSET };
    
    uint8_t regValue;
    int result { readRegister(Cmn::M_BUCK_BASE, offset, regValue) };
    if (result < 0) {
      LOG_ERR("Failed to read BUCK%d voltage status: %d!", static_cast<uint8_t>(buckId), result);
      return result;
    }
    
    voltageMillivolts = registerToVoltage(regValue & VOLT_MASK);
    return OK;
  }

  bool Npm1300::BuckIsEnabled(BuckId buckId)
  {
    BuckStatus status;
    if (BuckGetStatus(buckId, status) == 0) {
      return status.powered;
    }
    return false;
  }

  bool Npm1300::BuckIsPowerGood(BuckId buckId)
  {
    BuckStatus status;
    if (BuckGetStatus(buckId, status) == 0) { 
      return status.powerGood; 
    }
    return false;
  }

  // ========== Convenience Functions ==========

  int Npm1300::BuckConfigure(BuckId buckId, const BuckConfiguration& config)
  {
    constexpr int OK { 0 };
    int result;
    
    // Set voltages first (while disabled for safety).
    result = BuckSetNormalVoltage(buckId, config.normalVoltage);
    if (result < 0) return result;
    
    result = BuckSetRetentionVoltage(buckId, config.retentionVoltage);
    if (result < 0) return result;
    
    // Configure power mode.
    result = BuckSetPowerMode(buckId, config.powerMode);
    if (result < 0) return result;
    
    // Configure pull-down.
    result = BuckSetPullDown(buckId, config.pullDownEnabled);
    if (result < 0) return result;
    
    // Configure control sources.
    result = BuckSetVoltageControlSource(buckId, config.voltageControl);
    if (result < 0) return result;
    
    LOG_INF("BUCK%d configured: Normal=%dmV, Ret=%dmV, Mode=%s, PullDown=%s",
            static_cast<uint8_t>(buckId), config.normalVoltage, 
            config.retentionVoltage,
            (config.powerMode == BuckPowerMode::ForcePwm) ? "PWM" : "Auto",
            config.pullDownEnabled ? "ON" : "OFF");
    
    return OK;
  }

  int Npm1300::BuckGetConfiguration(BuckId buckId, BuckConfiguration& config)
  {
    constexpr int OK { 0 };

    // Read normal voltage.
    uint8_t offset = (buckId == BuckId::Buck1) ? Cmn::M_BUCK1_NORMVOUT_OFFSET : Cmn::M_BUCK2_NORMVOUT_OFFSET;
    uint8_t regValue;
    int result = readRegister(Cmn::M_BUCK_BASE, offset, regValue);
    if (result < 0) return result;
    config.normalVoltage = registerToVoltage(regValue & VOLT_MASK);

    // Read retention voltage.
    offset = (buckId == BuckId::Buck1) ? Cmn::M_BUCK1_RETVOUT_OFFSET : Cmn::M_BUCK2_RETVOUT_OFFSET;
    result = readRegister(Cmn::M_BUCK_BASE, offset, regValue);
    if (result < 0) return result;
    config.retentionVoltage = registerToVoltage(regValue & VOLT_MASK);

    // Read power mode from status.
    BuckStatus status;
    result = BuckGetStatus(buckId, status);
    if (result < 0) return result;
    config.powerMode = status.currentMode;

    // Read pull-down state.
    result = readRegister(Cmn::M_BUCK_BASE, Cmn::M_BUCKCTRL0_OFFSET, regValue);
    if (result < 0) return result;
    uint8_t pullDownBit = (buckId == BuckId::Buck1) ? BUCK1_ENPULLDOWN_BIT : BUCK2_ENPULLDOWN_BIT;
    config.pullDownEnabled = (regValue & pullDownBit) != 0;

    // Read control source.
    result = readRegister(Cmn::M_BUCK_BASE, Cmn::M_BUCKSWCTRLSEL_OFFSET, regValue);
    if (result < 0) return result;
    uint8_t ctrlMask = (buckId == BuckId::Buck1) ? BUCK1_SWCTRLSEL_MASK : BUCK2_SWCTRLSEL_MASK;
    uint8_t ctrlPos = (buckId == BuckId::Buck1) ? BUCK1_SWCTRLSEL_POS : BUCK2_SWCTRLSEL_POS;
    config.voltageControl = static_cast<BuckControlSource>((regValue & ctrlMask) >> ctrlPos);

    return OK;
  }

  int Npm1300::BuckResetToDefaults(BuckId buckId)
  {
    constexpr int DELAY { 10 }; // 10ms.

    BuckConfiguration defaultConfig = {
      .normalVoltage = 3300,
      .retentionVoltage = 1800,
      .powerMode = BuckPowerMode::Auto,
      .pullDownEnabled = false,
      .voltageControl = BuckControlSource::Register,
      .enableControl = BuckControlSource::Register
    };
    
    // Disable first.
    BuckSetEnable(buckId, false);
    k_msleep(DELAY);
    
    // Apply defaults.
    return BuckConfigure(buckId, defaultConfig);
  }

  void Npm1300::BuckPrintConfiguration(BuckId buckId)
  {
    BuckStatus status;
    if (BuckGetStatus(buckId, status) != 0) {
      LOG_ERR("Failed to read BUCK%d status!", static_cast<uint8_t>(buckId));
      return;
    }
    
    LOG_INF("========== BUCK%d Configuration ==========", static_cast<uint8_t>(buckId));
    LOG_INF("  Enabled:      %s", status.powered ? "YES" : "NO");
    LOG_INF("  Power Good:   %s", status.powerGood ? "YES" : "NO");
    LOG_INF("  Power Mode:   %s", (status.currentMode == BuckPowerMode::ForcePwm) ? "Force PWM" : "Auto");
    LOG_INF("  Target Volt:  %d mV", status.targetVoltage);
    LOG_INF("=========================================");
  }

  // ========== Helper Functions ==========

  uint8_t Npm1300::voltageToRegister(uint16_t millivolts)
  {
    // Round to nearest 100mV step.
    uint16_t rounded = ((millivolts + (VOLT_STEP_MV / 2)) / VOLT_STEP_MV) * VOLT_STEP_MV;
    
    // Clamp to valid range.
    if (rounded < VOLT_MIN_MV) rounded = VOLT_MIN_MV;
    if (rounded > VOLT_MAX_MV) rounded = VOLT_MAX_MV;
    
    // Convert to register value: VOUT = 1.0V + (REG × 0.1V).
    return (rounded - VOLT_MIN_MV) / VOLT_STEP_MV;
  }

  uint16_t Npm1300::registerToVoltage(uint8_t regValue)
  {
    // Convert from register value: VOUT = 1.0V + (REG × 0.1V).
    return VOLT_MIN_MV + (regValue * VOLT_STEP_MV);
  }

  bool Npm1300::validateVoltage(uint16_t millivolts)
  {
    if ((millivolts < VOLT_MIN_MV) || (millivolts > VOLT_MAX_MV)) {
      LOG_ERR("Voltage %d mV out of range [%d, %d]", millivolts, VOLT_MIN_MV, VOLT_MAX_MV);
      return false;
    }
      
    if ((millivolts % VOLT_STEP_MV) != 0) {
      LOG_WRN("Voltage %d mV will be rounded to nearest 100mV", millivolts);
    }
      
    return true;
  }

  bool Npm1300::validateBuckId(BuckId buckId)
  {
    if ((buckId != BuckId::Buck1) && (buckId != BuckId::Buck2)) {
      LOG_ERR("Invalid BUCK ID: %d", static_cast<uint8_t>(buckId));
      return false;
    }
    return true;
  }

  int Npm1300::writeTask(uint8_t base, uint8_t offset)
  {
    return mfd_npm13xx_reg_write(m_pmic, base, offset, TASK_TRIGGER);
  }

  int Npm1300::readRegister(uint8_t base, uint8_t offset, uint8_t& value)
  {
    return mfd_npm13xx_reg_read(m_pmic, base, offset, &value);
  }

  int Npm1300::writeRegister(uint8_t base, uint8_t offset, uint8_t value)
  {
    return mfd_npm13xx_reg_write(m_pmic, base, offset, value);
  }

  int Npm1300::updateRegister(uint8_t base, uint8_t offset, uint8_t value, uint8_t mask)
  {
    return mfd_npm13xx_reg_update(m_pmic, base, offset, value, mask);
  }
}
