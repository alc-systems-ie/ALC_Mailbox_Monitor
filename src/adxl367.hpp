#pragma once

/**
 * @file adxl367.hpp
 * @brief Simplified ADXL367 driver for ALC Mailbox Monitor.
 * 
 * Stripped-down version focused on:
 * - Wake-up mode operation (180nA)
 * - Activity detection for mailbox lid movement
 * - INT1 AWAKE signal for System OFF wake
 * - MQTT-configurable thresholds
 * 
 * Removed features not needed for mailbox monitoring:
 * - Tap detection
 * - Temperature sensing
 * - Data streaming
 * - GPIO interrupt callbacks (using System OFF wake instead)
 */

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

namespace alc 
{
  class Adxl367 
  {
    public:
      // ========== Enumerations ==========

      enum class I2cAddress : uint8_t {
        AddrHigh = 0x53,  ///< ASEL pin high (default on Thingy91x).
        AddrLow  = 0x1D   ///< ASEL pin low.
      };

      enum class Range : uint8_t {
        Range2g = 0,    ///< ±2g range (0.25 mg/LSB).
        Range4g = 1,    ///< ±4g range (0.5 mg/LSB).
        Range8g = 2     ///< ±8g range (1.0 mg/LSB).
      };

      enum class OperatingMode : uint8_t {
        Standby     = 0,  ///< Standby mode (40 nA).
        Measurement = 2   ///< Measurement mode.
      };

      enum class ActivityMode : uint8_t {
        Disabled   = 0,   ///< Detection disabled.
        Absolute   = 1,   ///< Absolute threshold detection.
        Referenced = 3    ///< Referenced (relative) detection.
      };

      enum class LinkLoopMode : uint8_t {
        Default = 0,    ///< Both enabled simultaneously.
        Linked  = 1,    ///< Sequential, interrupts require servicing.
        Loop    = 3     ///< Sequential, auto-acknowledged.
      };

      enum class WakeupRate : uint8_t {
        Rate12Sps  = 0,   ///< 12 samples/sec (fastest detection).
        Rate6Sps   = 1,   ///< 6 samples/sec (default).
        Rate3Sps   = 2,   ///< 3 samples/sec.
        Rate1_5Sps = 3    ///< 1.5 samples/sec (lowest power).
      };

      enum class ODR : uint8_t {
        Hz12_5 = 0,   ///< 12.5 Hz.
        Hz25   = 1,   ///< 25 Hz.
        Hz50   = 2,   ///< 50 Hz.
        Hz100  = 3,   ///< 100 Hz.
        Hz200  = 4,   ///< 200 Hz.
        Hz400  = 5    ///< 400 Hz.
      };

      enum class FifoMode : uint8_t {
        Disabled    = 0,  ///< FIFO disabled.
        OldestSaved = 1,  ///< Fills then stops.
        Stream      = 2,  ///< Always contains most recent data.
        Triggered   = 3   ///< Captures around activity event.
      };

      enum class IntPin : uint8_t {
        Int1 = 1,
        Int2 = 2
      };

      // ========== Structures ==========

      /**
       * @brief Device status register contents.
       */
      struct Status {
        bool dataReady;          ///< New data available.
        bool activityDetected;   ///< Activity detected.
        bool inactivityDetected; ///< Inactivity detected.
        bool awake;              ///< Device is in awake state.
      };

      /**
       * @brief Single XYZ sample from FIFO (converted to mg).
       */
      struct FifoSample {
        int16_t x;   ///< X-axis in mg.
        int16_t y;   ///< Y-axis in mg.
        int16_t z;   ///< Z-axis in mg.
      };

      /**
       * @brief Activity/inactivity configuration.
       * 
       * All threshold values are in mg (milli-g).
       * Time values are in samples at the current wake-up rate.
       */
      struct ActivityConfig {
        ActivityMode activityMode;    ///< Activity detection mode.
        ActivityMode inactivityMode;  ///< Inactivity detection mode.
        LinkLoopMode linkLoop;        ///< Link/loop mode.
        uint16_t activityThreshold;   ///< Activity threshold in mg.
        uint8_t activityTime;         ///< Activity time in samples.
        uint16_t inactivityThreshold; ///< Inactivity threshold in mg.
        uint16_t inactivityTime;      ///< Inactivity time in samples.
      };

      /**
       * @brief Default configuration suitable for mailbox monitoring.
       * 
       * Uses referenced activity (detects change from rest) and absolute
       * inactivity (returns to sleep when acceleration < 1g).
       */
      static constexpr ActivityConfig DEFAULT_MAILBOX_CONFIG {
        .activityMode = ActivityMode::Referenced,
        .inactivityMode = ActivityMode::Absolute,
        .linkLoop = LinkLoopMode::Loop,
        .activityThreshold = 250,     // 250mg - detect lid movement.
        .activityTime = 1,            // 1 sample.
        .inactivityThreshold = 1200,  // 1.2g - above gravity for absolute.
        .inactivityTime = 10          // ~1.6s at 6 SPS.
      };

      // ========== Constructor ==========

      /**
       * @brief Constructor.
       * @param i2cDev Pointer to I2C device.
       * @param addr I2C address selection (based on ASEL pin).
       */
      Adxl367(const struct device* i2cDev, I2cAddress addr = I2cAddress::AddrLow);

      // ========== Initialisation ==========

      /**
       * @brief Initialise the accelerometer.
       * 
       * Performs soft reset and verifies device ID.
       * 
       * @return 0 on success, negative error code on failure.
       */
      int Init();

      /**
       * @brief Perform software reset.
       * @return 0 on success, negative error code on failure.
       */
      int SoftReset();

      // ========== Operating Mode ==========

      /**
       * @brief Set operating mode.
       * @param mode Operating mode (Standby or Measurement).
       * @return 0 on success, negative error code on failure.
       */
      int SetOperatingMode(OperatingMode mode);

      /**
       * @brief Enable wake-up mode for ultra-low power operation.
       * 
       * In wake-up mode, the device samples at a reduced rate (6 SPS default)
       * and only outputs the AWAKE signal when activity is detected.
       * Power consumption: ~180 nA.
       * 
       * @param rate Wake-up sampling rate.
       * @return 0 on success, negative error code on failure.
       */
      int EnableWakeupMode(WakeupRate rate = WakeupRate::Rate6Sps);

      /**
       * @brief Disable wake-up mode (switch to full ODR measurement).
       * @return 0 on success, negative error code on failure.
       */
      int DisableWakeupMode();

      // ========== Configuration ==========

      /**
       * @brief Set measurement range.
       * @param range Measurement range (±2g, ±4g, or ±8g).
       * @return 0 on success, negative error code on failure.
       */
      int SetRange(Range range);

      /**
       * @brief Set output data rate.
       * @param odr Output data rate.
       * @return 0 on success, negative error code on failure.
       */
      int SetOdr(ODR odr);

      /**
       * @brief Configure activity/inactivity detection.
       * 
       * This is the main configuration for mailbox motion detection.
       * The thresholds can be adjusted via MQTT for tuning.
       * 
       * @param config Activity configuration.
       * @return 0 on success, negative error code on failure.
       */
      int ConfigureActivity(const ActivityConfig& config);

      /**
       * @brief Configure interrupt pin mapping.
       * 
       * For mailbox monitor, we map AWAKE to INT1 (active high).
       * This provides a level signal suitable for System OFF wake.
       * 
       * @param pin Interrupt pin (1 or 2).
       * @param awake Map AWAKE signal to pin.
       * @param activeLow True for active low, false for active high.
       * @return 0 on success, negative error code on failure.
       */
      int ConfigureInterrupt(IntPin pin, bool awake, bool activeLow = false);

      // ========== FIFO ==========

      /**
       * @brief Configure FIFO mode and channel selection.
       * @param mode FIFO operating mode.
       * @param storeXyz Store X, Y, Z channels (the only option we use).
       * @return 0 on success, negative error code on failure.
       */
      int ConfigureFifo(FifoMode mode);

      /**
       * @brief Read number of samples currently in FIFO.
       * @param entries Reference to store the count.
       * @return 0 on success, negative error code on failure.
       */
      int ReadFifoEntries(uint16_t& entries);

      /**
       * @brief Read all available FIFO data as XYZ sample sets.
       *
       * Reads FIFO_ENTRIES, then bulk-reads from I2C_FIFO_DATA (0x18).
       * Each sample is 2 bytes: D[15:14]=channel ID, D[13:0]=signed 14-bit data.
       * Samples arrive in X, Y, Z order (3 samples per set).
       *
       * @param samples Output buffer for decoded XYZ samples.
       * @param maxSets Maximum number of XYZ sets the buffer can hold.
       * @param setsRead Number of complete XYZ sets actually read.
       * @return 0 on success, negative error code on failure.
       */
      int ReadFifo(FifoSample* samples, uint16_t maxSets, uint16_t& setsRead);

      // ========== Status ==========

      /**
       * @brief Read device status.
       * 
       * Reading status clears the interrupt flags.
       * 
       * @param status Reference to Status structure.
       * @return 0 on success, negative error code on failure.
       */
      int ReadStatus(Status& status);

      /**
       * @brief Check if device is awake (motion detected).
       * @return True if awake (motion detected).
       */
      bool IsAwake();

      // ========== Threshold Updates (for MQTT tuning) ==========

      /**
       * @brief Update activity threshold.
       * 
       * Can be called while in measurement mode to adjust sensitivity.
       * 
       * @param thresholdMg Threshold in milli-g.
       * @return 0 on success, negative error code on failure.
       */
      int SetActivityThreshold(uint16_t thresholdMg);

      /**
       * @brief Update inactivity threshold.
       * @param thresholdMg Threshold in milli-g.
       * @return 0 on success, negative error code on failure.
       */
      int SetInactivityThreshold(uint16_t thresholdMg);

      /**
       * @brief Update inactivity time.
       * @param samples Number of samples (at wake-up rate).
       * @return 0 on success, negative error code on failure.
       */
      int SetInactivityTime(uint16_t samples);

      // ========== Debug ==========

      /**
       * @brief Print current configuration for debugging.
       */
      void PrintConfiguration();

    private:
      const struct device* m_i2c;
      uint8_t m_i2cAddr;
      Range m_currentRange;

      // I2C communication helpers.
      int writeRegister(uint8_t reg, uint8_t value);
      int readRegister(uint8_t reg, uint8_t& value);
      int updateRegister(uint8_t reg, uint8_t value, uint8_t mask);
      int readBurst(uint8_t reg, uint8_t* buffer, uint16_t length);

      // Conversion helpers.
      uint16_t mgToThreshold(uint16_t mg);
      float getScaleFactor();

      // Device verification.
      int verifyDeviceId();

      // Constants.
      static constexpr uint8_t STARTUP_DELAY_MS { 100 };
      static constexpr uint8_t RESET_DELAY_MS { 8 };
  };

} // namespace alc
