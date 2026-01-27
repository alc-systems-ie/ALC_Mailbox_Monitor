# CLAUDE.md - ALC Mailbox Monitor

## Project Overview

nRF9151-based ultra-low-power mailbox delivery notification firmware for the Thingy91x platform. Detects mailbox opening/closing via ADXL367 accelerometer motion sensing and sends MQTT notifications over LTE-M cellular.

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
| `src/modem.cpp/hpp` | LTE-M/NB-IoT connectivity via nrf_modem_lib |
| `src/retained.cpp/hpp` | NVS flash event buffering for offline resilience |
| `src/mqtt.cpp/hpp` | TLS MQTT client (HiveMQ Cloud) |
| `src/adxl367.cpp/hpp` | Accelerometer driver (I2C, 180nA wake mode) |
| `src/npm1300.cpp/hpp` | PMIC driver (timer, battery, charging) |
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
- LTE-M mode (switched from NB-IoT for faster, more reliable connections)
- TF-M for secure partition
- Immediate logging to UART
- No Zephyr PM (uses direct System OFF)
- NVS flash for event buffering (nRF91 lacks RAM retention in System OFF)

## MQTT Runtime Configuration

### Configurable Parameters

| Parameter | Type | Default | Min | Max | Unit | Description |
|-----------|------|---------|-----|-----|------|-------------|
| `mail_window` | uint32 | 240 | 1 | 86400 | seconds | Time window for open/close detection cycle |
| `activity_threshold` | uint16 | 250 | 1 | 8000 | mg | Motion sensitivity for wake trigger |
| `activity_time` | uint8 | 1 | 1 | 255 | samples | Consecutive samples above threshold to trigger |
| `inactivity_threshold` | uint16 | 1200 | 1 | 8000 | mg | Threshold to return to inactive state |
| `inactivity_time` | uint8 | 10 | 1 | 255 | samples | Consecutive samples below threshold for inactive |
| `max_buffered_events` | uint8 | 10 | 1 | 20 | events | Maximum mail events to buffer when offline |
| `poll_interval` | uint16 | 60 | 10 | 300 | seconds | Provisioning mode wake/poll interval |

### Command Topic

```
alc/{DEVICE_ID}/commands
```

### Command Formats (JSON)

```json
// Provisioning mode commands
{"enable": true}              // Enable device, exit provisioning mode
{"disable": true}             // Disable device, enter provisioning mode
{"poll_interval": 30}         // Set provisioning poll interval (10-300s)

// Set individual parameters
{"mail_window": 300}
{"activity_threshold": 200}
{"activity_time": 2}
{"inactivity_threshold": 1000}
{"inactivity_time": 15}
{"max_buffered_events": 15}

// Reset all to factory defaults
{"reset_config": true}

// Request current config (publishes to status topic)
{"status_request": true}

// Device reset (waits for timer expiry, then reboots, returns to provisioning mode)
{"reset_device": true}
```

### Device Reset Behaviour

The `reset_device` command triggers a full system reboot via `sys_reboot()`. To avoid interrupting an in-progress open/close cycle:

1. If nPM1300 timer has expired: Reset immediately
2. If timer is running: Wait for timer expiry OR `mail_window` seconds (whichever comes first)
3. Shutdown modem cleanly
4. Execute system reset

After reset, the device goes through normal boot sequence including the 3-second timer initialisation that sets the expired flag, ensuring the next motion is correctly detected as an OPEN event.

Buffered events are preserved across the reset.

**Note:** `reset_device` also sets `enabled = false`, returning the device to provisioning mode after reset.

## Provisioning Mode

Devices start in **provisioning mode** when first powered on (or after NVS magic changes). In this mode, the device stays awake and polls periodically for an enable command rather than detecting mail events.

### Behaviour

1. **First Boot**: Device boots disabled (`enabled: false`) and enters provisioning mode
2. **Polling Loop**: Device stays awake, polls every `poll_interval` seconds (default 60s) using `k_sleep()`
3. **On Poll**: Connects to MQTT, publishes status, checks for enable command
4. **Enable Command**: When `{"enable": true}` received, device saves state and reboots
5. **Post-Enable Boot**: Device runs 3-second timer init, then enters System OFF for mail detection

**Note:** Provisioning mode does NOT use System OFF - it stays awake to allow faster response to enable commands and simpler state management. Power consumption is higher but this is acceptable during initial setup.

### Provisioning Commands

| Command | Description |
|---------|-------------|
| `{"enable": true}` | Enable device, exit provisioning mode |
| `{"disable": true}` | Disable device, enter provisioning mode on next boot |
| `{"poll_interval": 30}` | Set provisioning poll interval (10-300 seconds) |

Commands should be **retained messages**. The enable/disable commands are automatically cleared after being processed to prevent repeated execution.

### State Persistence

The `enabled` and `poll_interval` values are stored in NVS flash and persist across System OFF cycles:

```cpp
struct RetainedState {
    ...
    bool enabled;           // Device operational state
    uint16_t poll_interval; // Provisioning poll interval (seconds)
    ...
};
```

### Disabling a Device

A device can be returned to provisioning mode via:
1. **Disable command**: `{"disable": true}` - takes effect on next boot/wake
2. **Reset command**: `{"reset_device": true}` - resets and enters provisioning mode
3. **Change magic number**: Update `RetainedState::MAGIC` in `retained.hpp` to invalidate old NVS data

**Note:** `west flash --erase` only erases internal flash. Settings are stored on external SPI flash which persists across internal erase.

### Status Response Topic

Device publishes to: `alc/{DEVICE_ID}/status`

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

| Field | Type | Description |
|-------|------|-------------|
| `enabled` | bool | `true` when device is in normal operation mode |
| `provisioning` | bool | `true` when device is in provisioning mode (inverse of enabled) |
| `poll_interval` | uint16 | Provisioning mode wake interval in seconds |

### Implementation Notes

- Commands should be **retained messages** so device receives on next connection
- Configuration persists only until next System OFF (no flash storage)
- ADXL367 parameters reconfigure immediately after command received
- `mail_window` changes affect the next open/close cycle

## Battery Status Message

Device publishes to: `alc/{DEVICE_ID}/battery`

### JSON Payload (Standardised Format)

```json
{
  "level": 75,
  "voltage_mv": 3800,
  "current_ma": 150,
  "temperature_c": 25,
  "charging": true,
  "charge_status": "cc"
}
```

### Field Definitions

| Field | Type | Unit | Description |
|-------|------|------|-------------|
| `level` | int | % | State of charge percentage (0-100) |
| `voltage_mv` | int | mV | Battery voltage in millivolts |
| `current_ma` | int | mA | Battery current in milliamps |
| `temperature_c` | int | °C | Battery temperature in Celsius |
| `charging` | bool | - | Whether VBUS is connected |
| `charge_status` | string | - | Charging phase (see below) |

### Charge Status Values

- `idle` - Not charging
- `trickle` - Trickle charge phase
- `cc` - Constant current phase
- `cv` - Constant voltage phase
- `complete` - Charging complete

### SoC Estimation

The `level` field uses a simple linear voltage-based estimation:
- 3.0V = 0%
- 4.2V = 100%

This provides a reasonable approximation for LP803448 Li-Po batteries without requiring the full fuel gauge algorithm to be initialised.

## Development Notes

### Timer State Detection

Uses `EVENTSSHPHLDSET` bit 3, not `TIMERSTATUS` register. See `Npm1300::TimerIsExpired()`.

**Important:** The timer must be configured before use:
```cpp
m_pmic.TimerConfigure(TimerMode::GeneralPurpose, TimerPrescaler::Slow);
m_pmic.TimerSetDuration(seconds);  // Sets hi/mid/lo bytes + strobes
m_pmic.TimerStart();
// Wait for TimerIsExpired() to return true
m_pmic.TimerClearEvent();  // Reset for next cycle
```

The 3-second timer initialisation on fresh boot establishes the "ready for OPEN" state by ensuring the expired flag is set.

### Wake Sources

```cpp
enum class WakeSource { Accelerometer, Timer, HallSensor, PowerOn };
```

Hall sensor (P0.02) stubbed but not implemented.

### ADXL367 AWAKE State Before System OFF

The GPIO latch only captures **rising edges**. Before entering System OFF, the firmware waits for ADXL367 to return to inactive state (AWAKE=0). If AWAKE=1 when entering System OFF, and it clears during boot, no rising edge occurs and the latch won't be set - causing wake source detection to fail.

See `configureWakeSources()` in `app.cpp` for the polling logic (5s timeout, 100ms poll interval).

### Event Buffering (NVS Flash)

Mail delivery events are buffered in NVS flash when network connectivity fails. This ensures events aren't lost during outages and decouples the state machine from connectivity status.

**Important:** nRF91 series does NOT support RAM retention in System OFF mode (unlike nRF52/nRF53). NVS flash is used instead.

**Structure (`retained.hpp`):**
```cpp
struct BufferedEvent {
    uint32_t timestamp;       // Seconds since boot (TODO: RTC epoch)
    bool owner_intervened;    // Future: hall sensor detected
};

struct RetainedState {
    uint32_t magic;           // 0x4D41494D ("MAIM") for validity - version 2
    uint8_t event_count;      // Number of buffered events
    uint8_t max_events;       // Runtime configurable (1-20)
    bool enabled;             // Device operational state (false = provisioning)
    uint16_t poll_interval;   // Provisioning poll interval (seconds)
    BufferedEvent events[20]; // Circular buffer, oldest at index 0
};
```

**Behaviour:**
1. On every CLOSE event: Event is buffered to flash with timestamp
2. After buffering: Attempt LTE connection and MQTT send
3. On successful send: All buffered events transmitted, buffer cleared
4. On connection failure: Events remain buffered for next CLOSE event
5. Buffer full: Oldest event dropped to make room

**SMS Flood Prevention:**
When sending multiple buffered events (catch-up after outage):
- Older events sent with `owner_intervened: true` to suppress SMS notifications
- Most recent event sent with actual `owner_intervened` value
- This prevents carers receiving a flood of SMS for stale events

**Event Message Format:**
```json
{
  "event": "mail_delivered",
  "timestamp": 12345,
  "owner_intervened": false
}
```

| Field | Type | Description |
|-------|------|-------------|
| `event` | string | Always `"mail_delivered"` |
| `timestamp` | uint32 | Seconds since device boot (TODO: RTC epoch) |
| `owner_intervened` | bool | `true` suppresses SMS notification |

The `timestamp` field currently uses uptime in seconds. Future hardware revision will include RTC for epoch timestamps.

**Note on `owner_intervened`:** When catching up after connectivity outage, older buffered events are sent with `owner_intervened: true` to prevent SMS flood. Only the most recent event uses the actual value.

### Power Budget

- System OFF: < 1 μA
- ADXL367 wake mode: ~180 nA
- Active (modem on): ~50-200 mA briefly

### OPEN Event Timing Optimisation

To support fast mail deliveries (< 5 seconds open-to-close), hardware init is split:

1. **Essential init** (always): PMIC, ADXL367 (~1.5 seconds)
2. **Network init** (CLOSE only): Modem, MQTT (deferred)

OPEN events skip network init entirely, reducing wake-to-sleep time from ~4.6s to ~1.5s.

**Note:** LED code has been removed for production. Files `led.cpp/hpp` remain in repo but are not compiled.

## Build Requirements

- NCS v3.1.0
- Thingy91x hardware with LP803448 battery
- SIM card with LTE-M coverage

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
│   ├── retained.cpp/hpp
│   ├── adxl367.cpp/hpp
│   ├── npm1300.cpp/hpp (+ npm1300_const.hpp)
│   ├── fuel_gauge.cpp/hpp
│   ├── certificate.h
│   └── LP803448_battery_model.h
└── compile_commands.json → build/
```
