#include "adxl367.hpp"
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(adxl367, LOG_LEVEL_INF);

namespace alc {

  // ========== Register Addresses ==========

  // Device ID registers.
  constexpr uint8_t M_REG_DEVID_AD        { 0x00 };
  constexpr uint8_t M_REG_DEVID_MST       { 0x01 };
  constexpr uint8_t M_REG_PART_ID         { 0x02 };

  // Status.
  constexpr uint8_t M_REG_STATUS          { 0x0B };

  // Data registers (14-bit: H[7:0]=D[13:6], L[7:2]=D[5:0], L[1:0]=reserved).
  constexpr uint8_t M_REG_XDATA_H         { 0x0E };
  constexpr uint8_t M_REG_XDATA_L         { 0x0F };
  constexpr uint8_t M_REG_YDATA_H         { 0x10 };
  constexpr uint8_t M_REG_YDATA_L         { 0x11 };
  constexpr uint8_t M_REG_ZDATA_H         { 0x12 };
  constexpr uint8_t M_REG_ZDATA_L         { 0x13 };
  constexpr uint8_t M_REG_TEMP_H          { 0x14 };
  constexpr uint8_t M_REG_TEMP_L          { 0x15 };

  // FIFO.
  constexpr uint8_t M_REG_FIFO_ENTRIES_L  { 0x0C };
  constexpr uint8_t M_REG_FIFO_ENTRIES_H  { 0x0D };
  constexpr uint8_t M_REG_I2C_FIFO_DATA   { 0x18 };

  // Soft reset.
  constexpr uint8_t M_REG_SOFT_RESET      { 0x1F };

  // Activity/Inactivity thresholds.
  constexpr uint8_t M_REG_THRESH_ACT_H    { 0x20 };
  constexpr uint8_t M_REG_THRESH_ACT_L    { 0x21 };
  constexpr uint8_t M_REG_TIME_ACT        { 0x22 };
  constexpr uint8_t M_REG_THRESH_INACT_H  { 0x23 };
  constexpr uint8_t M_REG_THRESH_INACT_L  { 0x24 };
  constexpr uint8_t M_REG_TIME_INACT_H    { 0x25 };
  constexpr uint8_t M_REG_TIME_INACT_L    { 0x26 };

  // Activity/Inactivity control.
  constexpr uint8_t M_REG_ACT_INACT_CTL   { 0x27 };

  // FIFO control.
  constexpr uint8_t M_REG_FIFO_CONTROL    { 0x28 };
  constexpr uint8_t M_REG_FIFO_SAMPLES    { 0x29 };

  // Interrupt mapping.
  constexpr uint8_t M_REG_INTMAP1_LOWER   { 0x2A };
  constexpr uint8_t M_REG_INTMAP2_LOWER   { 0x2B };

  // Filter control.
  constexpr uint8_t M_REG_FILTER_CTL      { 0x2C };

  // Power control.
  constexpr uint8_t M_REG_POWER_CTL       { 0x2D };

  // Timer control (wake-up rate).
  constexpr uint8_t M_REG_TIMER_CTL       { 0x39 };

  // ========== Expected ID Values ==========

  constexpr uint8_t M_DEVID_AD_VALUE      { 0xAD };
  constexpr uint8_t M_DEVID_MST_VALUE     { 0x1D };
  constexpr uint8_t M_PART_ID_VALUE       { 0xF7 };

  // ========== Masks and Shifts ==========

  // STATUS register.
  constexpr uint8_t M_STATUS_DATA_READY   { 0x01 };
  constexpr uint8_t M_STATUS_FIFO_READY   { 0x02 };
  constexpr uint8_t M_STATUS_FIFO_WM      { 0x04 };
  constexpr uint8_t M_STATUS_FIFO_OVERRUN { 0x08 };
  constexpr uint8_t M_STATUS_ACT          { 0x10 };
  constexpr uint8_t M_STATUS_INACT        { 0x20 };
  constexpr uint8_t M_STATUS_AWAKE        { 0x40 };
  constexpr uint8_t M_STATUS_ERR_USER     { 0x80 };

  // ACT_INACT_CTL shifts.
  constexpr uint8_t M_ACT_EN_SHIFT        { 0 };
  constexpr uint8_t M_INACT_EN_SHIFT      { 2 };
  constexpr uint8_t M_LINKLOOP_SHIFT      { 4 };

  // FIFO_CONTROL (0x28): [7]=reserved, [6:3]=CHANNEL_SELECT, [2]=FIFO_SAMPLES[8], [1:0]=FIFO_MODE.
  constexpr uint8_t M_FIFO_MODE_MASK      { 0x03 };
  constexpr uint8_t M_FIFO_SAMPLES_BIT8   { 0x04 };
  constexpr uint8_t M_FIFO_CHANNEL_SHIFT  { 3 };
  constexpr uint8_t M_FIFO_CHANNEL_MASK   { 0x78 };

  // FILTER_CTL (0x2C): [7:6]=RANGE, [5]=I2C_HS, [4]=reserved, [3]=EXT_SAMPLE, [2:0]=ODR.
  constexpr uint8_t M_RANGE_SHIFT         { 6 };
  constexpr uint8_t M_RANGE_MASK          { 0xC0 };
  constexpr uint8_t M_I2C_HS_MASK         { 0x20 };
  constexpr uint8_t M_EXT_SAMPLE_MASK     { 0x08 };
  constexpr uint8_t M_ODR_MASK            { 0x07 };

  // POWER_CTL (0x2D): [7]=reserved, [6]=EXT_CLK, [5:4]=NOISE, [3]=WAKEUP, [2]=AUTOSLEEP, [1:0]=MEASURE.
  constexpr uint8_t M_MEASURE_SHIFT       { 0 };
  constexpr uint8_t M_MEASURE_MASK        { 0x03 };
  constexpr uint8_t M_MEASURE_STANDBY     { 0 };
  constexpr uint8_t M_MEASURE_MEASUREMENT { 2 };
  constexpr uint8_t M_AUTOSLEEP_MASK      { 0x04 };
  constexpr uint8_t M_WAKEUP_MASK         { 0x08 };
  constexpr uint8_t M_NOISE_SHIFT         { 4 };
  constexpr uint8_t M_NOISE_MASK          { 0x30 };
  constexpr uint8_t M_NOISE_NORMAL        { 0 };
  constexpr uint8_t M_EXT_CLK_MASK        { 0x40 };

  // TIMER_CTL (0x39): [7:6]=WAKEUP_RATE, [5]=reserved, [4:0]=TIMER_KEEP_ALIVE.
  constexpr uint8_t M_WAKEUP_RATE_SHIFT   { 6 };
  constexpr uint8_t M_WAKEUP_RATE_MASK    { 0xC0 };
  constexpr uint8_t M_KEEP_ALIVE_MASK     { 0x1F };

  // INTMAP1_LOWER (0x2A) / INTMAP2_LOWER (0x2B): same bit layout for INT1/INT2.
  // [7]=INT_LOW, [6]=AWAKE, [5]=INACT, [4]=ACT, [3]=FIFO_OVERRUN, [2]=FIFO_WM, [1]=FIFO_READY, [0]=DATA_READY.
  constexpr uint8_t M_INT_DATA_READY_MASK { 0x01 };
  constexpr uint8_t M_INT_FIFO_READY_MASK { 0x02 };
  constexpr uint8_t M_INT_FIFO_WM_MASK    { 0x04 };
  constexpr uint8_t M_INT_FIFO_OVR_MASK   { 0x08 };
  constexpr uint8_t M_INT_ACT_MASK        { 0x10 };
  constexpr uint8_t M_INT_INACT_MASK      { 0x20 };
  constexpr uint8_t M_INT_AWAKE_MASK      { 0x40 };
  constexpr uint8_t M_INT_LOW_MASK        { 0x80 };

  // Soft reset.
  constexpr uint8_t M_SOFT_RESET_CODE     { 0x52 };

  // Threshold register masks.
  constexpr uint8_t M_THRESH_H_MASK       { 0x7F };
  constexpr uint8_t M_THRESH_L_MASK       { 0xFC };

  // Scale factors (mg per LSB).
  constexpr float M_SCALE_2G_MG           { 0.25f };
  constexpr float M_SCALE_4G_MG           { 0.5f };
  constexpr float M_SCALE_8G_MG           { 1.0f };

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
    k_msleep(M_STARTUP_DELAY_MS);

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
    int result { writeRegister(M_REG_SOFT_RESET, M_SOFT_RESET_CODE) };
    if (result < 0) {
      LOG_ERR("Failed to write soft reset: %d!", result);
      return result;
    }

    // Wait for reset to complete.
    k_msleep(M_RESET_DELAY_MS);

    LOG_DBG("Soft reset complete.");
    return 0;
  }

  int Adxl367::verifyDeviceId()
  {
    uint8_t adDevId { 0 };
    uint8_t memsDevId { 0 };
    uint8_t partId { 0 };

    int result { readRegister(M_REG_DEVID_AD, adDevId) };
    if (result < 0) { return result; }

    result = readRegister(M_REG_DEVID_MST, memsDevId);
    if (result < 0) { return result; }

    result = readRegister(M_REG_PART_ID, partId);
    if (result < 0) { return result; }

    if (adDevId != M_DEVID_AD_VALUE) {
      LOG_ERR("Invalid AD Device ID: 0x%02X (expected 0x%02X)!", adDevId, M_DEVID_AD_VALUE);
      return -ENODEV;
    }

    if (memsDevId != M_DEVID_MST_VALUE) {
      LOG_ERR("Invalid MEMS Device ID: 0x%02X (expected 0x%02X)!", memsDevId, M_DEVID_MST_VALUE);
      return -ENODEV;
    }

    if (partId != M_PART_ID_VALUE) {
      LOG_ERR("Invalid Part ID: 0x%02X (expected 0x%02X)!", partId, M_PART_ID_VALUE);
      return -ENODEV;
    }

    LOG_INF("Device ID verified: AD=0x%02X, MEMS=0x%02X, Part=0x%02X.", adDevId, memsDevId, partId);
    return 0;
  }

  // ========== Operating Mode ==========

  int Adxl367::SetOperatingMode(OperatingMode mode)
  {
    uint8_t measureValue { (mode == OperatingMode::Measurement)
                           ? M_MEASURE_MEASUREMENT
                           : M_MEASURE_STANDBY };

    int result { updateRegister(M_REG_POWER_CTL,
                                measureValue << M_MEASURE_SHIFT,
                                M_MEASURE_MASK) };
    if (result < 0) {
      LOG_ERR("Failed to set operating mode: %d!", result);
      return result;
    }

    if (mode == OperatingMode::Measurement) {
      // Wait for output to settle.
      k_msleep(M_STARTUP_DELAY_MS);
    }

    LOG_INF("Operating mode set to %s.",
            (mode == OperatingMode::Measurement) ? "Measurement" : "Standby");
    return 0;
  }

  int Adxl367::EnableWakeupMode(WakeupRate rate)
  {
    // Per datasheet: wake-up mode requires Normal noise mode.
    uint8_t powerCtl { 0 };
    int result { readRegister(M_REG_POWER_CTL, powerCtl) };
    if (result < 0) {
      LOG_ERR("Failed to read POWER_CTL: %d!", result);
      return result;
    }

    uint8_t noiseMode { static_cast<uint8_t>((powerCtl & M_NOISE_MASK) >> M_NOISE_SHIFT) };
    if (noiseMode != M_NOISE_NORMAL) {
      LOG_WRN("Wake-up mode requires Normal noise mode. Switching...");
      result = updateRegister(M_REG_POWER_CTL,
                              M_NOISE_NORMAL << M_NOISE_SHIFT,
                              M_NOISE_MASK);
      if (result < 0) {
        LOG_ERR("Failed to set noise mode: %d!", result);
        return result;
      }
    }

    // Set wake-up rate.
    result = updateRegister(M_REG_TIMER_CTL,
                            static_cast<uint8_t>(rate) << M_WAKEUP_RATE_SHIFT,
                            M_WAKEUP_RATE_MASK);
    if (result < 0) {
      LOG_ERR("Failed to set wake-up rate: %d!", result);
      return result;
    }

    // Enable wake-up mode bit.
    result = updateRegister(M_REG_POWER_CTL, M_WAKEUP_MASK, M_WAKEUP_MASK);
    if (result < 0) {
      LOG_ERR("Failed to enable wake-up mode: %d!", result);
      return result;
    }

    const char* rateStr[] { "12", "6", "3", "1.5" };
    LOG_INF("Wake-up mode enabled at %s SPS.", rateStr[static_cast<uint8_t>(rate)]);
    return 0;
  }

  int Adxl367::DisableWakeupMode()
  {
    int result { updateRegister(M_REG_POWER_CTL, 0, M_WAKEUP_MASK) };
    if (result < 0) {
      LOG_ERR("Failed to disable wake-up mode: %d!", result);
      return result;
    }

    LOG_INF("Wake-up mode disabled (full ODR active).");
    return 0;
  }

  int Adxl367::EnableMeasurementAutosleep()
  {
    // POWER_CTL = 0x07: MEASURE=10 (measurement), AUTOSLEEP=1.
    // No explicit WAKEUP bit — autosleep handles the transition to wake-up mode.
    constexpr uint8_t M_POWER_CTL_MEAS_AUTOSLEEP { 0x06 };
    int result { writeRegister(M_REG_POWER_CTL, M_POWER_CTL_MEAS_AUTOSLEEP) };
    if (result < 0) {
      LOG_ERR("Failed to enable measurement+autosleep: %d!", result);
      return result;
    }

    LOG_INF("Measurement mode with autosleep enabled (POWER_CTL=0x%02X).",
            M_POWER_CTL_MEAS_AUTOSLEEP);
    return 0;
  }

  // ========== Configuration ==========

  int Adxl367::SetRange(Range range)
  {
    int result { updateRegister(M_REG_FILTER_CTL,
                                static_cast<uint8_t>(range) << M_RANGE_SHIFT,
                                M_RANGE_MASK) };
    if (result < 0) {
      LOG_ERR("Failed to set range: %d!", result);
      return result;
    }

    m_currentRange = range;

    const char* rangeStr[] { "±2g", "±4g", "±8g" };
    LOG_INF("Range set to %s.", rangeStr[static_cast<uint8_t>(range)]);
    return 0;
  }

  int Adxl367::SetOdr(ODR odr)
  {
    int result { updateRegister(M_REG_FILTER_CTL, static_cast<uint8_t>(odr), M_ODR_MASK) };
    if (result < 0) {
      LOG_ERR("Failed to set ODR: %d!", result);
      return result;
    }

    const char* odrStr[] { "12.5Hz", "25Hz", "50Hz", "100Hz", "200Hz", "400Hz" };
    LOG_INF("ODR set to %s.", odrStr[static_cast<uint8_t>(odr)]);
    return 0;
  }

  // ========== FIFO ==========

  int Adxl367::ConfigureFifo(FifoMode mode)
  {
    // FIFO_CONTROL (0x28): CHANNEL_SELECT[6:3]=0x0 (XYZ), FIFO_SAMPLES[8]=0, FIFO_MODE[1:0].
    uint8_t value { static_cast<uint8_t>(mode) };
    int result { writeRegister(M_REG_FIFO_CONTROL, value) };
    if (result < 0) {
      LOG_ERR("Failed to configure FIFO: %d!", result);
      return result;
    }

    // FIFO_SAMPLES (0x29): Watermark bits [7:0]. Default is 0x80; set to 0
    // (combined with FIFO_SAMPLES[8]=0 in FIFO_CONTROL, watermark = 0).
    result = writeRegister(M_REG_FIFO_SAMPLES, 0);
    if (result < 0) {
      LOG_ERR("Failed to set FIFO samples: %d!", result);
      return result;
    }

    const char* modeStr[] { "Disabled", "OldestSaved", "Stream", "Triggered" };
    LOG_INF("FIFO configured: mode=%s, channels=XYZ.", modeStr[static_cast<uint8_t>(mode)]);
    return 0;
  }

  int Adxl367::ReadFifoEntries(uint16_t& entries)
  {
    uint8_t hi { 0 };
    uint8_t lo { 0 };

    int result { readRegister(M_REG_FIFO_ENTRIES_H, hi) };
    if (result < 0) { return result; }

    result = readRegister(M_REG_FIFO_ENTRIES_L, lo);
    if (result < 0) { return result; }

    // FIFO_ENTRIES is 10-bit: H[1:0] are MSBs, L[7:0] are LSBs.
    entries = static_cast<uint16_t>(((hi & 0x03) << 8) | lo);
    return 0;
  }

  int Adxl367::ReadFifo(FifoSample* samples, uint16_t maxSets, uint16_t& setsRead)
  {
    setsRead = 0;

    uint16_t entries { 0 };
    int result { ReadFifoEntries(entries) };
    if (result < 0) { return result; }

    LOG_INF("FIFO_ENTRIES raw: %u", entries);

    if (entries == 0) { return 0; }

    // Each XYZ set = 3 entries. Each entry = 2 bytes (14-bit + ID format).
    uint16_t availableSets { static_cast<uint16_t>(entries / 3) };
    uint16_t setsToRead { (availableSets > maxSets) ? maxSets : availableSets };
    uint16_t bytesToRead { static_cast<uint16_t>(setsToRead * 3 * 2) };

    if (bytesToRead == 0) { return 0; }

    // Bulk read from I2C_FIFO_DATA (0x18).
    // Cap to keep stack usage reasonable.
    constexpr uint16_t M_MAX_RAW_BYTES { 30 * 3 * 2 };  // 30 XYZ sets = 180 bytes.
    uint8_t raw[M_MAX_RAW_BYTES];
    if (bytesToRead > M_MAX_RAW_BYTES) {
      setsToRead = M_MAX_RAW_BYTES / 6;
      bytesToRead = static_cast<uint16_t>(setsToRead * 6);
    }
    result = readBurst(M_REG_I2C_FIFO_DATA, raw, bytesToRead);
    if (result < 0) {
      LOG_ERR("FIFO bulk read failed: %d!", result);
      return result;
    }

    // Log first 6 raw bytes for format debugging.
    if (bytesToRead >= 6) {
      LOG_INF("FIFO raw[0..5]: %02X %02X %02X %02X %02X %02X",
              raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);
    }

    // Decode FIFO samples.
    // FIFO format (14-bit + ID): D[15:14]=channel ID, D[13:0]=signed 14-bit data.
    // First byte is MSB, second byte is LSB.
    float scale { getScaleFactor() };

    for (uint16_t i = 0; i < setsToRead; ++i) {
      for (uint8_t ch = 0; ch < 3; ++ch) {
        uint16_t offset { static_cast<uint16_t>((i * 3 + ch) * 2) };
        uint16_t raw16 { static_cast<uint16_t>((raw[offset] << 8) | raw[offset + 1]) };

        // Extract 14-bit signed value (bits [13:0]).
        int16_t rawVal { static_cast<int16_t>(raw16 & 0x3FFF) };
        // Sign-extend from 14-bit.
        if (rawVal & 0x2000) {
          rawVal |= static_cast<int16_t>(0xC000);
        }

        int16_t mg { static_cast<int16_t>(static_cast<float>(rawVal) * scale) };

        switch (ch) {
          case 0: samples[i].x = mg; break;
          case 1: samples[i].y = mg; break;
          case 2: samples[i].z = mg; break;
        }
      }
    }

    // Count valid samples (stop at first all-zero set - gravity ensures at least one axis is non-zero).
    uint16_t validSets { 0 };
    for (uint16_t i = 0; i < setsToRead; ++i) {
      if (samples[i].x == 0 && samples[i].y == 0 && samples[i].z == 0) {
        break;
      }
      validSets = i + 1;
      LOG_INF("FIFO[%u]: X=%d Y=%d Z=%d mg", i, samples[i].x, samples[i].y, samples[i].z);
    }

    if (validSets < setsToRead) {
      LOG_INF("FIFO: %u valid of %u decoded (stopped at zero padding).", validSets, setsToRead);
    }

    setsRead = validSets;
    return 0;
  }

  // ========== Activity/Inactivity ==========

  int Adxl367::ConfigureActivity(const ActivityConfig& config)
  {
    int result;

    // Set activity threshold.
    uint16_t actThresh { mgToThreshold(config.activityThreshold) };
    result = writeRegister(M_REG_THRESH_ACT_H, (actThresh >> 6) & M_THRESH_H_MASK);
    if (result < 0) { return result; }
    result = writeRegister(M_REG_THRESH_ACT_L, (actThresh << 2) & M_THRESH_L_MASK);
    if (result < 0) { return result; }

    // Set activity time.
    result = writeRegister(M_REG_TIME_ACT, config.activityTime);
    if (result < 0) { return result; }

    // Set inactivity threshold.
    uint16_t inactThresh { mgToThreshold(config.inactivityThreshold) };
    result = writeRegister(M_REG_THRESH_INACT_H, (inactThresh >> 6) & M_THRESH_H_MASK);
    if (result < 0) { return result; }
    result = writeRegister(M_REG_THRESH_INACT_L, (inactThresh << 2) & M_THRESH_L_MASK);
    if (result < 0) { return result; }

    // Set inactivity time (16-bit).
    result = writeRegister(M_REG_TIME_INACT_H, (config.inactivityTime >> 8) & 0xFF);
    if (result < 0) { return result; }
    result = writeRegister(M_REG_TIME_INACT_L, config.inactivityTime & 0xFF);
    if (result < 0) { return result; }

    // Configure activity/inactivity control register.
    uint8_t ctrlValue { static_cast<uint8_t>(
        (static_cast<uint8_t>(config.activityMode) << M_ACT_EN_SHIFT) |
        (static_cast<uint8_t>(config.inactivityMode) << M_INACT_EN_SHIFT) |
        (static_cast<uint8_t>(config.linkLoop) << M_LINKLOOP_SHIFT)
    )};

    result = writeRegister(M_REG_ACT_INACT_CTL, ctrlValue);
    if (result < 0) { return result; }

    LOG_INF("Activity configured: Act=%dmg/%d, Inact=%dmg/%d.",
            config.activityThreshold, config.activityTime,
            config.inactivityThreshold, config.inactivityTime);
    return 0;
  }

  int Adxl367::ConfigureInterrupt(IntPin pin, bool awake, bool activeLow)
  {
    uint8_t intMap { 0 };
    if (awake) { intMap |= M_INT_AWAKE_MASK; }
    if (activeLow) { intMap |= M_INT_LOW_MASK; }

    uint8_t regAddr { (pin == IntPin::Int1) ? M_REG_INTMAP1_LOWER : M_REG_INTMAP2_LOWER };

    int result { writeRegister(regAddr, intMap) };
    if (result < 0) {
      LOG_ERR("Failed to configure INT%d: %d!", static_cast<uint8_t>(pin), result);
      return result;
    }

    LOG_INF("INT%d configured: AWAKE=%d, ActiveLow=%d.", static_cast<uint8_t>(pin), awake, activeLow);
    return 0;
  }

  // ========== Status ==========

  int Adxl367::ReadStatus(Status& status)
  {
    uint8_t value { 0 };
    int result { readRegister(M_REG_STATUS, value) };
    if (result < 0) {
      LOG_ERR("Failed to read status: %d!", result);
      return result;
    }

    status.dataReady = (value & M_STATUS_DATA_READY) != 0;
    status.fifoReady = (value & M_STATUS_FIFO_READY) != 0;
    status.fifoWatermark = (value & M_STATUS_FIFO_WM) != 0;
    status.fifoOverrun = (value & M_STATUS_FIFO_OVERRUN) != 0;
    status.activityDetected = (value & M_STATUS_ACT) != 0;
    status.inactivityDetected = (value & M_STATUS_INACT) != 0;
    status.awake = (value & M_STATUS_AWAKE) != 0;
    status.errUserRegs = (value & M_STATUS_ERR_USER) != 0;

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

  // ========== Data Register Reads ==========

  int Adxl367::ReadAxes(int16_t& x, int16_t& y, int16_t& z)
  {
    // Bulk read 6 bytes: XDATA_H, XDATA_L, YDATA_H, YDATA_L, ZDATA_H, ZDATA_L.
    uint8_t raw[6];
    int result { readBurst(M_REG_XDATA_H, raw, 6) };
    if (result < 0) {
      LOG_ERR("Failed to read axes: %d!", result);
      return result;
    }

    // Data register format: H[7:0]=D[13:6], L[7:2]=D[5:0], L[1:0]=reserved.
    // Combine to 14-bit signed value, then convert to mg.
    float scale { getScaleFactor() };

    auto decode = [scale](uint8_t hi, uint8_t lo) -> int16_t {
      int16_t rawVal { static_cast<int16_t>((hi << 6) | (lo >> 2)) };
      // Sign-extend from 14-bit.
      if (rawVal & 0x2000) {
        rawVal |= static_cast<int16_t>(0xC000);
      }
      return static_cast<int16_t>(static_cast<float>(rawVal) * scale);
    };

    x = decode(raw[0], raw[1]);
    y = decode(raw[2], raw[3]);
    z = decode(raw[4], raw[5]);

    return 0;
  }

  int Adxl367::ReadTemperature(int16_t& tempRaw)
  {
    uint8_t raw[2];
    int result { readBurst(M_REG_TEMP_H, raw, 2) };
    if (result < 0) {
      LOG_ERR("Failed to read temperature: %d!", result);
      return result;
    }

    // Same format as data registers: H[7:0]=D[13:6], L[7:2]=D[5:0].
    tempRaw = static_cast<int16_t>((raw[0] << 6) | (raw[1] >> 2));
    if (tempRaw & 0x2000) {
      tempRaw |= static_cast<int16_t>(0xC000);
    }

    return 0;
  }

  // ========== Threshold Updates ==========

  int Adxl367::SetActivityThreshold(uint16_t thresholdMg)
  {
    uint16_t thresh { mgToThreshold(thresholdMg) };

    int result { writeRegister(M_REG_THRESH_ACT_H, (thresh >> 6) & M_THRESH_H_MASK) };
    if (result < 0) { return result; }

    result = writeRegister(M_REG_THRESH_ACT_L, (thresh << 2) & M_THRESH_L_MASK);
    if (result < 0) { return result; }

    LOG_INF("Activity threshold updated to %d mg.", thresholdMg);
    return 0;
  }

  int Adxl367::SetInactivityThreshold(uint16_t thresholdMg)
  {
    uint16_t thresh { mgToThreshold(thresholdMg) };

    int result { writeRegister(M_REG_THRESH_INACT_H, (thresh >> 6) & M_THRESH_H_MASK) };
    if (result < 0) { return result; }

    result = writeRegister(M_REG_THRESH_INACT_L, (thresh << 2) & M_THRESH_L_MASK);
    if (result < 0) { return result; }

    LOG_INF("Inactivity threshold updated to %d mg.", thresholdMg);
    return 0;
  }

  int Adxl367::SetInactivityTime(uint16_t samples)
  {
    int result { writeRegister(M_REG_TIME_INACT_H, (samples >> 8) & 0xFF) };
    if (result < 0) { return result; }

    result = writeRegister(M_REG_TIME_INACT_L, samples & 0xFF);
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

  int Adxl367::ReadRegisterDebug(uint8_t reg, uint8_t& value)
  {
    return readRegister(reg, value);
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

  int Adxl367::readBurst(uint8_t reg, uint8_t* buffer, uint16_t length)
  {
    int result { i2c_write_read(m_i2c, m_i2cAddr, &reg, 1, buffer, length) };
    if (result < 0) {
      LOG_ERR("I2C burst read failed: reg=0x%02X, len=%u, err=%d!", reg, length, result);
      return result;
    }
    return 0;
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
        return M_SCALE_2G_MG;
      case Range::Range4g:
        return M_SCALE_4G_MG;
      case Range::Range8g:
        return M_SCALE_8G_MG;
      default:
        return M_SCALE_2G_MG;
    }
  }

} // namespace alc
