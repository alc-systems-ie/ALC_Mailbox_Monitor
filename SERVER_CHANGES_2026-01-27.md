# Server-Side Changes Required - 2026-01-27

## Summary

Firmware updates to the ALC Mailbox Monitor require corresponding server-side modifications to handle new message formats and features.

---

## 1. Event Message Format Change

### Previous Format
```json
{
  "event": "mail_delivered"
}
```

### New Format
```json
{
  "event": "mail_delivered",
  "timestamp": 12345,
  "owner_intervened": false
}
```

### Field Definitions

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `event` | string | Yes | Always `"mail_delivered"` |
| `timestamp` | uint32 | Yes | Seconds since device boot (temporary until RTC fitted) |
| `owner_intervened` | bool | Yes | If `true`, suppress SMS notification to carers |

### Server Action Required
- Update event parser to accept new fields
- **IMPORTANT:** When `owner_intervened` is `true`, do NOT send SMS notification
- The `timestamp` field is currently relative (uptime), not absolute epoch time - store but don't rely on it for accurate timing until RTC is fitted

---

## 2. Event Buffering & SMS Flood Prevention

### Behaviour
When the device experiences connectivity issues, mail delivery events are buffered locally (up to 10 events, configurable to 20). On next successful connection, all buffered events are sent.

### SMS Flood Prevention Logic
- **Older buffered events:** Sent with `owner_intervened: true` → No SMS
- **Most recent event:** Sent with actual `owner_intervened` value → SMS if `false`

### Server Action Required
- No special handling needed - just respect the `owner_intervened` flag
- Multiple events may arrive in quick succession after an outage

---

## 3. New MQTT Command: `reset_device`

### Command Topic
```
alc/{DEVICE_ID}/commands
```

### Command Format
```json
{"reset_device": true}
```

### Behaviour
- Device waits for current open/close cycle to complete (up to 240 seconds)
- Performs clean modem shutdown
- Executes full system reboot
- Buffered events are preserved across reset

### Server Action Required
- Add UI/API capability to send this command if remote device reset is needed
- Use as retained message so device receives on next connection

---

## 4. New Configurable Parameter: `max_buffered_events`

### Command Format
```json
{"max_buffered_events": 15}
```

### Valid Range
- Minimum: 1
- Maximum: 20
- Default: 10

### Status Response
The status message now includes:
```json
{
  "mail_window": 240,
  "activity_threshold": 250,
  "activity_time": 1,
  "inactivity_threshold": 1200,
  "inactivity_time": 10,
  "max_buffered_events": 10,
  "buffered_events": 0
}
```

### Server Action Required
- Update status message parser to handle new fields
- Optionally add UI to configure `max_buffered_events`

---

## 5. Network Mode Change: NB-IoT → LTE-M

### Impact
- Faster connection times (1-5 seconds vs 10-30+ seconds)
- More reliable connections
- No server-side changes required

---

## MQTT Topics Reference

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `alc/{DEVICE_ID}/events` | Device → Server | Mail delivery events |
| `alc/{DEVICE_ID}/battery` | Device → Server | Battery status |
| `alc/{DEVICE_ID}/status` | Device → Server | Configuration status |
| `alc/{DEVICE_ID}/commands` | Server → Device | Configuration commands |

---

## Migration Notes

1. **Backwards Compatibility:** The new event format includes additional fields. Ensure parser handles both old (if any old firmware devices exist) and new formats gracefully.

2. **Testing:** Send `{"reset_device": true}` as a retained message to test the reset functionality. Device will reset on next wake/connection.

3. **SMS Logic:** Critical change - always check `owner_intervened` before sending SMS notifications.

---

## Commits (chronological)

1. `db48e60` - Add LTE-M support and NVS flash event buffering
2. `44f3f80` - Improve modem error handling after connection timeout
3. `208f467` - Ignore legacy settings key to prevent error log
4. `11def77` - Add remote device reset command
5. `f18e145` - Document event message format fields for server reference
