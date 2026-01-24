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
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app.hpp"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main()
{
  LOG_INF("===========================================");
  LOG_INF("  ALC MAILBOX MONITOR");
  LOG_INF("  Firmware Version: 0.1.0");
  LOG_INF("===========================================");
  
  // Create application instance.
  alc::App app;
  
  // Initialise hardware and subsystems.
  if (!app.Init()) {
    LOG_ERR("Application initialisation failed!");
    
    // On init failure, sleep briefly then reset.
    k_sleep(K_SECONDS(5));
    NVIC_SystemReset();
    
    return -1;
  }
  
  // Run the application (handles wake events, then enters System OFF).
  app.Run();
  
  // Should never reach here.
  return 0;
}
