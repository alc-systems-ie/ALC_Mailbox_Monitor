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

### State Machine (Single-Wake)

Single wake event per mail delivery cycle:

1. **System OFF**: Device sleeps (~180nA, ADXL367 wake-up mode)
2. **Motion detected**: ADXL367 AWAKE rises → MCU wakes from System OFF
3. **Poll AWAKE**: MCU stays awake polling until AWAKE clears (door closed)
4. **Send event**: Connect to MQTT, send `mailbox_visited` or `mailbox_open`, disconnect
5. **System OFF**: Return to sleep

The nPM1300 GP Timer drives an escalating door-open notification sequence when the door is left open. It also handles fresh boot initialisation (3s timer to establish ready state) and may be used for future heartbeat timing.

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

All commands are sent as JSON to `alc/{DEVICE_ID}/commands` (retained messages recommended).

### Commands

| Command | Example | Description |
|---------|---------|-------------|
| `enable` | `{"enable": true}` | Exit provisioning mode, start mail detection |
| `disable` | `{"disable": true}` | Enter provisioning mode on next boot |
| `reset_config` | `{"reset_config": true}` | Reset all parameters to defaults |
| `status_request` | `{"status_request": true}` | Publish current config to status topic |
| `reset_device` | `{"reset_device": true}` | Reboot device, returns to provisioning mode |

### Adjustable Parameters

| Parameter | Command | Default | Range | Unit | Description |
|-----------|---------|---------|-------|------|-------------|
| `mail_window` | `{"mail_window": 300}` | 240 | 1–86400 | seconds | Open/close cycle timeout |
| `activity_threshold` | `{"activity_threshold": 200}` | 250 | 1–8000 | mg | Motion sensitivity to wake |
| `activity_time` | `{"activity_time": 2}` | 1 | 1–255 | samples | Samples above threshold to trigger |
| `inactivity_threshold` | `{"inactivity_threshold": 1000}` | 250 | 1–8000 | mg | Threshold to detect rest (referenced) |
| `inactivity_time` | `{"inactivity_time": 15}` | 10 | 1–255 | samples | Samples below threshold for inactive |
| `max_buffered_events` | `{"max_buffered_events": 15}` | 10 | 1–20 | events | Offline event buffer size |
| `poll_interval` | `{"poll_interval": 30}` | 60 | 10–300 | seconds | Provisioning mode poll frequency |

### Persistence Notes

- ADXL367 parameters (`activity_threshold`, `activity_time`, `inactivity_threshold`, `inactivity_time`, `mail_window`) take effect immediately but do **not** persist across System OFF.
- `enabled`, `poll_interval`, `max_buffered_events`, and `door_open_stage` are stored in NVS flash and persist across System OFF.

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

The `enabled`, `poll_interval`, and `door_open_stage` values are stored in NVS flash and persist across System OFF cycles:

```cpp
struct RetainedState {
    ...
    bool enabled;              // Device operational state
    uint8_t door_open_stage;   // Escalating door-open timer (0=inactive, 1-3)
    uint16_t poll_interval;    // Provisioning poll interval (seconds)
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
  "inactivity_threshold": 250,
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

- **Accelerometer (P0.11):** ADXL367 INT1 — motion detection, always enabled.
- **Timer (P0.02):** nPM1300 SHPHLD GPIO — door-open escalating timer and future heartbeat. Always configured as wake source; no-op if no timer is running.
- **Hall sensor:** Stubbed but not implemented.

### ADXL367 AWAKE State Before System OFF

The GPIO latch only captures **rising edges**. Before entering System OFF, the firmware waits for ADXL367 to return to inactive state (AWAKE=0). If AWAKE=1 when entering System OFF, and it clears during boot, no rising edge occurs and the latch won't be set - causing wake source detection to fail.

See `configureWakeSources()` in `app.cpp` for the polling logic (5s timeout, 100ms poll interval).

### ADXL367 Loop Mode with Referenced Activity/Inactivity

The ADXL367 uses **loop mode** with **referenced** activity and inactivity detection. Referenced mode compares acceleration against a reference point captured at the last state transition, rather than against an absolute value. This correctly detects orientation changes (lid open/close) that don't exceed absolute thresholds.

**Configuration:**
- Activity: Referenced, 250mg threshold, 1 sample
- Inactivity: Referenced, 250mg threshold, 10 samples (~1.6s at 6 SPS)
- Link/loop: Loop (auto-acknowledged, sequential act→inact)
- Autosleep enabled (POWER_CTL bit 2): device autonomously switches between measurement and wake-up mode

**Loop Mode Startup Sequence (required):**

In loop mode, AWAKE starts HIGH on power-up and won't clear until a full activity→inactivity cycle completes. The datasheet specifies a startup sequence to clear this:

```cpp
// 1. Configure with dummy thresholds (in Standby)
//    Activity threshold = 1mg (below noise floor → triggers immediately)
//    Inactivity threshold = 8000mg (full scale → triggers immediately)
//    Both referenced mode, loop mode

// 2. Enter measurement + autosleep (POWER_CTL = 0x06)
//    This starts the loop state machine

// 3. Wait for AWAKE=0 (~100ms)
//    The dummy thresholds cause immediate activity→inactivity cycle

// 4. Reconfigure ONLY threshold/timer registers (0x20-0x26)
//    with real values (e.g. 250mg activity, 250mg inactivity)
```

**Critical: Do NOT write ACT_INACT_CTL (0x27) during step 4.** Writing the mode register while in measurement mode resets the loop state machine, causing AWAKE to stick HIGH again. Only write threshold and timer registers (0x20-0x26).

**Behaviour:**
- Device sleeps in home position (AWAKE=0, ~180nA wake-up mode)
- Any movement exceeding 250mg from reference → AWAKE=1 (activity detected)
- Device must settle within 250mg of a new reference for 10 samples → AWAKE=0
- Device only returns to AWAKE=0 in its home position, not on an edge/tilted
- If door is left open (different orientation from home), AWAKE stays HIGH

**Register Naming Convention:**
- Register addresses: `M_REG_` prefix (e.g. `M_REG_STATUS`, `M_REG_THRESH_ACT_H`)
- Masks and constants: `M_` prefix (e.g. `M_AWAKE_MASK`, `M_WAKEUP_RATE_SHIFT`)

**Data Register Format (ReadAxes, 0x0E-0x13):**
- H[7:0] = D[13:6], L[7:2] = D[5:0], L[1:0] = reserved
- Different from FIFO format (D[15:14]=channel ID, D[13:0]=signed 14-bit)

### Motion Detection State Machine

The approach uses a single-wake design with three event classifications:

1. **Wake from System OFF** (ADXL367 AWAKE rising edge)
2. **Capture AWAKE state** at boot (before driver init resets the sensor)
3. **Classify event:**

**Case 1 (Bump):** AWAKE was LOW at boot — motion ended before MCU started. Ignored.

**Case 2 (Mailbox visited):** AWAKE was HIGH at boot, device settles at home position. Send `mailbox_visited` event. If an escalating door-open timer is running, stop it and reset the stage to 0.

**Case 3 (Door left open):** AWAKE was HIGH at boot, device settles but NOT at home position. Starts the escalating door-open timer sequence (see below). ADXL367 is recalibrated so a door-close will also trigger a wake.

**Important:** Case 3 triggers on position (NOT HOME), not on the 30s AWAKE timeout. Because `configureMotionSensor()` resets the ADXL367 reference on boot, AWAKE clears instantly when the box is stable in the open position — the 30s timeout never fires. The timeout remains only as a safety net for a genuinely stuck AWAKE signal.

### Escalating Door-Open Timer

When the door is left open (case 3), the nPM1300 GP Timer sends up to 3 escalating notifications via System OFF wake on P0.02:

| Stage | Duration (production) | Duration (testing) | Action on expiry |
|-------|----------------------|-------------------|-----------------|
| 1 | 4 minutes | 20 seconds | Send notification, start stage 2 timer |
| 2 | 1 hour | 30 seconds | Send notification, start stage 3 timer |
| 3 | 2 hours | 40 seconds | Send final notification, stop (no more timers) |

**Durations** are defined in `M_DOOR_OPEN_DURATIONS[]` in `app.hpp`. Testing and production values are provided; swap the active line to switch.

**Persistence:** The current stage (`door_open_stage`, 0-3) is stored in `RetainedState` in NVS flash, surviving System OFF. This allows the firmware to know on a timer wake which stage to escalate to next.

**Flow:**
1. Case 3 detected → check `door_open_stage`. If already at max (3), do nothing.
2. Stop any existing timer (`TimerStop()`), then start timer with `M_DOOR_OPEN_DURATIONS[stage]`, set `door_open_stage = stage + 1`.
3. Timer expires → `handleTimerWake()` sends notification, checks if `stage < 3`.
4. If under cap: start next timer, increment stage. If at cap: reset stage to 0, stop.
5. Door close at any point (case 2): stop timer hardware, disable interrupt, reset stage to 0.

**Edge case — gust of wind:** If the door is open and a gust triggers an accelerometer wake while a stage timer is running, the MCU boots, detects NOT HOME (case 3 again), and restarts the timer at the current stage. The existing timer is explicitly stopped before starting the new one to ensure deterministic behaviour. The stage does not advance — only a timer expiry advances the stage.

**Wake sources for System OFF:**
- P0.11 (ADXL367 INT1): Motion detection — always enabled.
- P0.02 (nPM1300 SHPHLD GPIO): Timer expiry — always enabled (no-op if no timer running).

**Power consideration:** During the initial wake, the MCU polls AWAKE briefly (typically 0ms when door is stable open, up to 30s safety timeout). After classifying as NOT HOME, it enters System OFF and only wakes briefly on each timer expiry to send a notification.

### FIFO Configuration

- FIFO mode: Stream (always contains most recent data)
- Channels: XYZ only
- ODR: 50 Hz (in measurement mode), wake-up rate 6 SPS (in autosleep)
- FIFO read register: 0x18 (I2C_FIFO_DATA), bulk read

**FIFO Data Format (different from data registers):**
- 16 bits per sample: D[15:14] = channel ID, D[13:0] = signed 14-bit data
- Samples arrive in X, Y, Z order (3 samples per XYZ set)

**Known Issues / Quirks:**
- Main stack increased to 8K for analysis buffers — review if this can be reduced

### Event Buffering (NVS Flash)

Mail delivery events are buffered in NVS flash when network connectivity fails. This ensures events aren't lost during outages and decouples the state machine from connectivity status.

**Important:** nRF91 series does NOT support RAM retention in System OFF mode (unlike nRF52/nRF53). NVS flash is used instead.

**Structure (`retained.hpp`):**
```cpp
enum class EventType : uint8_t {
    MailboxVisited = 0,   // Door opened and closed (delivery or collection)
    MailboxOpen = 1       // Door left open (escalating timer notification)
};

struct BufferedEvent {
    uint32_t timestamp;       // Seconds since boot (TODO: RTC epoch)
    bool owner_intervened;    // Future: hall sensor detected
    EventType event_type;     // mailbox_visited or mailbox_open
};

struct RetainedState {
    uint32_t magic;           // 0x4D414950 ("MAIP") for validity - version 5
    uint8_t event_count;      // Number of buffered events
    uint8_t max_events;       // Runtime configurable (1-20)
    bool enabled;             // Device operational state (false = provisioning)
    uint8_t door_open_stage;  // Escalating door-open timer stage (0=inactive, 1-3)
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
  "event": "mailbox_visited",
  "timestamp": 12345,
  "owner_intervened": false
}
```

| Field | Type | Description |
|-------|------|-------------|
| `event` | string | `"mailbox_visited"` or `"mailbox_open"` (see below) |
| `timestamp` | uint32 | Seconds since device boot (TODO: RTC epoch) |
| `owner_intervened` | bool | `true` suppresses SMS notification |

**Event Types:**
- `mailbox_visited` — Door opened and closed (case 2). Covers both mail delivery and collection; the device cannot discriminate between the two.
- `mailbox_open` — Door left open (timer wake). Sent by the escalating door-open timer sequence (up to 3 notifications).

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
