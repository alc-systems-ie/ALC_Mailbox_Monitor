#include "orientation.hpp"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(orientation, LOG_LEVEL_DBG);

namespace alc
{

  OrientationDetector::OrientationDetector(Adxl367& accel)
      : m_accel(accel)
      , m_state{}
  {
  }

  void OrientationDetector::Init(const OrientationConfig& config)
  {
    m_state.config = config;
    m_state.reference.valid = false;
    m_state.doorOpen = false;
    resetStats();

    LOG_INF("OrientationDetector initialised:");
    LOG_INF("  Open threshold:  %d mg", config.openThresholdMg);
    LOG_INF("  Close threshold: %d mg", config.closeThresholdMg);
    LOG_INF("  Stable time:     %d ms", config.stableTimeMs);
    LOG_INF("  Sample interval: %d ms", config.samplingIntervalMs);
    LOG_INF("  Max open timeout: %d s", config.maxOpenTimeoutSecs);
  }

  int OrientationDetector::Calibrate(uint8_t numSamples)
  {
    LOG_INF("Calibrating closed position reference (%d samples)...", numSamples);

    int32_t sumX { 0 };
    int32_t sumY { 0 };
    int32_t sumZ { 0 };

    for (uint8_t i = 0; i < numSamples; ++i) {
      Adxl367::AxisData sample;
      int result { m_accel.ReadAxes(sample) };
      if (result < 0) {
        LOG_ERR("Calibration read failed at sample %d: %d", i, result);
        return result;
      }

      sumX += sample.x;
      sumY += sample.y;
      sumZ += sample.z;

      LOG_DBG("  Sample %d: X=%d, Y=%d, Z=%d", i, sample.x, sample.y, sample.z);

      // Small delay between samples for stability.
      k_msleep(50);
    }

    m_state.reference.x = static_cast<int16_t>(sumX / numSamples);
    m_state.reference.y = static_cast<int16_t>(sumY / numSamples);
    m_state.reference.z = static_cast<int16_t>(sumZ / numSamples);
    m_state.reference.valid = true;

    LOG_INF("Calibration complete. Reference: X=%d, Y=%d, Z=%d mg",
            m_state.reference.x, m_state.reference.y, m_state.reference.z);

    return 0;
  }

  void OrientationDetector::SetReference(const OrientationReference& ref)
  {
    m_state.reference = ref;
    LOG_INF("Reference set externally: X=%d, Y=%d, Z=%d mg (valid=%d)",
            ref.x, ref.y, ref.z, ref.valid);
  }

  uint16_t OrientationDetector::CalculateDeviation(const Adxl367::AxisData& current) const
  {
    if (!m_state.reference.valid) {
      return 0;
    }

    // Calculate Euclidean distance from reference.
    int32_t dx { current.x - m_state.reference.x };
    int32_t dy { current.y - m_state.reference.y };
    int32_t dz { current.z - m_state.reference.z };

    // Use integer sqrt approximation to avoid floating point.
    // sqrt(dx^2 + dy^2 + dz^2)
    int64_t sumSquares { dx * dx + dy * dy + dz * dz };

    // Simple integer square root (Newton's method).
    if (sumSquares == 0) {
      return 0;
    }

    int64_t x { sumSquares };
    int64_t y { (x + 1) / 2 };
    while (y < x) {
      x = y;
      y = (x + sumSquares / x) / 2;
    }

    return static_cast<uint16_t>(x);
  }

  bool OrientationDetector::isOpenPosition(const Adxl367::AxisData& current) const
  {
    return CalculateDeviation(current) >= m_state.config.openThresholdMg;
  }

  bool OrientationDetector::isClosedPosition(const Adxl367::AxisData& current) const
  {
    return CalculateDeviation(current) <= m_state.config.closeThresholdMg;
  }

  void OrientationDetector::resetStats()
  {
    m_state.lastStats = OrientationStats{};
    m_state.lastStats.minX = INT16_MAX;
    m_state.lastStats.maxX = INT16_MIN;
    m_state.lastStats.minY = INT16_MAX;
    m_state.lastStats.maxY = INT16_MIN;
    m_state.lastStats.minZ = INT16_MAX;
    m_state.lastStats.maxZ = INT16_MIN;
  }

  void OrientationDetector::updateStats(const Adxl367::AxisData& sample)
  {
    m_state.lastStats.sampleCount++;

    // Update min/max for each axis.
    if (sample.x < m_state.lastStats.minX) { m_state.lastStats.minX = sample.x; }
    if (sample.x > m_state.lastStats.maxX) { m_state.lastStats.maxX = sample.x; }
    if (sample.y < m_state.lastStats.minY) { m_state.lastStats.minY = sample.y; }
    if (sample.y > m_state.lastStats.maxY) { m_state.lastStats.maxY = sample.y; }
    if (sample.z < m_state.lastStats.minZ) { m_state.lastStats.minZ = sample.z; }
    if (sample.z > m_state.lastStats.maxZ) { m_state.lastStats.maxZ = sample.z; }

    // Update max deviation.
    uint16_t deviation { CalculateDeviation(sample) };
    if (deviation > m_state.lastStats.maxDeviationMg) {
      m_state.lastStats.maxDeviationMg = static_cast<int16_t>(deviation);
    }
  }

  void OrientationDetector::LogCurrentAxes()
  {
    Adxl367::AxisData current;
    int result { m_accel.ReadAxes(current) };
    if (result < 0) {
      LOG_ERR("Failed to read axes: %d", result);
      return;
    }

    uint16_t deviation { CalculateDeviation(current) };
    bool awake { m_accel.IsAwake() };

    // Log in CSV-friendly format for easy analysis.
    // Format: timestamp_ms, x, y, z, deviation, awake
    LOG_INF("AXIS,%lld,%d,%d,%d,%d,%d",
            k_uptime_get(), current.x, current.y, current.z, deviation, awake ? 1 : 0);
  }

  OrientationResult OrientationDetector::RunDetectionCycle()
  {
    LOG_INF("Starting orientation detection cycle...");

    if (!m_state.reference.valid) {
      LOG_WRN("No reference calibration - performing auto-calibration");
      // Auto-calibrate assumes current position is closed.
      // This is a fallback; proper calibration should be done during provisioning.
      int result { Calibrate() };
      if (result < 0) {
        return OrientationResult::Error;
      }
      return OrientationResult::CalibrationDone;
    }

    resetStats();

    int64_t startTime { k_uptime_get() };
    int64_t openDetectedTime { 0 };
    int64_t stableStartTime { 0 };
    bool doorOpened { false };
    bool wasInClosedPosition { true };

    const uint32_t timeoutMs { m_state.config.maxOpenTimeoutSecs * 1000 };

    // OPTIMISATION: This polling loop could be replaced with FIFO-based
    // sampling using watermark interrupts. The ADXL367 has a 512-sample
    // FIFO that could buffer data while we sleep between reads.

    while (true) {
      Adxl367::AxisData current;
      int result { m_accel.ReadAxes(current) };
      if (result < 0) {
        LOG_ERR("Axis read failed: %d", result);
        return OrientationResult::Error;
      }

      updateStats(current);

      uint16_t deviation { CalculateDeviation(current) };
      bool inOpenPosition { isOpenPosition(current) };
      bool inClosedPosition { isClosedPosition(current) };
      int64_t now { k_uptime_get() };
      uint32_t elapsedMs { static_cast<uint32_t>(now - startTime) };

      // Log every sample for data capture.
      // OPTIMISATION: Reduce logging frequency in production.
      LOG_DBG("SAMPLE: t=%u, X=%d, Y=%d, Z=%d, dev=%d, open=%d, closed=%d",
              elapsedMs, current.x, current.y, current.z,
              deviation, inOpenPosition ? 1 : 0, inClosedPosition ? 1 : 0);

      // State machine for door position.
      if (!doorOpened) {
        // Looking for door to open.
        if (inOpenPosition) {
          doorOpened = true;
          openDetectedTime = now;
          m_state.lastStats.openDetected = true;
          LOG_INF("OPEN detected at t=%u ms, deviation=%d mg", elapsedMs, deviation);
        }
      } else {
        // Door is open - looking for close.
        if (inClosedPosition) {
          if (!wasInClosedPosition) {
            // Just entered closed position - start stability timer.
            stableStartTime = now;
            LOG_DBG("Entered closed position, starting stability timer");
          }

          // Check if stable long enough.
          if ((now - stableStartTime) >= m_state.config.stableTimeMs) {
            m_state.lastStats.closeDetected = true;
            m_state.lastStats.durationMs = elapsedMs;
            LOG_INF("CLOSE detected at t=%u ms (stable for %d ms)",
                    elapsedMs, m_state.config.stableTimeMs);
            return OrientationResult::MailDelivered;
          }
        } else {
          // Reset stability timer if we leave closed position.
          stableStartTime = now;
        }

        wasInClosedPosition = inClosedPosition;
      }

      // Check for timeout.
      if (elapsedMs >= timeoutMs) {
        m_state.lastStats.durationMs = elapsedMs;
        if (doorOpened) {
          LOG_WRN("Timeout - door left open for %u ms", elapsedMs);
          return OrientationResult::DoorLeftOpen;
        } else {
          LOG_INF("Timeout - no significant door movement detected");
          return OrientationResult::SpuriousMotion;
        }
      }

      // Check if accelerometer has returned to inactive (no more motion).
      // This is a secondary exit condition if the door never opened significantly.
      if (!doorOpened && !m_accel.IsAwake()) {
        m_state.lastStats.durationMs = elapsedMs;
        LOG_INF("ADXL367 returned to inactive without open detection (t=%u ms)", elapsedMs);
        return OrientationResult::SpuriousMotion;
      }

      // OPTIMISATION: Instead of k_msleep, could use FIFO watermark
      // interrupt and k_sem_take with timeout.
      k_msleep(m_state.config.samplingIntervalMs);
    }
  }

} // namespace alc
