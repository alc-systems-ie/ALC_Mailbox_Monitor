#include "adxl367.hpp"
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(adxl367, LOG_LEVEL_INF);

namespace alc {

  // ========== Register Definitions ==========
  // Embedded here to keep the driver self-contained.

  namespace reg {
    // Device ID registers.
    constexpr uint8_t DEVID_AD        { 0x00 };
    constexpr uint8_t DEVID_MST       { 0x01 };
    constexpr uint8_t PART_ID         { 0x02 };
    
    // Expected values.
    constexpr uint8_t DEVID_AD_VALUE  { 0xAD };
    constexpr uint8_t DEVID_MST_VALUE { 0x1D };
    constexpr uint8_t PART_ID_VALUE   { 0xF7 };
    
    // Data registers (14-bit, upper 8 bits in _H, lower 6 in _L[7:2]).
    constexpr uint8_t XDATA_H         { 0x0E };
    constexpr uint8_t XDATA_L         { 0x0F };
    constexpr uint8_t YDATA_H         { 0x10 };
    constexpr uint8_t YDATA_L         { 0x11 };
    constexpr uint8_t ZDATA_H         { 0x12 };
    constexpr uint8_t ZDATA_L         { 0x13 };

    // Status register.
    constexpr uint8_t STATUS          { 0x0B };
    constexpr uint8_t STATUS_DATA_READY_MASK  { 0x01 };
    constexpr uint8_t STATUS_ACT_MASK         { 0x10 };
    constexpr uint8_t STATUS_INACT_MASK       { 0x20 };
    constexpr uint8_t STATUS_AWAKE_MASK       { 0x40 };
    
    // Activity/Inactivity thresholds.
    constexpr uint8_t THRESH_ACT_H    { 0x20 };
    constexpr uint8_t THRESH_ACT_L    { 0x21 };
    constexpr uint8_t TIME_ACT        { 0x22 };
    constexpr uint8_t THRESH_INACT_H  { 0x23 };
    constexpr uint8_t THRESH_INACT_L  { 0x24 };
    constexpr uint8_t TIME_INACT_H    { 0x25 };
    constexpr uint8_t TIME_INACT_L    { 0x26 };
    
    // Activity/Inactivity control.
    constexpr uint8_t ACT_INACT_CTL   { 0x27 };
    constexpr uint8_t ACT_EN_SHIFT    { 0 };
    constexpr uint8_t INACT_EN_SHIFT  { 2 };
    constexpr uint8_t LINKLOOP_SHIFT  { 4 };
    
    // Filter control.
    constexpr uint8_t FILTER_CTL      { 0x2C };
    constexpr uint8_t RANGE_SHIFT     { 6 };
    constexpr uint8_t RANGE_MASK      { 0xC0 };
    
    // Power control.
    constexpr uint8_t POWER_CTL       { 0x2D };
    constexpr uint8_t MEASURE_SHIFT   { 0 };
    constexpr uint8_t MEASURE_MASK    { 0x03 };
    constexpr uint8_t MEASURE_STANDBY     { 0 };
    constexpr uint8_t MEASURE_MEASUREMENT { 2 };
    constexpr uint8_t WAKEUP_MASK     { 0x08 };
    constexpr uint8_t NOISE_SHIFT     { 4 };
    constexpr uint8_t NOISE_MASK      { 0x30 };
    constexpr uint8_t NOISE_NORMAL    { 0 };
    
    // Timer control (wake-up rate).
    constexpr uint8_t TIMER_CTL       { 0x39 };
    constexpr uint8_t WAKEUP_RATE_SHIFT { 0 };
    constexpr uint8_t WAKEUP_RATE_MASK  { 0x03 };
    
    // Interrupt mapping.
    constexpr uint8_t INTMAP1_LOWER   { 0x2A };
    constexpr uint8_t INTMAP2_LOWER   { 0x2B };
    constexpr uint8_t INT_AWAKE_MASK  { 0x40 };
    constexpr uint8_t INT_LOW_MASK    { 0x80 };
    
    // Soft reset.
    constexpr uint8_t SOFT_RESET      { 0x1F };
    constexpr uint8_t SOFT_RESET_CODE { 0x52 };
    
    // Scale factors (mg per LSB).
    constexpr float SCALE_2G_MG { 0.25f };
    constexpr float SCALE_4G_MG { 0.5f };
    constexpr float SCALE_8G_MG { 1.0f };
    
    // Threshold register masks.
    constexpr uint8_t THRESH_ACT_H_MASK { 0x7F };
    constexpr uint8_t THRESH_ACT_L_MASK { 0xFC };
  }

  // ========== Constructor ==========

  Adxl367::Adxl367(const struct device* i2cDev, I2cAddress addr)
      : m_i2c(i2cDev)
      , m_i2cAddr(static_cast<uint8_t>(addr))
      , m_currentRange(Range::Range2g)
  {
  }

  // ========== Initialisation ==========

  int Adxl367::Init()
  {
    if (!device_is_ready(m_i2c)) {
      LOG_ERR("I2C device not ready!");
      return -ENODEV;
    }

    // Perform soft reset.
    int result { SoftReset() };
    if (result < 0) {
      LOG_ERR("Soft reset failed: %d!", result);
      return result;
    }

    // Wait for device to be ready.
    k_msleep(STARTUP_DELAY_MS);

    // Verify device ID.
    result = verifyDeviceId();
    if (result < 0) {
      LOG_ERR("Device ID verification failed: %d!", result);
      return result;
    }

    LOG_INF("ADXL367 initialised (I2C addr: 0x%02X).", m_i2cAddr);

    return 0;
  }

  int Adxl367::SoftReset()
  {
    int result { writeRegister(reg::SOFT_RESET, reg::SOFT_RESET_CODE) };
    if (result < 0) {
      LOG_ERR("Failed to write soft reset: %d!", result);
      return result;
    }

    // Wait for reset to complete.
    k_msleep(RESET_DELAY_MS);

    LOG_DBG("Soft reset complete.");
    return 0;
  }

  int Adxl367::verifyDeviceId()
  {
    uint8_t adDevId { 0 };
    uint8_t memsDevId { 0 };
    uint8_t partId { 0 };

    int result { readRegister(reg::DEVID_AD, adDevId) };
    if (result < 0) { return result; }

    result = readRegister(reg::DEVID_MST, memsDevId);
    if (result < 0) { return result; }

    result = readRegister(reg::PART_ID, partId);
    if (result < 0) { return result; }

    if (adDevId != reg::DEVID_AD_VALUE) {
      LOG_ERR("Invalid AD Device ID: 0x%02X (expected 0x%02X)!", adDevId, reg::DEVID_AD_VALUE);
      return -ENODEV;
    }

    if (memsDevId != reg::DEVID_MST_VALUE) {
      LOG_ERR("Invalid MEMS Device ID: 0x%02X (expected 0x%02X)!", memsDevId, reg::DEVID_MST_VALUE);
      return -ENODEV;
    }

    if (partId != reg::PART_ID_VALUE) {
      LOG_ERR("Invalid Part ID: 0x%02X (expected 0x%02X)!", partId, reg::PART_ID_VALUE);
      return -ENODEV;
    }

    LOG_INF("Device ID verified: AD=0x%02X, MEMS=0x%02X, Part=0x%02X.", adDevId, memsDevId, partId);
    return 0;
  }

  // ========== Operating Mode ==========

  int Adxl367::SetOperatingMode(OperatingMode mode)
  {
    uint8_t measureValue { (mode == OperatingMode::Measurement) 
                           ? reg::MEASURE_MEASUREMENT 
                           : reg::MEASURE_STANDBY };

    int result { updateRegister(reg::POWER_CTL, 
                                measureValue << reg::MEASURE_SHIFT, 
                                reg::MEASURE_MASK) };
    if (result < 0) {
      LOG_ERR("Failed to set operating mode: %d!", result);
      return result;
    }

    if (mode == OperatingMode::Measurement) {
      // Wait for output to settle.
      k_msleep(STARTUP_DELAY_MS);
    }

    LOG_INF("Operating mode set to %s.", 
            (mode == OperatingMode::Measurement) ? "Measurement" : "Standby");
    return 0;
  }

  int Adxl367::EnableWakeupMode(WakeupRate rate)
  {
    // Per datasheet: wake-up mode requires Normal noise mode.
    uint8_t powerCtl { 0 };
    int result { readRegister(reg::POWER_CTL, powerCtl) };
    if (result < 0) {
      LOG_ERR("Failed to read POWER_CTL: %d!", result);
      return result;
    }

    uint8_t noiseMode { static_cast<uint8_t>((powerCtl & reg::NOISE_MASK) >> reg::NOISE_SHIFT) };
    if (noiseMode != reg::NOISE_NORMAL) {
      LOG_WRN("Wake-up mode requires Normal noise mode. Switching...");
      result = updateRegister(reg::POWER_CTL, 
                              reg::NOISE_NORMAL << reg::NOISE_SHIFT, 
                              reg::NOISE_MASK);
      if (result < 0) {
        LOG_ERR("Failed to set noise mode: %d!", result);
        return result;
      }
    }

    // Set wake-up rate.
    result = updateRegister(reg::TIMER_CTL, 
                            static_cast<uint8_t>(rate) << reg::WAKEUP_RATE_SHIFT, 
                            reg::WAKEUP_RATE_MASK);
    if (result < 0) {
      LOG_ERR("Failed to set wake-up rate: %d!", result);
      return result;
    }

    // Enable wake-up mode bit.
    result = updateRegister(reg::POWER_CTL, reg::WAKEUP_MASK, reg::WAKEUP_MASK);
    if (result < 0) {
      LOG_ERR("Failed to enable wake-up mode: %d!", result);
      return result;
    }

    const char* rateStr[] { "12", "6", "3", "1.5" };
    LOG_INF("Wake-up mode enabled at %s SPS.", rateStr[static_cast<uint8_t>(rate)]);
    return 0;
  }

  // ========== Configuration ==========

  int Adxl367::SetRange(Range range)
  {
    int result { updateRegister(reg::FILTER_CTL, 
                                static_cast<uint8_t>(range) << reg::RANGE_SHIFT, 
                                reg::RANGE_MASK) };
    if (result < 0) {
      LOG_ERR("Failed to set range: %d!", result);
      return result;
    }

    m_currentRange = range;

    const char* rangeStr[] { "±2g", "±4g", "±8g" };
    LOG_INF("Range set to %s.", rangeStr[static_cast<uint8_t>(range)]);
    return 0;
  }

  int Adxl367::ConfigureActivity(const ActivityConfig& config)
  {
    int result;

    // Set activity threshold.
    uint16_t actThresh { mgToThreshold(config.activityThreshold) };
    result = writeRegister(reg::THRESH_ACT_H, (actThresh >> 6) & reg::THRESH_ACT_H_MASK);
    if (result < 0) { return result; }
    result = writeRegister(reg::THRESH_ACT_L, (actThresh << 2) & reg::THRESH_ACT_L_MASK);
    if (result < 0) { return result; }

    // Set activity time.
    result = writeRegister(reg::TIME_ACT, config.activityTime);
    if (result < 0) { return result; }

    // Set inactivity threshold.
    uint16_t inactThresh { mgToThreshold(config.inactivityThreshold) };
    result = writeRegister(reg::THRESH_INACT_H, (inactThresh >> 6) & reg::THRESH_ACT_H_MASK);
    if (result < 0) { return result; }
    result = writeRegister(reg::THRESH_INACT_L, (inactThresh << 2) & reg::THRESH_ACT_L_MASK);
    if (result < 0) { return result; }

    // Set inactivity time (16-bit).
    result = writeRegister(reg::TIME_INACT_H, (config.inactivityTime >> 8) & 0xFF);
    if (result < 0) { return result; }
    result = writeRegister(reg::TIME_INACT_L, config.inactivityTime & 0xFF);
    if (result < 0) { return result; }

    // Configure activity/inactivity control register.
    uint8_t ctrlValue { static_cast<uint8_t>(
        (static_cast<uint8_t>(config.activityMode) << reg::ACT_EN_SHIFT) |
        (static_cast<uint8_t>(config.inactivityMode) << reg::INACT_EN_SHIFT) |
        (static_cast<uint8_t>(config.linkLoop) << reg::LINKLOOP_SHIFT)
    )};
    
    result = writeRegister(reg::ACT_INACT_CTL, ctrlValue);
    if (result < 0) { return result; }

    LOG_INF("Activity configured: Act=%dmg/%d, Inact=%dmg/%d.",
            config.activityThreshold, config.activityTime,
            config.inactivityThreshold, config.inactivityTime);
    return 0;
  }

  int Adxl367::ConfigureInterrupt(IntPin pin, bool awake, bool activeLow)
  {
    uint8_t intMap { 0 };
    if (awake) { intMap |= reg::INT_AWAKE_MASK; }
    if (activeLow) { intMap |= reg::INT_LOW_MASK; }

    uint8_t regAddr { (pin == IntPin::Int1) ? reg::INTMAP1_LOWER : reg::INTMAP2_LOWER };

    int result { writeRegister(regAddr, intMap) };
    if (result < 0) {
      LOG_ERR("Failed to configure INT%d: %d!", static_cast<uint8_t>(pin), result);
      return result;
    }

    LOG_INF("INT%d configured: AWAKE=%d, ActiveLow=%d.",
            static_cast<uint8_t>(pin), awake, activeLow);
    return 0;
  }

  // ========== Status ==========

  int Adxl367::ReadStatus(Status& status)
  {
    uint8_t value { 0 };
    int result { readRegister(reg::STATUS, value) };
    if (result < 0) {
      LOG_ERR("Failed to read status: %d!", result);
      return result;
    }

    status.dataReady = (value & reg::STATUS_DATA_READY_MASK) != 0;
    status.activityDetected = (value & reg::STATUS_ACT_MASK) != 0;
    status.inactivityDetected = (value & reg::STATUS_INACT_MASK) != 0;
    status.awake = (value & reg::STATUS_AWAKE_MASK) != 0;

    return 0;
  }

  bool Adxl367::IsAwake()
  {
    Status status;
    if (ReadStatus(status) < 0) {
      return true;  // Default to awake on error (safer).
    }
    return status.awake;
  }

  int Adxl367::ReadAxes(AxisData& data)
  {
    // Read all 6 bytes in a single burst read for consistency.
    uint8_t regAddr { reg::XDATA_H };
    uint8_t buffer[6];

    int result { i2c_write_read(m_i2c, m_i2cAddr, &regAddr, 1, buffer, sizeof(buffer)) };
    if (result < 0) {
      LOG_ERR("Failed to read axis data: %d!", result);
      return result;
    }

    // Combine bytes into 14-bit signed values.
    // Format: H[7:0] = D[13:6], L[7:2] = D[5:0], L[1:0] = unused.
    auto combine14bit = [](uint8_t h, uint8_t l) -> int16_t {
      int16_t raw { static_cast<int16_t>((static_cast<uint16_t>(h) << 6) | (l >> 2)) };
      // Sign-extend from 14-bit to 16-bit.
      if (raw & 0x2000) {
        raw |= 0xC000;
      }
      return raw;
    };

    int16_t rawX { combine14bit(buffer[0], buffer[1]) };
    int16_t rawY { combine14bit(buffer[2], buffer[3]) };
    int16_t rawZ { combine14bit(buffer[4], buffer[5]) };

    // Convert to milli-g using current range scale factor.
    float scale { getScaleFactor() };
    data.x = static_cast<int16_t>(static_cast<float>(rawX) * scale);
    data.y = static_cast<int16_t>(static_cast<float>(rawY) * scale);
    data.z = static_cast<int16_t>(static_cast<float>(rawZ) * scale);

    return 0;
  }

  // ========== Threshold Updates ==========

  int Adxl367::SetActivityThreshold(uint16_t thresholdMg)
  {
    uint16_t thresh { mgToThreshold(thresholdMg) };
    
    int result { writeRegister(reg::THRESH_ACT_H, (thresh >> 6) & reg::THRESH_ACT_H_MASK) };
    if (result < 0) { return result; }
    
    result = writeRegister(reg::THRESH_ACT_L, (thresh << 2) & reg::THRESH_ACT_L_MASK);
    if (result < 0) { return result; }

    LOG_INF("Activity threshold updated to %d mg.", thresholdMg);
    return 0;
  }

  int Adxl367::SetInactivityThreshold(uint16_t thresholdMg)
  {
    uint16_t thresh { mgToThreshold(thresholdMg) };
    
    int result { writeRegister(reg::THRESH_INACT_H, (thresh >> 6) & reg::THRESH_ACT_H_MASK) };
    if (result < 0) { return result; }
    
    result = writeRegister(reg::THRESH_INACT_L, (thresh << 2) & reg::THRESH_ACT_L_MASK);
    if (result < 0) { return result; }

    LOG_INF("Inactivity threshold updated to %d mg.", thresholdMg);
    return 0;
  }

  int Adxl367::SetInactivityTime(uint16_t samples)
  {
    int result { writeRegister(reg::TIME_INACT_H, (samples >> 8) & 0xFF) };
    if (result < 0) { return result; }
    
    result = writeRegister(reg::TIME_INACT_L, samples & 0xFF);
    if (result < 0) { return result; }

    LOG_INF("Inactivity time updated to %d samples.", samples);
    return 0;
  }

  // ========== Debug ==========

  void Adxl367::PrintConfiguration()
  {
    LOG_INF("========== ADXL367 Configuration ==========");

    const char* rangeStr[] { "±2g", "±4g", "±8g" };
    LOG_INF("  I2C Address:  0x%02X", m_i2cAddr);
    LOG_INF("  Range:        %s", rangeStr[static_cast<uint8_t>(m_currentRange)]);

    Status status;
    if (ReadStatus(status) == 0) {
      LOG_INF("  Awake:        %s", status.awake ? "YES" : "NO");
      LOG_INF("  Activity:     %s", status.activityDetected ? "YES" : "NO");
      LOG_INF("  Inactivity:   %s", status.inactivityDetected ? "YES" : "NO");
    }

    LOG_INF("=============================================");
  }

  // ========== I2C Helpers ==========

  int Adxl367::writeRegister(uint8_t reg, uint8_t value)
  {
    uint8_t buffer[2] { reg, value };
    
    int result { i2c_write(m_i2c, buffer, sizeof(buffer), m_i2cAddr) };
    if (result < 0) {
      LOG_ERR("I2C write failed: reg=0x%02X, err=%d!", reg, result);
      return result;
    }

    return 0;
  }

  int Adxl367::readRegister(uint8_t reg, uint8_t& value)
  {
    int result { i2c_write_read(m_i2c, m_i2cAddr, &reg, 1, &value, 1) };
    if (result < 0) {
      LOG_ERR("I2C read failed: reg=0x%02X, err=%d!", reg, result);
      return result;
    }

    return 0;
  }

  int Adxl367::updateRegister(uint8_t reg, uint8_t value, uint8_t mask)
  {
    uint8_t current { 0 };
    int result { readRegister(reg, current) };
    if (result < 0) {
      return result;
    }

    uint8_t newValue { static_cast<uint8_t>((current & ~mask) | (value & mask)) };
    return writeRegister(reg, newValue);
  }

  // ========== Conversion Helpers ==========

  uint16_t Adxl367::mgToThreshold(uint16_t mg)
  {
    float scale { getScaleFactor() };
    return static_cast<uint16_t>(static_cast<float>(mg) / scale);
  }

  float Adxl367::getScaleFactor()
  {
    switch (m_currentRange) {
      case Range::Range2g:
        return reg::SCALE_2G_MG;
      case Range::Range4g:
        return reg::SCALE_4G_MG;
      case Range::Range8g:
        return reg::SCALE_8G_MG;
      default:
        return reg::SCALE_2G_MG;
    }
  }

} // namespace alc
