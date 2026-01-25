# CLAUDE.md - ALC Mailbox Monitor

## Project Overview

nRF9151-based ultra-low-power mailbox delivery notification firmware for the Thingy91x platform. Detects mailbox opening/closing via ADXL367 accelerometer motion sensing and sends MQTT notifications over NB-IoT cellular.

**Key design principle:** Minimize power via System OFF with nPM1300 persistent timer maintaining state across sleep cycles.

## Quick Reference

```bash
# Build
west build -b thingy91x/nrf9151/ns

# Flash
west flash

# Monitor serial output
minicom -D /dev/tty.usbmodem* -b 115200
```

## Architecture

### State Machine (Timer-Based)

The nPM1300 GP Timer persists across System OFF, eliminating flash storage needs:

1. **OPEN Event** (first motion): Timer expired → Start 240s timer → Sleep
2. **CLOSE Event** (second motion within window): Timer running → Send MQTT "mail_delivered" → Sleep
3. Timer expiry resets state automatically

### Source Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | Entry point, creates App instance |
| `src/app.cpp/hpp` | State machine, event orchestration |
| `src/modem.cpp/hpp` | NB-IoT/LTE connectivity via nrf_modem_lib |
| `src/mqtt.cpp/hpp` | TLS MQTT client (HiveMQ Cloud) |
| `src/adxl367.cpp/hpp` | Accelerometer driver (I2C, 180nA wake mode) |
| `src/npm1300.cpp/hpp` | PMIC driver (timer, battery, charging) |
| `src/led.cpp/hpp` | RGB LED via PWM |
| `src/fuel_gauge.cpp/hpp` | Battery SoC estimation |

### Hardware Interfaces

- **ADXL367**: I2C address 0x1D, INT1 on P0.11 (wake source)
- **nPM1300**: I2C PMIC, GP Timer for persistent state
- **RGB LED**: PWM channels (led0-red, led0-green, led0-blue aliases)
- **Modem**: nRF9151 integrated LTE-M/NB-IoT

## Configuration

### Key Constants (app.hpp)

- `MAIL_WINDOW_SECS`: 240 (4-minute open window)
- `MODEM_TIMEOUT_SECS`: 90
- Device ID prefix: "cccc"

### MQTT Broker

- Host: `e40e8a66d54d495c86d6336e20375793.s1.eu.hivemq.cloud:8883`
- TLS security tag: 24
- Topics: `alc/{DEVICE_ID}/events`, `battery`, `heartbeat`, `commands`

### prj.conf Highlights

- C++20 enabled
- NB-IoT mode (not LTE-M)
- TF-M for secure partition
- Immediate logging to UART
- No Zephyr PM (uses direct System OFF)

## MQTT Runtime Configuration

### Configurable Parameters

| Parameter | Type | Default | Min | Max | Unit | Description |
|-----------|------|---------|-----|-----|------|-------------|
| `mail_window` | uint32 | 240 | 1 | 86400 | seconds | Time window for open/close detection cycle |
| `activity_threshold` | uint16 | 250 | 1 | 8000 | mg | Motion sensitivity for wake trigger |
| `activity_time` | uint8 | 1 | 1 | 255 | samples | Consecutive samples above threshold to trigger |
| `inactivity_threshold` | uint16 | 1200 | 1 | 8000 | mg | Threshold to return to inactive state |
| `inactivity_time` | uint8 | 10 | 1 | 255 | samples | Consecutive samples below threshold for inactive |

### Command Topic

```
alc/{DEVICE_ID}/commands
```

### Command Formats (JSON)

```json
// Set individual parameters
{"mail_window": 300}
{"activity_threshold": 200}
{"activity_time": 2}
{"inactivity_threshold": 1000}
{"inactivity_time": 15}

// Reset all to factory defaults
{"reset_config": true}

// Request current config (publishes to status topic)
{"status_request": true}
```

### Status Response Topic

Device publishes to: `alc/{DEVICE_ID}/status`

```json
{
  "mail_window": 240,
  "activity_threshold": 250,
  "activity_time": 1,
  "inactivity_threshold": 1200,
  "inactivity_time": 10
}
```

### Implementation Notes

- Commands should be **retained messages** so device receives on next connection
- Configuration persists only until next System OFF (no flash storage)
- ADXL367 parameters reconfigure immediately after command received
- `mail_window` changes affect the next open/close cycle

## Development Notes

### Timer State Detection

Uses `EVENTSSHPHLDSET` bit 3, not `TIMERSTATUS` register. See `Npm1300::TimerIsExpired()`.

### Wake Sources

```cpp
enum class WakeSource { Accelerometer, Timer, HallSensor, PowerOn };
```

Hall sensor (P0.02) stubbed but not implemented.

### Power Budget

- System OFF: < 1 μA
- ADXL367 wake mode: ~180 nA
- Active (modem on): ~50-200 mA briefly

### LED Status Colors

- BLUE: Boot/startup
- AMBER: Processing motion wake
- GREEN: Mail delivery confirmed
- RED: Error
- OFF: System OFF (normal state)

## Build Requirements

- NCS v3.1.0
- Thingy91x hardware with LP803448 battery
- SIM card with NB-IoT coverage

## File Structure

```
alc_mailbox_monitor/
├── CMakeLists.txt
├── prj.conf
├── boards/thingy91x_nrf9151_ns.overlay
├── src/
│   ├── main.cpp
│   ├── app.cpp/hpp
│   ├── modem.cpp/hpp
│   ├── mqtt.cpp/hpp
│   ├── adxl367.cpp/hpp
│   ├── npm1300.cpp/hpp (+ npm1300_const.hpp)
│   ├── led.cpp/hpp
│   ├── fuel_gauge.cpp/hpp
│   ├── certificate.h
│   └── LP803448_battery_model.h
└── compile_commands.json → build/
```
