# ALC Mailbox Monitor - Command Tests

Test scripts for validating MQTT command handling functionality.

## Available Commands

The device accepts the following MQTT commands on topic `alc/{DEVICE_ID}/commands`:

| Command | JSON Format | Valid Range | Description |
|---------|-------------|-------------|-------------|
| Set mail window | `{"mail_window": 240}` | 1-86400 | Mail detection window in seconds |
| Set activity threshold | `{"activity_threshold": 250}` | 1-8000 | Motion sensitivity in mg |
| Set activity time | `{"activity_time": 1}` | 1-255 | Activity confirmation samples |
| Set inactivity threshold | `{"inactivity_threshold": 1200}` | 1-8000 | Stillness threshold in mg |
| Set inactivity time | `{"inactivity_time": 10}` | 1-255 | Inactivity confirmation samples |
| Reset config | `{"reset_config": true}` | - | Reset all to defaults |
| Request status | `{"status_request": true}` | - | Request current config |

## Default Values

- `mail_window`: 240 seconds (4 minutes)
- `activity_threshold`: 250 mg
- `activity_time`: 1 sample
- `inactivity_threshold`: 1200 mg
- `inactivity_time`: 10 samples

## Python Test Script

### Prerequisites

```bash
pip install paho-mqtt
```

### Running Tests

```bash
# Run all tests
python tests/mqtt_command_test.py

# Run specific test
python tests/mqtt_command_test.py --test mail_window
python tests/mqtt_command_test.py --test thresholds
python tests/mqtt_command_test.py --test status
python tests/mqtt_command_test.py --test reset
python tests/mqtt_command_test.py --test invalid

# Interactive mode
python tests/mqtt_command_test.py --interactive

# Specify device ID
python tests/mqtt_command_test.py --device-id "your-device-id"
```

### Configuration

Edit `mqtt_command_test.py` to set your broker credentials:

```python
USERNAME = "alc-mailbox"
PASSWORD = "your-password-here"
```

## Manual Testing with mosquitto_pub

If you have the Mosquitto client tools installed:

```bash
# Set broker variables
BROKER="e40e8a66d54d495c86d6336e20375793.s1.eu.hivemq.cloud"
PORT=8883
USER="alc-mailbox"
PASS="your-password"
DEVICE="ccccddddeeeeffff0000111122221111"

# Set mail window to 3 minutes
mosquitto_pub -h $BROKER -p $PORT -u $USER -P $PASS \
  --capath /etc/ssl/certs \
  -t "alc/$DEVICE/commands" \
  -m '{"mail_window": 180}' \
  -r

# Request status
mosquitto_pub -h $BROKER -p $PORT -u $USER -P $PASS \
  --capath /etc/ssl/certs \
  -t "alc/$DEVICE/commands" \
  -m '{"status_request": true}' \
  -r

# Reset to defaults
mosquitto_pub -h $BROKER -p $PORT -u $USER -P $PASS \
  --capath /etc/ssl/certs \
  -t "alc/$DEVICE/commands" \
  -m '{"reset_config": true}' \
  -r

# Subscribe to responses
mosquitto_sub -h $BROKER -p $PORT -u $USER -P $PASS \
  --capath /etc/ssl/certs \
  -t "alc/$DEVICE/#" -v
```

## Test Scenarios

### Scenario 1: Basic Configuration Change

1. Subscribe to device status topic
2. Send `{"mail_window": 120}`
3. Trigger device wake (shake mailbox)
4. Verify status response shows `mail_window: 120`

### Scenario 2: Sensitivity Adjustment

For a more sensitive setup (detects lighter touches):
```json
{"activity_threshold": 100, "activity_time": 1}
```

For a less sensitive setup (ignores wind/vibration):
```json
{"activity_threshold": 500, "activity_time": 3}
```

### Scenario 3: Full Configuration Test

1. Send `{"reset_config": true}` - reset to defaults
2. Send `{"status_request": true}` - verify defaults
3. Send custom config:
   ```json
   {"mail_window": 300}
   ```
4. Wait for device wake, verify config applied
5. Send `{"reset_config": true}` - restore defaults

## Expected Responses

The device publishes on these topics:

- `alc/{DEVICE_ID}/status` - Configuration status response
- `alc/{DEVICE_ID}/events` - Mail delivery events
- `alc/{DEVICE_ID}/battery` - Battery status
- `alc/{DEVICE_ID}/heartbeat` - Periodic heartbeat

### Status Response Format

```json
{
  "mail_window": 240,
  "activity_threshold": 250,
  "activity_time": 1,
  "inactivity_threshold": 1200,
  "inactivity_time": 10
}
```

## Notes

- Commands are retained on the broker and processed on next device wake
- Device only wakes on motion events or timer expiry
- Changes to ADXL367 parameters take effect immediately after command processing
- Invalid values are logged and ignored (check device logs via serial)
