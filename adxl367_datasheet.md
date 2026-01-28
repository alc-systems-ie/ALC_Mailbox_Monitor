# ADXL367 Data Sheet

**Micropower, 3-Axis, ±2 g/±4 g/±8 g Digital Output MEMS Accelerometer**

Rev. B

---

## Features

### Supply Voltage
- Single-cell battery operation: 1.1 V to 3.6 V
- Internal power supply regulation for high PSRR

### Ultralow Power
- 0.89 µA at 100 Hz ODR, 2.0 V supply
- 180 nA motion activated wake-up mode
- 40 nA standby current

### Performance
- High resolution: 0.25 mg/LSB
- Low noise to 170 µg/√Hz
- Deep 512 sample embedded FIFO

### Built-in Features for System Level Power Savings
- Single-tap and double-tap detection with only 35 nA of added current
- Adjustable threshold sleep and wake-up modes for motion activation
- Autonomous interrupt processing without microcontroller intervention
- Awake state output enables motion activated switch implementation

### Additional Features
- Acceleration sample synchronization via external trigger
- On-chip temperature sensor
- Internal two-pole antialias filter
- SPI (4-wire) and I²C digital interfaces
- Small package: 2.2 mm × 2.3 mm × 0.87 mm

---

## Applications

- 24/7 sensing
- Hearing aids
- Vital signs monitoring devices
- Motion-enabled power save switches
- Motion-enabled metering devices
- Smart watch with single-cell operation
- Smart home

---

## General Description

The ADXL367 is an ultralow power, 3-axis MEMS accelerometer that consumes only **0.89 µA** at a 100 Hz output data rate and **180 nA** when in motion-triggered wake-up mode. Unlike accelerometers that use power duty cycling to achieve low power consumption, the ADXL367 does not alias input signals by undersampling, but samples the full bandwidth of the sensor at all data rates.

The ADXL367 always provides **14-bit output resolution**. 8-bit formatted data is offered for more efficient single-byte transfers when lower resolution is sufficient. 12-bit formatted data is also provided for ADXL362 design compatibility.

Measurement ranges of **±2 g, ±4 g, and ±8 g** are available, with a resolution of 0.25 mg/LSB on the ±2 g range.

### Key Integrated Features
- Deep multimode output FIFO
- Built-in micropower temperature sensor
- Internal ADC for synchronous conversion of additional analog input
- Single-tap and double-tap detection (only 35 nA additional current)
- State machine to prevent false triggering
- Provisions for external control of sampling time and/or external clock

The ADXL367 operates on a wide **1.1 V to 3.6 V** supply range and can interface to a host operating on a separate supply voltage.

---

## Specifications

**Conditions:** T_A = 25°C, V_S = 2.0 V, V_DDIO = 2.0 V, 100 Hz ODR, ±2 g range, acceleration = 0 g, default settings

### Sensor Input

| Parameter | Test Conditions | Min | Typ | Max | Unit |
|-----------|-----------------|-----|-----|-----|------|
| Measurement Range | User selectable | | ±2, ±4, ±8 | | g |
| Nonlinearity (X, Y-axis) | FSR = 2 g | | 0.5 | | % |
| Nonlinearity (Z-axis) | FSR = 2 g | | 1.8 | | % |
| Sensor Resonant Frequency (X, Y) | | | 2170 | | Hz |
| Sensor Resonant Frequency (Z) | | | 3000 | | Hz |
| Cross Axis Sensitivity | | | 0.8 | | % |

### Output Resolution

| Parameter | Value | Unit |
|-----------|-------|------|
| All g Ranges | 14 | Bits |

### Sensitivity

| Parameter | Condition | Value | Unit |
|-----------|-----------|-------|------|
| Scale Factor | 2 g range | 0.25 | mg/LSB |
| Scale Factor | 4 g range | 0.5 | mg/LSB |
| Scale Factor | 8 g range | 1 | mg/LSB |
| Sensitivity | 2 g range | 4000 | LSB/g |
| Sensitivity | 4 g range | 2000 | LSB/g |
| Sensitivity | 8 g range | 1000 | LSB/g |
| Sensitivity Change Due to Temperature | | ±0.05 | %/°C |

### 0 g Offset

| Parameter | Condition | Min | Typ | Max | Unit |
|-----------|-----------|-----|-----|-----|------|
| 0 g Output (X, Y) | | −150 | ±35 | +150 | mg |
| 0 g Output (Z) | | −250 | ±50 | +250 | mg |
| 0 g Offset vs. Temperature | 2 g range | | ±0.68 | | mg/°C |
| 0 g Offset vs. Temperature | 4 g range | | ±1.28 | | mg/°C |
| 0 g Offset vs. Temperature | 8 g range | | ±2.5 | | mg/°C |

### Noise Performance

| Parameter | Condition | Typ | Unit |
|-----------|-----------|-----|------|
| Noise Density (Normal) | 2 g range | 370 | µg/√Hz |
| Noise Density (Low Noise) | 2 g range | 200 | µg/√Hz |
| Noise Density (Low Noise) | 2 g, 400 ODR | 170 | µg/√Hz |

### Bandwidth

| Parameter | Condition | Min | Max | Unit |
|-----------|-----------|-----|-----|------|
| Low Pass Filter −3 dB Corner | 2-pole filter | | ODR/2 | Hz |
| Output Data Rate (ODR) | User selectable | 12.5 | 400 | Hz |

### Self Test

| Parameter | Condition | Min | Typ | Max | Unit |
|-----------|-----------|-----|-----|-----|------|
| Output Change (X_OUT) | | 90 | 180 | 270 | mg |

### Power Supply

| Parameter | Condition | Min | Typ | Max | Unit |
|-----------|-----------|-----|-----|-----|------|
| Operating Voltage (V_S) | | 1.1 | 2.0 | 3.6 | V |
| I/O Voltage (V_DDIO) | | 1.1 | 2.0 | 3.6 | V |
| Supply Reset Threshold | | | 50 | | mV |

### Supply Current

| Mode | Condition | Typ | Unit |
|------|-----------|-----|------|
| Measurement Mode (Normal) | 100 Hz ODR | 0.89 | µA |
| Measurement Mode (Low Noise) | 100 Hz ODR | 1.77 | µA |
| Wake-Up Mode | | 181 | nA |
| Standby | | 40 | nA |

### Temperature Sensor

| Parameter | Condition | Value | Unit |
|-----------|-----------|-------|------|
| Bias Average | At 25°C | 165 | LSB |
| Sensitivity Average | | 54 | LSB/°C |
| Resolution | | 14 | Bits |

### External ADC Input

| Parameter | Condition | Value | Unit |
|-----------|-----------|-------|------|
| Input Range | | 0 to V_REG_OUT | V |
| Bias | | −8030 | LSB |
| Gain | | 15301 | LSB/V |
| Noise RMS | ODR = 100 Hz | 3 | LSB |
| SNR | | 70 | dB |

### Environmental

| Parameter | Min | Max | Unit |
|-----------|-----|-----|------|
| Operating Temperature | −40 | +85 | °C |

---

## Absolute Maximum Ratings

| Parameter | Rating |
|-----------|--------|
| Acceleration (Any Axis, Unpowered) | 5000 g |
| Acceleration (Any Axis, Powered) | 5000 g |
| V_S | −0.3 V to +4.0 V |
| V_DDIO | −0.3 V to +4.0 V |
| All Other Pins | −0.3 V to V_DDIO |
| Output Short-Circuit Duration | Indefinite |
| Storage Temperature | −50°C to +150°C |

---

## Pin Configuration

| Pin | Mnemonic | Description |
|-----|----------|-------------|
| 1 | SCLK | SPI Communication Clock. Tied low for I²C |
| 2 | MOSI/SDA | Main Output, Subordinate Input (MOSI), I²C Serial Data (SDA) |
| 3 | MISO/ASEL | Main Input, Subordinate Output (MISO), I²C Address Select (ASEL) |
| 4 | CS/SCL | SPI Chip Select (active low), I²C Clock (SCL) |
| 5 | INT1 | Interrupt 1 Output. Also serves as input for external clocking |
| 6 | INT2 | Interrupt 2 Output. Also serves as input for synchronized sampling |
| 7 | GND | Ground (must be grounded) |
| 8 | ADC_IN | ADC Input Pin. Can be left unconnected or connected to Pin 7/Pin 11 |
| 9 | V_REG_OUT | Internally Regulated Voltage (requires external 0.2 µF capacitor) |
| 10 | V_S | Supply Voltage |
| 11 | GND | Ground (must be grounded) |
| 12 | V_DDIO | Supply Voltage for Digital I/O |

---

## Theory of Operation

### Mechanical Device Operation

The moving component of the sensor is a polysilicon surface-micromachined structure built on top of a silicon wafer. Polysilicon springs suspend the structure over the surface and provide resistance against acceleration forces.

Deflection is measured using differential capacitors consisting of independent fixed plates and plates attached to the moving mass. Acceleration deflects the structure and unbalances the differential capacitor, resulting in a sensor output proportional to acceleration. Phase sensitive demodulation determines the magnitude and polarity.

### Operating Modes

The ADXL367 has three operating modes:

#### 1. Measurement Mode
- Normal operating mode for continuous, wide bandwidth sensing
- Supply current < 1.3 µA across all data rates up to 400 Hz
- All features available in this mode
- **Important:** Wait 100 ms after entering measurement mode before reading acceleration data

#### 2. Wake-Up Mode
- Reduces current to very low level by periodic sampling
- Electronics turned off between measurements
- 4 user-selectable wake-up rates: ~12.5 to ~1.5 samples/second
- Current consumption: 180 nA typical
- Ideal for motion-activated on/off switch implementation
- **Not supported in low noise mode**

#### 3. Standby Mode
- Suspends measurement, reduces current to 40 nA typical
- Pending interrupts and data are preserved
- No new interrupts generated
- Device powers up in standby with all sensor functions off

> **Note:** Register changes (0x00 to 0x2D) must be made in standby mode. Changes made during measurement mode may only be effective for part of a measurement.

### Selectable Measurement Ranges

| Range | Sensitivity |
|-------|-------------|
| ±2 g | 0.25 mg/LSB |
| ±4 g | 0.5 mg/LSB |
| ±8 g | 1 mg/LSB |

Acceleration samples are always converted by a 14-bit ADC, so sensitivity scales with g range.

### Selectable Output Data Rates

Available rates: 12.5 Hz to 400 Hz

The internal low-pass filter corner is automatically set to ensure Nyquist sampling criterion is met. Current consumption varies somewhat with ODR but remains below 1.3 µA over the entire range.

### Power/Noise Tradeoff

| Mode | Noise Density (µg/√Hz) | Current (µA) |
|------|------------------------|--------------|
| Normal Operation | 370 | 0.89 |
| Low Noise Mode | 200 | 1.77 |

### Temperature Sensor

- 14-bit resolution
- Trimmed at room temperature before shipping
- Can be used to:
  - Monitor internal system temperature
  - Improve temperature stability via calibration
  - Calibrate acceleration temperature drift (±0.5 mg/°C typical)

### External ADC

- Additional 14-bit ADC input for external analog signal digitization
- Sampled at same ODR as acceleration and temperature data
- Adds ~50 nA to current consumption at 100 Hz ODR
- Can be powered down when not needed
- Input range: 0 V (GND) to V_REG_OUT

---

## Power Savings Features

### Ultralow Power Consumption

- 0.8 µA to 1.4 µA (typical) across all data rates up to 400 Hz
- 160 nA (typical) in motion triggered wake-up mode
- 40 nA (typical) standby current

### Motion Detection

Built-in logic detects:
- **Activity:** Acceleration greater than threshold
- **Inactivity:** Lack of acceleration greater than threshold

#### Activity Detection

An activity event is detected when acceleration remains greater than a specified threshold for a specified time on any enabled axis.

**Referenced vs Absolute Configuration:**
- **Absolute:** Samples compared directly to user-set threshold
- **Referenced:** Activity detected when samples deviate from an internally defined reference point (removes effect of static 1 g from gravity)

#### Inactivity Detection

Detected when acceleration remains below threshold for specified time on ALL enabled axes.

**Referenced vs Absolute Configuration:**
- **Absolute:** Required for free fall detection
- **Referenced:** Eliminates effects of static acceleration due to gravity

#### Linking Activity and Inactivity Detection

| Mode | Description |
|------|-------------|
| **Default** | Both enabled concurrently, interrupts serviced by host processor |
| **Linked** | Only one enabled at a time, interrupts serviced by host processor |
| **Loop** | Only one enabled at a time, interrupts internally acknowledged |

#### Autosleep

When in linked or loop mode, enabling autosleep causes the device to:
- Enter wake-up mode when inactivity detected
- Re-enter measurement mode when activity detected

### FIFO

- 512-sample deep buffer
- Benefits:
  - System level power savings (host processor can sleep)
  - Data recording/event context (triggered mode captures pre-event data)
  - Can store up to 13+ seconds of data

### Using the AWAKE Bit

The AWAKE bit indicates whether the device is awake (experienced activity) or asleep (experienced inactivity).

Can be mapped to INT1 or INT2 pins to serve as a status output for motion activated switch implementation.

---

## Additional Features

### Free Fall Detection

Implement using inactivity interrupt:
- Set THRESH_INACT to free fall threshold (300-600 mg recommended)
- Set TIME_INACT for minimum duration (100-350 ms recommended)

### Tap Detection

Capable of detecting single or double taps.

**Parameters:**
| Register | Function | Scale Factor |
|----------|----------|--------------|
| THRESH_TAP (0x2F) | Tap threshold | 31.25 mg/LSB |
| TAP_DUR (0x30) | Maximum tap duration | 625 µs/LSB |
| TAP_LATENT (0x31) | Wait time after first tap | 1.25 ms/LSB |
| TAP_WINDOW (0x32) | Time window for second tap | 1.25 ms/LSB |

### External Clock

- Built-in 102.4 kHz clock (typical) serves as default time base
- External clock can be provided (51.2 kHz to 102.4 kHz)
- ODR scales with clock frequency:
  ```
  ODR_ACTUAL = ODR_SELECTED × (f / 102.4 kHz)
  ```
- **Must be configured in standby mode**

### External Trigger

For precisely timed acceleration measurements:
- INT2 pin used as synchronization trigger input
- Set EXT_SAMPLE bit in FILTER_CTL register (0x2C)

**Trigger Requirements:**
- Active high
- Pulse width ≥ 80 µs
- Deasserted for ≥ 120 µs before reassertion
- Maximum sampling frequency: 625 Hz (typical)

### Self Test

Tests mechanical and electronic systems simultaneously by applying electrostatic force to the sensor.

**Procedure:**
1. Enter measurement mode, wait 100 ms
2. Enable self test (ST bit in SELF_TEST register)
3. Wait 4/ODR
4. Read X-axis data
5. Apply self test force (ST_FORCE bit)
6. Wait 4/ODR
7. Read X-axis data
8. Compare difference to specification (90-270 mg)
9. Disable self test

### User Register Protection

SEU (Single Event Upset) protection via 99-bit error correcting code:
- Detects single-bit and double-bit errors
- Protects registers 0x00 to 0x43
- ERR_USER_REGS status bit indicates errors

---

## Serial Communications

The ADXL367 communicates via 4-wire SPI or I²C as a subordinate device.

### SPI Communication

**Recommended clock speeds:** 1 MHz to 8 MHz (12 pF max loading)

**SPI Mode:** CPOL = 0, CPHA = 0

#### SPI Commands

| Command | Value | Description |
|---------|-------|-------------|
| Write Register | 0x0A | `<CS↓> <0x0A> <address> <data> [additional data...] <CS↑>` |
| Read Register | 0x0B | `<CS↓> <0x0B> <address> <data> [additional data...] <CS↑>` |
| Read FIFO | 0x0D | `<CS↓> <0x0D> <data> <data> ... <CS↑>` |

**Multibyte Transfers:**
- Supported for all commands
- Address auto-increments for each additional byte
- Recommended for reading complete sample sets

### I²C Communication

With SCLK tied to ground, the ADXL367 operates in I²C mode.

**I²C Addresses:**

| ASEL Pin | 7-bit Address | Write | Read |
|----------|---------------|-------|------|
| High | 0x53 | 0xA6 | 0xA7 |
| Low (GND) | 0x1D | 0x3A | 0x3B |

**Supported Modes:**
- Standard mode (100 kHz)
- Fast mode (400 kHz)
- High speed mode (up to 3.4 MHz when I2C_HS = 1)

> **Important:** ASEL pin must be connected to either V_DDIO or GND when using I²C.

---

## Register Map

### Overview

| Address | Name | Description |
|---------|------|-------------|
| 0x00 | DEVID_AD | ADI Device ID (0xAD) |
| 0x01 | DEVID_MST | MEMS Device ID (0x1D) |
| 0x02 | PART_ID | Part ID (0xF7) |
| 0x03 | REV_ID | Revision ID (0x03) |
| 0x04-0x07 | SERIAL_NUMBER | 31-bit serial number |
| 0x08 | XDATA | X-axis data [13:6] (8-bit) |
| 0x09 | YDATA | Y-axis data [13:6] (8-bit) |
| 0x0A | ZDATA | Z-axis data [13:6] (8-bit) |
| 0x0B | STATUS | Status register |
| 0x0C-0x0D | FIFO_ENTRIES | FIFO sample count |
| 0x0E-0x0F | XDATA_H/L | X-axis data (14-bit) |
| 0x10-0x11 | YDATA_H/L | Y-axis data (14-bit) |
| 0x12-0x13 | ZDATA_H/L | Z-axis data (14-bit) |
| 0x14-0x15 | TEMP_H/L | Temperature data (14-bit) |
| 0x16-0x17 | EX_ADC_H/L | External ADC data (14-bit) |
| 0x18 | I2C_FIFO_DATA | I²C FIFO read address |
| 0x1F | SOFT_RESET | Soft reset (write 0x52) |
| 0x20-0x21 | THRESH_ACT | Activity threshold |
| 0x22 | TIME_ACT | Activity time |
| 0x23-0x24 | THRESH_INACT | Inactivity threshold |
| 0x25-0x26 | TIME_INACT | Inactivity time |
| 0x27 | ACT_INACT_CTL | Activity/inactivity control |
| 0x28 | FIFO_CONTROL | FIFO control |
| 0x29 | FIFO_SAMPLES | FIFO samples threshold |
| 0x2A | INTMAP1_LOWER | INT1 mapping (lower) |
| 0x2B | INTMAP2_LOWER | INT2 mapping (lower) |
| 0x2C | FILTER_CTL | Filter control |
| 0x2D | POWER_CTL | Power control |
| 0x2E | SELF_TEST | Self test control |
| 0x2F | TAP_THRESH | Tap threshold |
| 0x30 | TAP_DUR | Tap duration |
| 0x31 | TAP_LATENT | Tap latency |
| 0x32 | TAP_WINDOW | Tap window |
| 0x33-0x35 | X/Y/Z_OFFSET | User offset calibration |
| 0x36-0x38 | X/Y/Z_SENS | User sensitivity calibration |
| 0x39 | TIMER_CTL | Timer control |
| 0x3A | INTMAP1_UPPER | INT1 mapping (upper) |
| 0x3B | INTMAP2_UPPER | INT2 mapping (upper) |
| 0x3C | ADC_CTL | ADC control |
| 0x3D | TEMP_CTL | Temperature control |
| 0x3E-0x3F | TEMP_ADC_OVER_THRSH | Over threshold |
| 0x40-0x41 | TEMP_ADC_UNDER_THRSH | Under threshold |
| 0x42 | TEMP_ADC_TIMER | Temp/ADC timer |
| 0x43 | AXIS_MASK | Axis mask |
| 0x44 | STATUS_COPY | Status copy |
| 0x45 | STATUS_2 | Status 2 |

### Key Register Details

#### STATUS Register (0x0B)

| Bit | Name | Description |
|-----|------|-------------|
| 7 | ERR_USER_REGS | SEU error or device not configured |
| 6 | AWAKE | 1 = active, 0 = inactive |
| 5 | INACT | Inactivity detected |
| 4 | ACT | Activity detected |
| 3 | FIFO_OVER_RUN | FIFO overrun |
| 2 | FIFO_WATER_MARK | FIFO watermark reached |
| 1 | FIFO_READY | At least one sample in FIFO |
| 0 | DATA_READY | New data available |

#### ACT_INACT_CTL Register (0x27)

| Bits | Name | Description |
|------|------|-------------|
| [5:4] | LINKLOOP | 00: Default, 01: Linked, 10: Default, 11: Loop |
| [3:2] | INACT_EN | 00: Disabled, 01: Absolute, 11: Referenced |
| [1:0] | ACT_EN | 00: Disabled, 01: Absolute, 11: Referenced |

#### FIFO_CONTROL Register (0x28)

| Bits | Name | Description |
|------|------|-------------|
| [6:3] | CHANNEL_SELECT | Selects which channels to store |
| 2 | FIFO_SAMPLES[8] | MSB of FIFO samples |
| [1:0] | FIFO_MODE | 00: Disabled, 01: Oldest saved, 10: Stream, 11: Triggered |

#### FILTER_CTL Register (0x2C)

| Bits | Name | Description |
|------|------|-------------|
| [7:6] | RANGE | 00: ±2g, 01: ±4g, 10: ±8g |
| 5 | I2C_HS | High speed I²C mode |
| 3 | EXT_SAMPLE | External sampling trigger enable |
| [2:0] | ODR | 000: 12.5Hz, 001: 25Hz, 010: 50Hz, 011: 100Hz, 100: 200Hz, 101: 400Hz |

#### POWER_CTL Register (0x2D)

| Bits | Name | Description |
|------|------|-------------|
| 6 | EXT_CLK | External clock enable |
| [5:4] | NOISE | 00: Normal, 01: Low noise |
| 3 | WAKEUP | Wake-up mode enable |
| 2 | AUTOSLEEP | Autosleep enable |
| [1:0] | MEASURE | 00: Standby, 10: Measurement |

---

## Applications Information

### Device Configuration Sequence

1. Set activity/inactivity thresholds and timers (0x20-0x26)
2. Configure activity/inactivity functions (0x27)
3. Configure FIFO (0x28-0x29)
4. Map interrupts (0x2A-0x2B)
5. Configure device settings (0x2C)
6. Enter measurement mode (0x2D)

### Motion Switch Implementation

Using the AWAKE signal with autosleep:

1. Follow looped mode start-up routine
2. Host processor sleeps; accelerometer monitors for motion
3. Motion detected → INT1 goes HIGH → host wakes up
4. Host executes reference update routine
5. Customer code executes until inactivity detected
6. Host returns to sleep

### Free Fall Detection Start-Up Routine

For ±8 g range, 100 Hz ODR:

1. Write 0x18 to 0x24, 0x09 to 0x23 (threshold = 600 mg)
2. Write 0x03 to 0x26 (time = 30 ms)
3. Write 0x04 to 0x27 (absolute inactivity)
4. Write 0x20 to 0x2A or 0x2B (map interrupt)
5. Write 0x83 to 0x2C (±8 g, 100 Hz)
6. Write 0x02 to 0x2D (measurement mode)
7. Wait 100 ms for output to settle

### Power Supply Requirements

**Critical Requirements:**
- During power-up, supply current must be > 250 µA for correct fuse loading
- When power cycling, fully discharge to ground (V_S = 0 V) recommended
- If not discharging to ground:
  - V_S must rise from < V_RESET
  - Hold at < V_RESET for at least 300 ms before reapplying supply

**Decoupling:**
- 0.1 µF ceramic at V_S pin (as close as possible)
- 0.1 µF ceramic at V_DDIO pin (as close as possible)
- Separate supplies for V_S and V_DDIO recommended

### FIFO Modes

| Mode | Description |
|------|-------------|
| **Disabled** | No data stored, existing data cleared |
| **Oldest Saved** | Accumulates until full, then stops (first N) |
| **Stream** | Always contains most recent data (last N) |
| **Triggered** | Saves samples surrounding activity event |

**FIFO Data Formats:**

| Format | Bits | Description |
|--------|------|-------------|
| 14-bit + ID | 16 | D[15:14] = channel ID, D[13:0] = data |
| 12-bit packed | 12 | No channel ID, upper 12 bits |
| 8-bit packed | 8 | No channel ID, upper 8 bits |

**Sample Set Allocation (512 samples):**
- 513 sets of single-axis data
- 256 sets of 2-channel data
- 171 sets of 3-channel data
- 128 sets of 4-channel data

### Interrupts

**Clearing Interrupts:**
- STATUS or STATUS_COPY read → clears activity/inactivity
- STATUS2 read → clears tap/double tap
- Data register read → clears data ready
- FIFO read → clears FIFO interrupts (when condition no longer met)

**Interrupt Latency:** ~120 µs typical (up to 420 µs in wake-up mode)

---

## Mechanical Considerations

Mount the ADXL367 close to a hard mounting point of the PCB to minimize apparent measurement errors from PCB vibration.

### PCB Layout
- Place decoupling capacitors as close as possible to device
- Ensure low impedance ground connection

### Axes of Acceleration Sensitivity

Looking at device from top:
- **X-axis:** Horizontal, along pin 1-12 edge
- **Y-axis:** Horizontal, along pin 7-11 edge  
- **Z-axis:** Vertical, out of package

---

## Ordering Information

| Model | Temperature Range | Package | Packing |
|-------|-------------------|---------|---------|
| ADXL367BCCZ | −40°C to +85°C | LGA CC-12-4 | Tray |
| ADXL367BCCZ-RL | −40°C to +85°C | LGA CC-12-4 | Reel, 5000 |
| ADXL367BCCZ-RL7 | −40°C to +85°C | LGA CC-12-4 | Reel, 1500 |
| ADXL367U8-BCCZ | −40°C to +85°C | LGA CC-12-4 | Tray |
| ADXL367U8-BCCZ-RL | −40°C to +85°C | LGA CC-12-4 | Reel, 5000 |
| ADXL367U8-BCCZ-RL7 | −40°C to +85°C | LGA CC-12-4 | Reel, 1500 |

**Evaluation Boards:**
- EVAL-ADXL367Z: Breakout Board
- EVAL-ADXL367-SDP: Customer Evaluation System

---

## Revision History

| Date | Rev | Changes |
|------|-----|---------|
| 10/2024 | B | Terminology updates, various section changes |
| 9/2023 | A | Table updates, evaluation board changes |
| 3/2022 | 0 | Initial version |

---

*Document Feedback and Technical Support available at analog.com*

*©2021-2024 Analog Devices, Inc. All rights reserved.*
