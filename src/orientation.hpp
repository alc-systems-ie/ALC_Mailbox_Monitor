#pragma once

/**
 * @file orientation.hpp
 * @brief Experimental orientation-based door position detection.
 *
 * This module provides an alternative to the two-event open/close detection
 * by continuously sampling accelerometer data during the awake period to
 * determine door position from orientation.
 *
 * EXPERIMENTAL: This is a work-in-progress for field testing and threshold
 * determination. The API and approach may change significantly.
 *
 * Design goals:
 * - Capture raw data for analysis and threshold tuning
 * - Detect door open based on orientation change from closed reference
 * - Detect door close when orientation returns to reference
 * - Handle timeout if door left open
 *
 * OPTIMISATION NOTES:
 * - Currently uses polling; could be converted to FIFO watermark interrupts
 * - Reference position could be stored in NVS after calibration
 * - Sampling rate and thresholds are candidates for MQTT configuration
 */

#include "adxl367.hpp"
#include <cstdint>

namespace alc
{
  /**
   * @brief Orientation detection configuration.
   *
   * OPTIMISATION: These could be made MQTT-configurable like ActivityConfig.
   */
  struct OrientationConfig {
    uint16_t openThresholdMg;      ///< Deviation from closed to detect open (mg).
    uint16_t closeThresholdMg;     ///< Max deviation from closed to detect close (mg).
    uint16_t stableTimeMs;         ///< Time orientation must be stable (ms).
    uint16_t samplingIntervalMs;   ///< Polling interval (ms).
    uint32_t maxOpenTimeoutSecs;   ///< Max time door can be open before timeout.
  };

  /**
   * @brief Default configuration for initial testing.
   *
   * These values are guesses - field testing will refine them.
   */
  static constexpr OrientationConfig DEFAULT_ORIENTATION_CONFIG {
    .openThresholdMg = 200,       // 200mg deviation = ~11.5 degrees from vertical
    .closeThresholdMg = 100,      // Must return within 100mg of reference
    .stableTimeMs = 500,          // Must be stable for 500ms
    .samplingIntervalMs = 50,     // 20 Hz sampling (OPTIMISATION: use FIFO)
    .maxOpenTimeoutSecs = 300     // 5 minute timeout if left open
  };

  /**
   * @brief Reference orientation for closed door position.
   *
   * Captured during calibration or first boot. Represents the expected
   * accelerometer readings when the mailbox door is closed.
   */
  struct OrientationReference {
    int16_t x;      ///< Expected X-axis reading (mg).
    int16_t y;      ///< Expected Y-axis reading (mg).
    int16_t z;      ///< Expected Z-axis reading (mg).
    bool valid;     ///< True if reference has been calibrated.
  };

  /**
   * @brief Result of orientation detection cycle.
   */
  enum class OrientationResult {
    MailDelivered,    ///< Door opened and closed - mail delivery detected.
    DoorLeftOpen,     ///< Door opened but timeout before close.
    SpuriousMotion,   ///< Motion detected but no significant orientation change.
    CalibrationDone,  ///< Calibration capture completed.
    Error             ///< Detection failed due to error.
  };

  /**
   * @brief Statistics captured during a detection cycle.
   *
   * Used for data analysis and threshold tuning.
   */
  struct OrientationStats {
    uint32_t sampleCount;         ///< Total samples taken.
    uint32_t durationMs;          ///< Total cycle duration.
    int16_t maxDeviationMg;       ///< Maximum deviation from reference.
    int16_t minX, maxX;           ///< X-axis range during cycle.
    int16_t minY, maxY;           ///< Y-axis range during cycle.
    int16_t minZ, maxZ;           ///< Z-axis range during cycle.
    bool openDetected;            ///< Whether open threshold was exceeded.
    bool closeDetected;           ///< Whether close was detected after open.
  };

  /**
   * @brief Experimental state for orientation detection.
   *
   * OPTIMISATION: For production, this could be merged into RetainedState
   * and persisted to NVS. Currently kept separate for experimentation.
   */
  struct OrientationState {
    OrientationReference reference;   ///< Closed door reference position.
    OrientationConfig config;         ///< Detection configuration.
    OrientationStats lastStats;       ///< Stats from last detection cycle.
    bool doorOpen;                    ///< Current door state (for NVS persistence).
  };

  /**
   * @brief Orientation-based door position detector.
   *
   * Usage:
   * 1. Create instance with accelerometer reference
   * 2. Call Calibrate() on first boot or when requested
   * 3. Call RunDetectionCycle() when motion wakes device
   * 4. Check result and stats for analysis
   */
  class OrientationDetector
  {
  public:
    /**
     * @brief Constructor.
     * @param accel Reference to initialised ADXL367 driver.
     */
    explicit OrientationDetector(Adxl367& accel);

    /**
     * @brief Initialise with configuration.
     * @param config Detection configuration.
     */
    void Init(const OrientationConfig& config = DEFAULT_ORIENTATION_CONFIG);

    /**
     * @brief Calibrate closed door reference position.
     *
     * Takes multiple samples and averages to establish the reference
     * orientation for the closed door position. Should be called when
     * the door is known to be closed.
     *
     * @param numSamples Number of samples to average (default 10).
     * @return 0 on success, negative error code on failure.
     */
    int Calibrate(uint8_t numSamples = 10);

    /**
     * @brief Set reference from external source (e.g., NVS).
     * @param ref Reference orientation.
     */
    void SetReference(const OrientationReference& ref);

    /**
     * @brief Get current reference (for NVS storage).
     * @return Current reference orientation.
     */
    const OrientationReference& GetReference() const { return m_state.reference; }

    /**
     * @brief Check if reference is valid/calibrated.
     * @return True if reference has been set.
     */
    bool IsCalibrated() const { return m_state.reference.valid; }

    /**
     * @brief Run a complete detection cycle.
     *
     * This is the main entry point after motion wake. It:
     * 1. Samples orientation continuously while AWAKE
     * 2. Detects door open when deviation exceeds threshold
     * 3. Detects door close when orientation returns to reference
     * 4. Times out if door left open too long
     *
     * OPTIMISATION: Currently polls ReadAxes(). Could use FIFO with
     * watermark interrupt for lower power during long open periods.
     *
     * @return Detection result.
     */
    OrientationResult RunDetectionCycle();

    /**
     * @brief Get statistics from last detection cycle.
     * @return Stats structure with captured data.
     */
    const OrientationStats& GetLastStats() const { return m_state.lastStats; }

    /**
     * @brief Log current axis data (for debugging/analysis).
     *
     * Reads and logs current X, Y, Z values. Call this in a loop
     * to capture raw data for threshold analysis.
     */
    void LogCurrentAxes();

    /**
     * @brief Calculate deviation from reference.
     * @param current Current axis reading.
     * @return Euclidean distance in mg.
     */
    uint16_t CalculateDeviation(const Adxl367::AxisData& current) const;

  private:
    Adxl367& m_accel;
    OrientationState m_state;

    /**
     * @brief Update statistics with new sample.
     */
    void updateStats(const Adxl367::AxisData& sample);

    /**
     * @brief Reset statistics for new cycle.
     */
    void resetStats();

    /**
     * @brief Check if current orientation matches closed reference.
     * @param current Current axis reading.
     * @return True if within closeThresholdMg of reference.
     */
    bool isClosedPosition(const Adxl367::AxisData& current) const;

    /**
     * @brief Check if current orientation indicates open door.
     * @param current Current axis reading.
     * @return True if deviation exceeds openThresholdMg.
     */
    bool isOpenPosition(const Adxl367::AxisData& current) const;
  };

} // namespace alc
