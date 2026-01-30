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
  static bool s_initialised { false };
  static bool s_valid { false };

  // Settings key for our data.
  static constexpr const char* SETTINGS_KEY { "mailbox/events" };

  /**
   * @brief Settings handler for loading retained state.
   */
  static int settingsSetHandler(const char* name, size_t length, settings_read_cb readCallback, void* callbackArg)
  {
    if (strcmp(name, "events") == 0) 
    {
      if (length != sizeof(RetainedState)) {
        LOG_WRN("Invalid retained state size: %zu (expected %zu)", length, sizeof(RetainedState));
        return -EINVAL;
      }

      int result { readCallback(callbackArg, &g_retained, sizeof(g_retained)) };
      if (result < 0) {
        LOG_ERR("Failed to read retained state: %d", result);
        return result;
      }

      // Validate magic number.
      if (g_retained.magic == RetainedState::MAGIC) {
        s_valid = true;
        LOG_INF("Loaded retained state: %d buffered events (max %d), enabled=%s",
                      g_retained.event_count, g_retained.max_events,
                      g_retained.enabled ? "true" : "false");
      } else {
        LOG_WRN("Invalid magic in retained state: 0x%08X", g_retained.magic);
        s_valid = false;
      }

      return 0;
    }

    // Ignore legacy keys (e.g., "pending" from old firmware) to prevent error logs.
    if (strcmp(name, "pending") == 0) {
      LOG_INF("Ignoring legacy settings key: mailbox/%s", name);
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
    int result { settings_save_one(SETTINGS_KEY, &g_retained, sizeof(g_retained)) };
    if (result < 0) { LOG_ERR("Failed to save retained state: %d", result); }

    return result;
  }

  int retainedInit()
  {
    if (s_initialised) { return 0; } // Already initialised. 

    // Ensure settings subsystem is ready.
    int result { settings_subsys_init() };
    if (result < 0) {
      LOG_ERR("Settings subsystem init failed: %d", result);
      return result;
    }

    // Load settings (this will call our handler).
    result = settings_load();
    if (result < 0) {
      LOG_ERR("Settings load failed: %d", result);
      return result;
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
    g_retained.enabled = false;  // Start in provisioning mode.
    g_retained.door_open_stage = 0;
    g_retained.poll_interval = DEFAULT_POLL_INTERVAL;
    g_retained.mailWindowSecs = 240;  // M_MAIL_WINDOW_SECS default.
    g_retained.activityThresholdMg = 150;  // M_ACTIVITY_THRESHOLD_MG default.
    g_retained.activityTime = 1;  // M_ACTIVITY_TIME default.
    g_retained.inactivityThresholdMg = 250;  // M_INACTIVITY_THRESHOLD_MG default.
    g_retained.inactivityTime = 10;  // M_INACTIVITY_TIME default.
    g_retained.homeX = 0;
    g_retained.homeY = -1000;  // Default: gravity on -Y (vertical side-mount).
    g_retained.homeZ = 0;
    g_retained.homeCalibrated = false;
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
      memmove(&g_retained.events[0], &g_retained.events[excess], (g_retained.event_count - excess) * sizeof(BufferedEvent));

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

  void bufferMailEvent(uint32_t timestamp, bool smsSuppressed, EventType type, uint8_t doorOpenStage)
  {
    // If buffer is full, drop oldest event.
    if (g_retained.event_count >= g_retained.max_events) {
      // Shift all events left by one (dropping index 0).
      memmove(&g_retained.events[0], &g_retained.events[1], (g_retained.event_count - 1) * sizeof(BufferedEvent));
      g_retained.event_count--;
      LOG_INF("Buffer full - dropped oldest event.");
    }

    // Add new event at the end.
    BufferedEvent& newEvent = g_retained.events[g_retained.event_count];
    newEvent.timestamp = timestamp;
    newEvent.sms_suppress = smsSuppressed;
    newEvent.event_type = type;
    newEvent.door_open_stage = doorOpenStage;
    g_retained.event_count++;

    const char* typeStr = (type == EventType::MailboxOpen) ? "mailbox_open" : "mailbox_visited";
    LOG_INF("Buffered event %d: type=%s, timestamp=%u, sms_suppress=%d",
              g_retained.event_count, typeStr, timestamp, smsSuppressed);

    saveState();
  }

  void saveRetainedState()
  {
    saveState();
  }

  uint8_t getBufferedEventCount()
  {
    return g_retained.event_count;
  }

  bool getBufferedEvent(uint8_t index, BufferedEvent& event)
  {
    if (index >= g_retained.event_count) { return false; }

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

  void setDoorOpenStage(uint8_t stage)
  {
    if (stage > 3) { stage = 3; }

    if (g_retained.door_open_stage != stage) {
      g_retained.door_open_stage = stage;
      LOG_INF("Door-open stage set to %u.", stage);
      saveState();
    }
  }

  uint8_t getDoorOpenStage()
  {
    return g_retained.door_open_stage;
  }

  void setEnabled(bool enabled)
  {
    if (g_retained.enabled != enabled) {
      g_retained.enabled = enabled;
      LOG_INF("Device %s.", enabled ? "ENABLED" : "DISABLED");
      saveState();
    }
  }

  bool isEnabled()
  {
    return g_retained.enabled;
  }

  void setPollInterval(uint16_t seconds)
  {
    // Clamp to valid range (10-300 seconds).
    if (seconds < 10) {
      seconds = 10;
    } else if (seconds > 300) {
      seconds = 300;
    }

    g_retained.poll_interval = seconds;
    LOG_INF("Poll interval set to %u seconds.", seconds);
    saveState();
  }

  uint16_t getPollInterval()
  {
    return g_retained.poll_interval;
  }

  void setHomePosition(int16_t x, int16_t y, int16_t z)
  {
    g_retained.homeX = x;
    g_retained.homeY = y;
    g_retained.homeZ = z;
    g_retained.homeCalibrated = true;
    LOG_INF("Home position set: X=%d Y=%d Z=%d mg", x, y, z);
    saveState();
  }

  void getHomePosition(int16_t& x, int16_t& y, int16_t& z)
  {
    x = g_retained.homeX;
    y = g_retained.homeY;
    z = g_retained.homeZ;
  }

  bool isHomeCalibrated()
  {
    return g_retained.homeCalibrated;
  }

  } // namespace alc
