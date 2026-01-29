#pragma once

/**
 * @file diag_log.hpp
 * @brief Diagnostic wake log stored in NVS flash.
 *
 * Records every wake event with classification details in a 100-entry
 * circular buffer (~2.2 KB). Retrieved via MQTT dump_log command.
 */

#include <stdint.h>

namespace alc
{

enum class WakeClassification : uint8_t {
    Bump = 0,
    MailboxVisited = 1,
    DoorOpen = 2,
    TimerWake = 3,
    FreshBoot = 4,
    Provisioning = 5
};

struct DiagLogEntry {
    uint32_t timestamp;               // k_uptime_get() / 1000
    uint16_t awakePollMs;             // AWAKE poll duration (0 for bumps)
    int16_t settledX, settledY, settledZ;  // Settled position (mg)
    uint16_t batteryMv;               // Battery voltage
    uint8_t wakeSource;               // WakeSource enum
    WakeClassification classification;
    uint8_t doorOpenStage;            // Stage at time of event
    bool awakeAtBoot;                 // AWAKE pin state from main.cpp
    uint16_t activityThresholdMg;     // Active config
    uint8_t inactivityTime;           // Active config
};

static constexpr uint8_t DIAG_LOG_MAX_ENTRIES { 100 };

struct DiagLog {
    static constexpr uint32_t MAGIC = 0x444C4731;  // "DLG1"
    uint32_t magic;
    uint8_t count;        // Entries stored (0-100)
    uint8_t writeIndex;   // Next write position (circular)
    DiagLogEntry entries[DIAG_LOG_MAX_ENTRIES];
};

/**
 * @brief Initialise diagnostic log from NVS.
 * Must be called after settings_load() (i.e. after retainedInit()).
 */
int diagLogInit();

/**
 * @brief Add a diagnostic log entry and save to NVS.
 */
void diagLogEvent(const DiagLogEntry& entry);

/**
 * @brief Get number of stored entries.
 */
uint8_t diagLogCount();

/**
 * @brief Get entry by logical index (0 = oldest).
 */
bool diagLogGet(uint8_t index, DiagLogEntry& entry);

/**
 * @brief Clear all entries and save to NVS.
 */
void diagLogClear();

} // namespace alc
