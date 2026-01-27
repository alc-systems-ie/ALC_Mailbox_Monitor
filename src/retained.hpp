#pragma once

/**
 * @file retained.hpp
 * @brief Buffered event storage that persists across System OFF via NVS flash.
 *
 * IMPORTANT: nRF91 series does NOT support RAM retention in System OFF mode.
 * This module uses NVS (Non-Volatile Storage) in flash to persist events.
 *
 * Design Philosophy:
 * - Buffer mail delivery events locally on every CLOSE event
 * - Only attempt to send when LTE connection succeeds
 * - If connection fails, events remain buffered for next successful connection
 * - This keeps the OPEN/CLOSE state machine completely separate from connectivity
 *
 * Buffered Event Handling:
 * - On CLOSE event: Add event to buffer with timestamp, save to NVS
 * - On successful LTE connection: Send all buffered events, then clear buffer
 * - Older buffered events sent with owner_intervened=true to suppress SMS flood
 * - Most recent event sent with actual owner_intervened value
 *
 * Timestamps:
 * - Currently uses uptime in seconds (relative to boot)
 * - TODO: Replace with RTC epoch time once RTC hardware is fitted
 *
 * Usage:
 * - Call retainedInit() once at startup to load state from flash
 * - Call bufferMailEvent() on each CLOSE event
 * - Call getBufferedEventCount() to check if events need sending
 * - Call getBufferedEvent() to retrieve events for sending
 * - Call clearBufferedEvents() after successful transmission
 */

#include <stdint.h>

namespace alc
{

// Hardware limit for buffer size (compile-time maximum).
// Runtime limit is configurable via MQTT up to this value.
static constexpr uint8_t BUFFER_HARDWARE_MAX { 20 };

// Default buffer size.
static constexpr uint8_t DEFAULT_MAX_BUFFERED_EVENTS { 10 };

// Default provisioning poll interval (seconds).
static constexpr uint16_t DEFAULT_POLL_INTERVAL { 60 };

/**
 * @brief A single buffered mail delivery event.
 */
struct BufferedEvent {
    uint32_t timestamp;       ///< Event time in seconds since boot (TODO: RTC epoch).
    bool owner_intervened;    ///< True if owner was present (hall sensor, future).
};

/**
 * @brief State structure stored in NVS flash.
 *
 * Contains a circular buffer of mail delivery events that couldn't be
 * sent due to connectivity issues.
 */
struct RetainedState {
    static constexpr uint32_t MAGIC = 0x4D41494D;  // "MAIM" - version 2 with provisioning.

    uint32_t magic;                                ///< Validity marker.
    uint8_t event_count;                           ///< Number of buffered events (0 to max).
    uint8_t max_events;                            ///< Current max buffer size (runtime config).
    bool enabled;                                  ///< Device operational state (false = provisioning mode).
    uint8_t reserved;                              ///< Padding for alignment.
    uint16_t poll_interval;                        ///< Provisioning poll interval (seconds).
    BufferedEvent events[BUFFER_HARDWARE_MAX];     ///< Event buffer, oldest at index 0.
};

/**
 * @brief Global retained state instance (in RAM, backed by flash).
 */
extern RetainedState g_retained;

/**
 * @brief Initialise the retained state subsystem.
 *
 * Loads state from flash if valid, otherwise initialises to defaults.
 * Must be called once at startup before using other functions.
 *
 * @return 0 on success, negative error code on failure.
 */
int retainedInit();

/**
 * @brief Check if retained state contains valid data.
 *
 * @return true if state was loaded successfully from flash.
 */
bool hasValidRetainedState();

/**
 * @brief Initialise retained state to default values and save to flash.
 */
void initRetainedState();

/**
 * @brief Set the maximum number of events to buffer.
 *
 * This is a runtime configuration option, typically set via MQTT.
 * The value is clamped to 1-BUFFER_HARDWARE_MAX.
 *
 * @param maxEvents Maximum events to buffer (1-20).
 */
void setMaxBufferedEvents(uint8_t maxEvents);

/**
 * @brief Get the current maximum buffer size.
 *
 * @return Current max events setting.
 */
uint8_t getMaxBufferedEvents();

/**
 * @brief Buffer a mail delivery event.
 *
 * Adds an event to the buffer and saves to flash. If the buffer is full,
 * the oldest event is dropped to make room.
 *
 * @param timestamp Event timestamp (seconds since boot, TODO: RTC epoch).
 * @param ownerIntervened True if owner was present during delivery.
 */
void bufferMailEvent(uint32_t timestamp, bool ownerIntervened);

/**
 * @brief Get the number of buffered events waiting to be sent.
 *
 * @return Number of events in buffer (0 to max_events).
 */
uint8_t getBufferedEventCount();

/**
 * @brief Get a buffered event by index.
 *
 * @param index Event index (0 = oldest, count-1 = newest).
 * @param event Output parameter for the event data.
 * @return true if index is valid and event was retrieved.
 */
bool getBufferedEvent(uint8_t index, BufferedEvent& event);

/**
 * @brief Clear all buffered events after successful transmission.
 *
 * Resets the event count to zero and saves to flash.
 */
void clearBufferedEvents();

/**
 * @brief Check if the event buffer has any events waiting.
 *
 * @return true if there are events to send.
 */
inline bool hasBufferedEvents()
{
    return g_retained.event_count > 0;
}

/**
 * @brief Set the device enabled state.
 *
 * When disabled (false), device operates in provisioning mode, polling
 * periodically for an enable command. When enabled (true), device operates
 * normally, detecting mail delivery events.
 *
 * @param enabled True to enable normal operation, false for provisioning mode.
 */
void setEnabled(bool enabled);

/**
 * @brief Get the device enabled state.
 *
 * @return true if device is enabled for normal operation.
 */
bool isEnabled();

/**
 * @brief Set the provisioning poll interval.
 *
 * Determines how often the device wakes in provisioning mode to check
 * for enable commands.
 *
 * @param seconds Poll interval in seconds (clamped to valid range).
 */
void setPollInterval(uint16_t seconds);

/**
 * @brief Get the provisioning poll interval.
 *
 * @return Poll interval in seconds.
 */
uint16_t getPollInterval();

} // namespace alc
