# Server-Side Changes for Provisioning Mode

**Date:** 2026-01-27
**Commit:** c89f3ec
**Feature:** Device Provisioning Mode

## Overview

Devices now start in **provisioning mode** when first powered on. They poll periodically for an enable command rather than detecting mail events. This allows controlled deployment and activation of devices.

## New MQTT Commands

Commands should be published as **retained messages** to: `alc/{DEVICE_ID}/commands`

| Command | JSON | Description |
|---------|------|-------------|
| Enable | `{"enable": true}` | Activate device, exit provisioning mode |
| Disable | `{"disable": true}` | Deactivate device, enter provisioning mode on next wake |
| Set Poll Interval | `{"poll_interval": 30}` | Set provisioning poll interval (10-300 seconds) |

**Note:** Enable and disable commands are automatically cleared (empty retained message published) after processing to prevent repeated execution.

## Status Message Changes

Status topic: `alc/{DEVICE_ID}/status`

### New Fields

| Field | Type | Description |
|-------|------|-------------|
| `enabled` | bool | `true` when device is operational, `false` in provisioning mode |
| `provisioning` | bool | Inverse of `enabled` for convenience |
| `poll_interval` | uint16 | Wake interval in provisioning mode (seconds) |

### Updated JSON Format

```json
{
  "enabled": true,
  "provisioning": false,
  "poll_interval": 60,
  "mail_window": 240,
  "activity_threshold": 250,
  "activity_time": 1,
  "inactivity_threshold": 1200,
  "inactivity_time": 10,
  "max_buffered_events": 10,
  "buffered_events": 0
}
```

## Device Reset Behaviour Change

The `{"reset_device": true}` command now sets `enabled = false` before rebooting. After reset, the device returns to provisioning mode and requires re-activation.

## Device Lifecycle

### First Boot / After NVS Erase
1. Device boots with `enabled: false`
2. Enters provisioning mode
3. Wakes every `poll_interval` seconds
4. Publishes status with `enabled: false, provisioning: true`
5. Awaits `{"enable": true}` command

### Activation
1. Server publishes `{"enable": true}` as retained message
2. Device wakes, receives command
3. Device clears retained command
4. Device sets `enabled: true` in NVS
5. Device runs normal 3s timer init
6. Device enters System OFF, ready for mail detection

### Normal Operation
- Motion wake triggers OPEN/CLOSE detection
- Mail delivery events sent to `events` topic
- No provisioning polling occurs

### Deactivation
1. Server publishes `{"disable": true}` as retained message
2. Device receives on next wake
3. Device clears retained command
4. Device sets `enabled: false` in NVS
5. On next boot/wake, device enters provisioning mode

## Suggested Server Implementation

### Database Schema Addition
```sql
ALTER TABLE devices ADD COLUMN enabled BOOLEAN DEFAULT FALSE;
ALTER TABLE devices ADD COLUMN poll_interval INTEGER DEFAULT 60;
ALTER TABLE devices ADD COLUMN last_provisioning_poll TIMESTAMP;
```

### Activation Endpoint
```
POST /api/devices/{device_id}/enable
```
- Publishes `{"enable": true}` as retained message to commands topic
- Updates database `enabled = true`

### Deactivation Endpoint
```
POST /api/devices/{device_id}/disable
```
- Publishes `{"disable": true}` as retained message to commands topic
- Updates database `enabled = false`

### Status Handler Update
When processing status messages:
```python
if 'enabled' in payload:
    device.enabled = payload['enabled']
    device.provisioning = payload.get('provisioning', not device.enabled)
    device.poll_interval = payload.get('poll_interval', 60)
    device.last_status_update = datetime.utcnow()
```

### Provisioning Mode Detection
A device in provisioning mode will:
- Send status messages every `poll_interval` seconds
- Have `enabled: false` and `provisioning: true` in status
- NOT send mail delivery events

Consider adding a dashboard indicator for devices awaiting activation.

## Testing Checklist

1. [ ] Flash device with `--erase` to clear NVS
2. [ ] Verify device enters provisioning mode (status shows `enabled: false`)
3. [ ] Verify device polls at expected interval
4. [ ] Send `{"enable": true}` command
5. [ ] Verify device activates and performs 3s timer init
6. [ ] Verify motion detection works after activation
7. [ ] Send `{"disable": true}` command
8. [ ] Verify device returns to provisioning mode on next wake
9. [ ] Send `{"reset_device": true}` command
10. [ ] Verify device resets and enters provisioning mode
