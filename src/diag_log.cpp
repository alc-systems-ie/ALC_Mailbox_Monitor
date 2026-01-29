#include "diag_log.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <string.h>

LOG_MODULE_REGISTER(diag_log, LOG_LEVEL_INF);

namespace alc
{

static DiagLog s_log;
static bool s_loaded = false;

static constexpr const char* SETTINGS_KEY = "diaglog/entries";

static int settingsSetHandler(const char* name, size_t len,
                               settings_read_cb read_cb, void* cb_arg)
{
    if (strcmp(name, "entries") == 0) {
        if (len != sizeof(DiagLog)) {
            LOG_WRN("Invalid diag log size: %zu (expected %zu)", len, sizeof(DiagLog));
            return -EINVAL;
        }

        int rc = read_cb(cb_arg, &s_log, sizeof(s_log));
        if (rc < 0) {
            LOG_ERR("Failed to read diag log: %d", rc);
            return rc;
        }

        if (s_log.magic == DiagLog::MAGIC) {
            s_loaded = true;
            LOG_INF("Loaded diag log: %d entries", s_log.count);
        } else {
            LOG_WRN("Invalid diag log magic: 0x%08X", s_log.magic);
            s_loaded = false;
        }

        return 0;
    }

    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(diaglog, "diaglog", NULL, settingsSetHandler, NULL, NULL);

static int saveLog()
{
    int rc = settings_save_one(SETTINGS_KEY, &s_log, sizeof(s_log));
    if (rc < 0) {
        LOG_ERR("Failed to save diag log: %d", rc);
    }
    return rc;
}

int diagLogInit()
{
    // Settings are already loaded by retainedInit() -> settings_load().
    // Our handler was called during that load. Just check if we got valid data.
    if (!s_loaded) {
        LOG_INF("No valid diag log - initialising empty.");
        s_log.magic = DiagLog::MAGIC;
        s_log.count = 0;
        s_log.writeIndex = 0;
        memset(s_log.entries, 0, sizeof(s_log.entries));
        saveLog();
    }

    return 0;
}

void diagLogEvent(const DiagLogEntry& entry)
{
    s_log.entries[s_log.writeIndex] = entry;
    s_log.writeIndex++;

    if (s_log.writeIndex >= DIAG_LOG_MAX_ENTRIES) {
        s_log.writeIndex = 0;
    }

    if (s_log.count < DIAG_LOG_MAX_ENTRIES) {
        s_log.count++;
    }

    saveLog();
}

uint8_t diagLogCount()
{
    return s_log.count;
}

bool diagLogGet(uint8_t index, DiagLogEntry& entry)
{
    if (index >= s_log.count) {
        return false;
    }

    // Map logical index (0=oldest) to physical index.
    uint8_t physIndex;
    if (s_log.count < DIAG_LOG_MAX_ENTRIES) {
        // Buffer not full yet — entries start at 0.
        physIndex = index;
    } else {
        // Buffer full — oldest is at writeIndex.
        physIndex = (s_log.writeIndex + index) % DIAG_LOG_MAX_ENTRIES;
    }

    entry = s_log.entries[physIndex];
    return true;
}

void diagLogClear()
{
    LOG_INF("Clearing diag log (%d entries).", s_log.count);
    s_log.count = 0;
    s_log.writeIndex = 0;
    memset(s_log.entries, 0, sizeof(s_log.entries));
    saveLog();
}

} // namespace alc
