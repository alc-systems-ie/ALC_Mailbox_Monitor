/*
 * @file main.cpp
 * @brief ALC Mailbox Monitor - Main Entry Point.
 *
 * Ultra-low power mailbox monitoring using:
 * - nRF9151 with NB-IoT + PSM for cellular connectivity
 * - ADXL367 in wake-up mode for motion detection
 * - System OFF for minimum power consumption
 *
 * Device ID prefix: cccc (ALC_Mailbox_Monitor series)
 *
 * IMPORTANT: Wake source detection happens BEFORE App construction
 * to ensure GPIO latches are read before any driver init.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_power.h>

#include "app.hpp"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// GPIO pins for wake detection (must match app.hpp).
static constexpr uint32_t PIN_ACCEL_INT { 11 };   // ADXL367 INT1.
static constexpr uint32_t PIN_PMIC_INT { 2 };     // nPM1300 GPIO (future timer wake).

/**
 * @brief Identify wake source BEFORE any driver/class initialisation.
 *
 * This MUST be called before the App object is constructed, as
 * constructor initialisation of hardware objects can affect GPIO states.
 */
// Captured before any driver init — true if AWAKE pin was still HIGH at boot.
static bool s_awakeAtBoot { false };

static alc::WakeSource identifyWakeSourceEarly()
{
  uint32_t resetReason { nrf_power_resetreas_get(NRF_POWER_NS) };

  // Clear reset reason after reading.
  nrf_power_resetreas_clear(NRF_POWER_NS, resetReason);

  LOG_INF("Reset reason: 0x%08X", resetReason);

  // If not from System OFF, it's a fresh boot.
  if (!(resetReason & POWER_RESETREAS_OFF_Msk)) {
    LOG_INF("Fresh boot (not System OFF wake).");
    return alc::WakeSource::PowerOn;
  }

  LOG_INF("System OFF wake detected. Checking GPIO latches...");

  // Check accelerometer latch (P0.11) - MUST check before any I2C activity.
  if (nrf_gpio_pin_latch_get(PIN_ACCEL_INT)) {
    LOG_INF("  Accelerometer latch SET (P0.%d)", PIN_ACCEL_INT);
    // Read raw pin level BEFORE any driver init resets the ADXL367.
    // If pin is still HIGH, AWAKE is active (motion ongoing).
    // If LOW, motion was brief but real (latch proves rising edge occurred).
    uint32_t pinLevel { nrf_gpio_pin_read(PIN_ACCEL_INT) };
    s_awakeAtBoot = (pinLevel != 0);
    LOG_INF("  INT1 pin level: %u (AWAKE %s)", pinLevel,
            pinLevel ? "ACTIVE" : "cleared");
    nrf_gpio_pin_latch_clear(PIN_ACCEL_INT);
    return alc::WakeSource::Accelerometer;
  }

  // Check PMIC/Timer latch (P0.02) - future heartbeat wake.
  if (nrf_gpio_pin_latch_get(PIN_PMIC_INT)) {
    LOG_INF("  PMIC/Timer latch SET (P0.%d)", PIN_PMIC_INT);
    nrf_gpio_pin_latch_clear(PIN_PMIC_INT);
    return alc::WakeSource::Timer;
  }

  // System OFF wake but no latch identified.
  LOG_WRN("System OFF wake but no GPIO latch set!");
  return alc::WakeSource::Unknown;
}

int main()
{
  LOG_INF("===========================================");
  LOG_INF("  ALC MAILBOX MONITOR");
  LOG_INF("  Firmware Version: 0.1.0");
  LOG_INF("===========================================");

  // =========================================================================
  // CRITICAL: Detect wake source BEFORE App construction.
  // The App constructor initialises hardware objects which can affect GPIOs.
  // =========================================================================
  alc::WakeSource wake { identifyWakeSourceEarly() };
  LOG_INF("Wake source: %s", alc::wakeSourceToString(wake));

  // Create application instance and start with detected wake source.
  // Start() does not return - it enters System OFF at the end.
  alc::App app;
  app.Start(wake, s_awakeAtBoot);

  // Should never reach here.
  return 0;
}
