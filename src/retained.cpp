#include "retained.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <string.h>

LOG_MODULE_REGISTER(retained, LOG_LEVEL_INF);

namespace alc
{

// Global state instance (RAM copy, backed by flash).
RetainedState g_retained;

// Track if state has been loaded.
static bool s_initialised = false;
static bool s_valid = false;

// Settings key for our data.
static constexpr const char* SETTINGS_KEY = "mailbox/events";

/**
 * @brief Settings handler for loading retained state.
 */
static int settingsSetHandler(const char* name, size_t len,
                               settings_read_cb read_cb, void* cb_arg)
{
    if (strcmp(name, "events") == 0) {
        if (len != sizeof(RetainedState)) {
            LOG_WRN("Invalid retained state size: %zu (expected %zu)", len, sizeof(RetainedState));
            return -EINVAL;
        }

        int rc = read_cb(cb_arg, &g_retained, sizeof(g_retained));
        if (rc < 0) {
            LOG_ERR("Failed to read retained state: %d", rc);
            return rc;
        }

        // Validate magic number.
        if (g_retained.magic == RetainedState::MAGIC) {
            s_valid = true;
            LOG_INF("Loaded retained state: %d buffered events (max %d)",
                    g_retained.event_count, g_retained.max_events);
        } else {
            LOG_WRN("Invalid magic in retained state: 0x%08X", g_retained.magic);
            s_valid = false;
        }

        return 0;
    }

    return -ENOENT;
}

// Settings handler structure.
SETTINGS_STATIC_HANDLER_DEFINE(mailbox, "mailbox", NULL, settingsSetHandler, NULL, NULL);

/**
 * @brief Save current state to flash.
 */
static int saveState()
{
    int rc = settings_save_one(SETTINGS_KEY, &g_retained, sizeof(g_retained));
    if (rc < 0) {
        LOG_ERR("Failed to save retained state: %d", rc);
    }
    return rc;
}

int retainedInit()
{
    if (s_initialised) {
        return 0;  // Already initialised.
    }

    // Ensure settings subsystem is ready.
    int rc = settings_subsys_init();
    if (rc < 0) {
        LOG_ERR("Settings subsystem init failed: %d", rc);
        return rc;
    }

    // Load settings (this will call our handler).
    rc = settings_load();
    if (rc < 0) {
        LOG_ERR("Settings load failed: %d", rc);
        return rc;
    }

    s_initialised = true;

    // If state wasn't valid, initialise to defaults.
    if (!s_valid) {
        LOG_INF("No valid retained state - initialising defaults.");
        initRetainedState();
    }

    return 0;
}

bool hasValidRetainedState()
{
    return s_valid;
}

void initRetainedState()
{
    g_retained.magic = RetainedState::MAGIC;
    g_retained.event_count = 0;
    g_retained.max_events = DEFAULT_MAX_BUFFERED_EVENTS;
    memset(g_retained.events, 0, sizeof(g_retained.events));
    s_valid = true;

    saveState();
}

void setMaxBufferedEvents(uint8_t maxEvents)
{
    // Clamp to valid range.
    if (maxEvents < 1) {
        maxEvents = 1;
    } else if (maxEvents > BUFFER_HARDWARE_MAX) {
        maxEvents = BUFFER_HARDWARE_MAX;
    }

    g_retained.max_events = maxEvents;

    // If current count exceeds new max, trim oldest events.
    if (g_retained.event_count > maxEvents) {
        uint8_t excess = g_retained.event_count - maxEvents;

        // Shift events to remove oldest.
        memmove(&g_retained.events[0],
                &g_retained.events[excess],
                (g_retained.event_count - excess) * sizeof(BufferedEvent));

        g_retained.event_count = maxEvents;
        LOG_INF("Trimmed %d oldest events to fit new max.", excess);
    }

    LOG_INF("Max buffered events set to %d.", maxEvents);
    saveState();
}

uint8_t getMaxBufferedEvents()
{
    return g_retained.max_events;
}

void bufferMailEvent(uint32_t timestamp, bool ownerIntervened)
{
    // If buffer is full, drop oldest event.
    if (g_retained.event_count >= g_retained.max_events) {
        // Shift all events left by one (dropping index 0).
        memmove(&g_retained.events[0],
                &g_retained.events[1],
                (g_retained.event_count - 1) * sizeof(BufferedEvent));
        g_retained.event_count--;
        LOG_INF("Buffer full - dropped oldest event.");
    }

    // Add new event at the end.
    BufferedEvent& newEvent = g_retained.events[g_retained.event_count];
    newEvent.timestamp = timestamp;
    newEvent.owner_intervened = ownerIntervened;
    g_retained.event_count++;

    LOG_INF("Buffered event %d: timestamp=%u, owner_intervened=%d",
            g_retained.event_count, timestamp, ownerIntervened);

    saveState();
}

uint8_t getBufferedEventCount()
{
    return g_retained.event_count;
}

bool getBufferedEvent(uint8_t index, BufferedEvent& event)
{
    if (index >= g_retained.event_count) {
        return false;
    }

    event = g_retained.events[index];
    return true;
}

void clearBufferedEvents()
{
    if (g_retained.event_count > 0) {
        LOG_INF("Clearing %d buffered events.", g_retained.event_count);
        g_retained.event_count = 0;
        saveState();
    }
}

} // namespace alc
