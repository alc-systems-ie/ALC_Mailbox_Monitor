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
 * - Older buffered events sent with sms_suppress=true to suppress SMS flood
 * - Most recent event sent with actual sms_suppress value
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
 * @brief Event type for buffered events.
 */
enum class EventType : uint8_t {
    MailboxVisited = 0,   ///< Door opened and closed (delivery or collection).
    MailboxOpen = 1       ///< Door left open (escalating timer notification).
};

/**
 * @brief A single buffered mailbox event.
 */
struct BufferedEvent {
    uint32_t timestamp;       ///< Event time in seconds since boot (TODO: RTC epoch).
    bool sms_suppress;        ///< True to suppress SMS notification.
    EventType event_type;     ///< Type of event (visited or open).
    uint8_t door_open_stage;  ///< Escalation stage (1-3) for mailbox_open events.
};

/**
 * @brief State structure stored in NVS flash.
 *
 * Contains a circular buffer of mail delivery events that couldn't be
 * sent due to connectivity issues.
 */
struct RetainedState {
    static constexpr uint32_t MAGIC = 0x4D414954;  

    uint32_t magic;                                ///< Validity marker.
    uint8_t event_count;                           ///< Number of buffered events (0 to max).
    uint8_t max_events;                            ///< Current max buffer size (runtime config).
    bool enabled;                                  ///< Device operational state (false = provisioning mode).
    uint8_t door_open_stage;                       ///< Escalating door-open timer stage (0=inactive, 1-3=pending).
    uint16_t poll_interval;                        ///< Provisioning poll interval (seconds).

    // Persisted MQTT-configurable parameters.
    uint32_t mailWindowSecs;                       ///< Open/close cycle timeout (seconds).
    uint16_t activityThresholdMg;                  ///< ADXL367 activity threshold (mg).
    uint8_t activityTime;                          ///< ADXL367 activity time (samples).
    uint16_t inactivityThresholdMg;                ///< ADXL367 inactivity threshold (mg).
    uint8_t inactivityTime;                        ///< ADXL367 inactivity time (samples).

    int16_t homeX;                                 ///< Calibrated home position X (mg).
    int16_t homeY;                                 ///< Calibrated home position Y (mg).
    int16_t homeZ;                                 ///< Calibrated home position Z (mg).
    bool homeCalibrated;                           ///< True if home position has been calibrated.

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
 * @param smsSuppressed True to suppress SMS notification.
 * @param type Event type (MailboxVisited or MailboxOpen).
 */
void bufferMailEvent(uint32_t timestamp, bool smsSuppressed,
                     EventType type = EventType::MailboxVisited, uint8_t doorOpenStage = 0);

/**
 * @brief Save the current g_retained state to flash.
 *
 * Exposes the internal saveState() for use by app code that writes
 * directly to g_retained fields (e.g. persisted config parameters).
 */
void saveRetainedState();

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
 * @brief Set the door-open escalation stage.
 *
 * Persisted to flash across System OFF. Tracks which notification
 * in the escalating sequence has been sent (0=none, 1-3=stage).
 * Maximum 3 notifications: stage 1 (4 min), stage 2 (1 hr), stage 3 (2 hr).
 * Reset to 0 on door-close.
 *
 * @param stage Escalation stage (0-3).
 */
void setDoorOpenStage(uint8_t stage);

/**
 * @brief Get the door-open escalation stage.
 *
 * @return Current stage (0=inactive, 1-3=timer pending/sent).
 */
uint8_t getDoorOpenStage();

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

/**
 * @brief Store calibrated home position.
 */
void setHomePosition(int16_t x, int16_t y, int16_t z);

/**
 * @brief Retrieve calibrated home position.
 */
void getHomePosition(int16_t& x, int16_t& y, int16_t& z);

/**
 * @brief Check if home position has been calibrated.
 */
bool isHomeCalibrated();

} // namespace alc
