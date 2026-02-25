# ESP-NOW Communication Protocol Documentation

## Overview

This document describes the ESP-NOW packet communication protocol between the S3 (Command Interface) and C3 (Controller) ESP32 development boards, and how to use every source file in the project.

---

## System Architecture

```
+---------------------+    ESP-NOW     +----------------------+
|   S3 Board          |<-------------->|  C3 Board            |
| (Command Interface) |                |  (Controller)        |
|                     |  Commands      |                      |
| MAC: 30:ED:A0:      |--------------->|  MAC: A0:76:4E:      |
|      27:8F:A4       |                |       7B:9A:B4       |
|                     |  Status        |                      |
|                     |<---------------|                      |
+---------------------+                +----------------------+
```

- **S3 -> C3**: Sends control commands (motor duty/direction/enable, IR mode switch, color mode/LED control)
- **C3 -> S3**: Sends status packets (motor state + RPM, IR readings, color sensor readings)
- **Bidirectional**: Full duplex, fire-and-forget

### Hardware Summary

| Board | MCU | PlatformIO Board | COM Port | Framework |
|-------|-----|-------------------|----------|-----------|
| S3 | ESP32-S3 | `4d_systems_esp32s3_gen4_r8n16` | COM14 | Arduino |
| C3 | ESP32-C3-MINI-1 | `esp32-c3-devkitm-1` | COM18 | Arduino |

---

## Hardware Pin Reference (C3 Board)

| Pin | Function | Notes |
|-----|----------|-------|
| GPIO0 | Encoder -- Phase A | `INPUT_PULLUP`, interrupt on CHANGE |
| GPIO1 | Encoder -- Phase B | `INPUT_PULLUP`, interrupt on CHANGE |
| GPIO2 | Motor -- Enable (DRV8835) | PWM output (20 kHz, 10-bit) |
| GPIO3 | Motor -- Phase (DRV8835) / IR Analog (AO) | Direction control (IR not active in current build) |
| GPIO4 | Color Sensor -- LED control | `OUTPUT`, HIGH = LED on, LOW = LED off |
| GPIO5 | Color Sensor -- I2C SDA | TCS34725 |
| GPIO6 | Color Sensor -- I2C SCL | TCS34725 |
| GPIO8 | NeoPixel LED | Reserved |
| GPIO9 | BOOT button | Reserved |
| GPIO10+ | SPI Flash | **Do NOT use for GPIO** |

> **Important**: GPIO10 on the ESP32-C3-MINI-1 module is tied to SPI flash and cannot be used as general GPIO.

> **Pin conflict note**: GPIO4 is used by both the IR sensor (digital output) and the color sensor (LED control). When both sensors need to be active simultaneously, the IR digital pin must be moved to a different GPIO.

> **Encoder wiring**: Connect the 25GA370 encoder's Phase A output to GPIO0 and Phase B output to GPIO1. The encoder VCC connects to 3.3V (or 5V if level-shifted) and GND to board GND.

---

## Key Design Decisions

### Mode-Exclusive Pin Reading (IR Sensor)

The IR sensor cannot reliably drive both its **DO (digital)** and **AO (analog)** outputs simultaneously when both are being read. Continuously running `analogRead()` while also calling `digitalRead()` causes the ADC to load the sensor's internal circuitry and interferes with the LM393 comparator driving the DO line.

**Solution**: Only the active mode's pin is read at any time.

- In **digital mode**: only `digitalRead(GPIO4)` is called. `analogRead(GPIO3)` is never called.
- In **analog mode**: only `analogRead(GPIO3)` is called. `digitalRead(GPIO4)` is not called.
- On mode switch, `activateDigitalMode()` / `activateAnalogMode()` reconfigure `pinMode` to put the unused pin into high-Z (`INPUT`) to stop loading the sensor output.

### Safe Serial Output from ESP-NOW Callbacks

The `onDataRecv()` callback runs in the Wi-Fi task, not in `loop()`. Calling `Serial.print()` directly from this callback produces truncated or garbled output.

**Solution**: All status output is formatted into fixed `char[]` buffers with `snprintf()` inside the callback, setting a `volatile bool` flag. The `espnow_process()` function (called from `loop()`) checks flags and prints the complete strings.

### Quadrature Encoder RPM Measurement

The 25GA370 280RPM DC Gear Motor includes a built-in quadrature encoder with approximately 11 PPR (pulses per revolution) on the motor shaft. With full quadrature decoding (4× counting) and a gear ratio of ~21.3:1, this yields approximately 937 counts per output-shaft revolution.

**Implementation**:
- Interrupts are attached to both Phase A (GPIO0) and Phase B (GPIO1) on `CHANGE`.
- Both ISRs implement standard quadrature logic: when Phase A changes, if A == B then decrement, else increment (and vice versa for Phase B changes).
- RPM is recalculated every 100 ms by reading and resetting the pulse count atomically.
- The `Encoder` class is attached to `MotorController` via `attachEncoder()`, and `MotorController::update()` calls `Encoder::update()` internally.
- If the rotation direction appears inverted, swap the Phase A and Phase B wires.

**Key constants** (in `Encoder.h`):
| Constant | Value | Notes |
|----------|-------|-------|
| `kEncoderPPR` | 11.0 | Encoder disk pulses per motor-shaft revolution |
| `kGearRatio` | 21.3 | Gearbox ratio (adjust for your motor variant) |
| `kCountsPerOutputRev` | ~937.2 | PPR × 4 × gear_ratio |
| `kRpmSampleIntervalMs` | 100 | RPM recalculation period |

### TCS34725 Autorange

The color sensor uses an autorange system based on the DN40 application note (ductsoup's autorange implementation). A 5-entry gain/integration-time table automatically adjusts for dim-to-bright conditions:

| Index | Gain | Integration Time | Min Count | Max Count |
|-------|------|-------------------|-----------|-----------|
| 0 | 60x | 614 ms | 0 (start) | 20000 |
| 1 | 60x | 154 ms | 4990 | 63000 |
| 2 | 16x | 154 ms | 16790 | 63000 |
| 3 | 4x | 154 ms | 15740 | 63000 |
| 4 | 1x | 154 ms | 15740 | 0 (end) |

When the clear channel count exceeds `max_count`, the index moves up (less sensitive). When below `min_count`, the index moves down (more sensitive). After a range change, the sensor waits for 2x the integration time before re-reading.

---

## Communication Pattern — Hybrid Poll + On-Change

The S3 hub uses a **hybrid** strategy:

1. **Polling (regular)**: The S3 iterates through slots 1-5, sending a `kPacketPoll` to each. The C3 responds immediately with its current status. If no response within 30 ms, the device is marked offline and the hub advances to the next slot. A full cycle takes ~50-75 ms (~13-20 Hz).

2. **On-change (urgent)**: If a C3 detects a significant state change (e.g., IR obstacle detected, motor state changed via command), it pushes an unsolicited status packet immediately without waiting for the next poll. The S3 accepts and processes these between poll cycles.

This gives predictable refresh rates via polling, plus low-latency change detection via on-change pushes.

### Poll Cycle Timing

| Parameter | Value |
|-----------|-------|
| Poll timeout (per device) | 30 ms |
| Inter-device gap | 2 ms |
| Full cycle (5 devices, all online) | ~50-75 ms |
| Effective refresh rate | ~13-20 Hz |
| Change notification latency | < 10 ms (unsolicited push) |

---

## Packet Types

```cpp
enum PacketType : uint8_t {
  kPacketText         = 1,  // Text message (unused)
  kPacketControl      = 2,  // Motor control    (S3->C3)
  kPacketStatus       = 3,  // Motor status     (C3->S3)
  kPacketIRControl    = 4,  // IR mode switch   (S3->C3)
  kPacketIRStatus     = 5,  // IR sensor status (C3->S3)
  kPacketColorStatus  = 6,  // Color status     (C3->S3)
  kPacketColorControl = 7,  // Color control    (S3->C3)
  kPacketMacAddr      = 8,  // MAC announce     (reserved)
  kPacketPoll         = 9   // Poll request     (S3->C3)
};
```

### PollPacket — Poll request (S3 -> C3)

```cpp
struct PollPacket {
  uint8_t type;   // Always kPacketPoll (9)
};
```

**Size**: 1 byte
**Behaviour on C3**: Immediately responds with the status packet for its registered subsystem (motor, IR, or color).

---

### motorControlPacket -- Motor command (S3 -> C3)

```cpp
struct motorControlPacket {
  uint8_t type;        // Always kPacketControl (2)
  uint8_t duty_cycle;  // 0-100 (mapped on C3 to 30-95% actual, <10 = stop)
  uint8_t direction;   // 0=reverse, 1=forward
  uint8_t enable;      // 0=stop, 1=run
};
```

**Size**: 4 bytes
**Duty mapping on C3**: input 0-100 is linearly mapped to actual PWM duty 30-95%. Inputs below 10 result in motor stop (0% duty).

### MotorStatusPacket -- Motor state + RPM (C3 -> S3)

```cpp
struct MotorStatusPacket {
  uint8_t type;        // Always kPacketStatus (3)
  uint8_t duty_cycle;  // Current mapped duty cycle %
  uint8_t direction;   // 0=reverse, 1=forward
  uint8_t enable;      // 0=stopped, 1=running
  uint8_t rpm_high;    // RPM >> 8
  uint8_t rpm_low;     // RPM & 0xFF
};
```

**Size**: 6 bytes
**RPM**: reconstruct as `(rpm_high << 8) | rpm_low`
**Sent**: On poll response + on state change (unsolicited push)

**S3 JSON output**:
```json
{"slot":3,"type":"motor","online":true,"duty":75,"dir":1,"en":1,"rpm":250}
```

---

### IRControlPacket -- Switch IR mode (S3 -> C3)

```cpp
struct IRControlPacket {
  uint8_t type;   // Always kPacketIRControl (4)
  uint8_t mode;   // 0 = digital, 1 = analog
};
```

**Size**: 2 bytes

### IRStatusPacket -- IR sensor reading (C3 -> S3)

```cpp
struct IRStatusPacket {
  uint8_t type;           // Always kPacketIRStatus (5)
  uint8_t mode;           // 0 = digital, 1 = analog
  uint8_t digital_state;  // 0 = clear, 1 = obstacle
  uint8_t analog_high;    // analog_value >> 8
  uint8_t analog_low;     // analog_value & 0xFF
};
```

**Size**: 5 bytes
**Analog**: reconstruct as `(analog_high << 8) | analog_low` (0-4095, 12-bit ADC)
**Sent**: On poll response + on state change (unsolicited push)

---

### ColorControlPacket -- Color sensor command (S3 -> C3)

```cpp
struct ColorControlPacket {
  uint8_t type;      // Always kPacketColorControl (7)
  uint8_t command;   // 0=RGB, 1=Lux, 2=RGB+Lux, 3=LED on, 4=LED off
};
```

**Size**: 2 bytes

**Command values** (`ColorCommand` enum on C3):

| Value | Name | Effect |
|-------|------|--------|
| 0 | `kColorCmdRGB` | Switch to RGB-only display mode |
| 1 | `kColorCmdLux` | Switch to lux-only display mode |
| 2 | `kColorCmdRGBLux` | Switch to RGB+Lux display mode (default) |
| 3 | `kColorCmdLedOn` | Turn TCS34725 onboard LED on (GPIO4 HIGH) |
| 4 | `kColorCmdLedOff` | Turn TCS34725 onboard LED off (GPIO4 LOW) |

**Mode commands** (0-2) trigger an immediate status packet back to S3 (via `state_changed_`).
**LED commands** (3-4) do **not** trigger a status packet -- they only toggle the LED pin.

### ColorStatusPacket -- Color sensor reading (C3 -> S3)

```cpp
struct ColorStatusPacket {
  uint8_t type;       // Always kPacketColorStatus (6)
  uint8_t mode;       // 0=RGB, 1=Lux, 2=RGB+Lux
  uint8_t r;          // IR-compensated red, normalized 0-255
  uint8_t g;          // IR-compensated green, normalized 0-255
  uint8_t b;          // IR-compensated blue, normalized 0-255
  uint8_t lux_high;   // Lux >> 8
  uint8_t lux_low;    // Lux & 0xFF
};
```

**Size**: 7 bytes
**Lux**: reconstruct as `(lux_high << 8) | lux_low`
**RGB**: IR-compensated via DN40 algorithm, then normalized to 0-255 relative to the compensated clear channel
**Sent**: On poll response + on significant change (unsolicited push)
**Change thresholds**: RGB channel differs by >8 counts, or lux differs by >10

**S3 JSON output**:
```json
{"slot":1,"type":"color","online":true,"mode":2,"r":128,"g":64,"b":32,"lux":450}
```

> **Note**: The sensor always reads all channels internally regardless of mode. The mode only controls what fields the S3 displays.

---

## File Reference

### C3 Board -- `C3/include/Encoder.h`

Declares the `Encoder` class for quadrature decoding of the 25GA370 motor's built-in encoder.

**Pin constants**: `kEncoderPinA = 0` (GPIO0), `kEncoderPinB = 1` (GPIO1).

**Motor constants**: `kEncoderPPR = 11.0`, `kGearRatio = 21.3`, `kCountsPerOutputRev = 937.2`.

**Key API**:
```cpp
Encoder encoder(kEncoderPinA, kEncoderPinB);  // or Encoder encoder; (uses defaults)
encoder.init();          // Configure pins INPUT_PULLUP, attach interrupts
encoder.update();        // Recalculate RPM (every 100ms internally)
encoder.getRPM();        // Latest output-shaft RPM (0-65535)
encoder.getCount();      // Raw quadrature count since last update
encoder.resetCount();    // Zero the pulse counter
```

---

### C3 Board -- `C3/src/Encoder.cpp`

Implementation of `Encoder`. Uses a static singleton pointer for ISR routing (only one encoder instance supported).

**`init()`** -- Sets both pins to `INPUT_PULLUP`, attaches interrupts on `CHANGE` for both Phase A and Phase B.

**`update()`** -- Every `kRpmSampleIntervalMs` (100 ms): atomically reads and resets the pulse count, converts to output-shaft RPM using `kCountsPerOutputRev`.

**`handleA()` / `handleB()`** -- Full quadrature decoding (4× resolution). When Phase A changes: if A == B, decrement; else increment. Phase B is the inverse. This gives 4 counts per encoder pulse.

---

### C3 Board -- `C3/include/motorControl.h`

Declares `MotorController`, `motorControlPacket`, `MotorStatusPacket`, and motor hardware constants.

**Key constants**:
| Constant | Value | Notes |
|----------|-------|-------|
| `kEnablePin` | 2 | DRV8835 BENABLE (PWM) |
| `kPhasePin` | 3 | DRV8835 BPHASE (direction) |
| `kDefaultFreqHz` | 20000 | 20 kHz PWM |
| `kDutyLowerLimit` | 30 | Minimum mapped duty % |
| `kDutyUpperLimit` | 95 | Maximum mapped duty % |
| `kDutyStopThreshold` | 10 | Input below this = stop |

**Key API**:
```cpp
MotorController motor;
motor.initialize();              // Configure PWM and direction pins
motor.attachEncoder(&encoder);   // Attach encoder for RPM measurement
motor.setDutyCycle(75);          // 0-100 input, mapped to 30-95% actual
motor.setDirection(true);        // true=forward, false=reverse
motor.setEnabled(true);          // Enable/disable PWM output
motor.update();                  // Ramp duty + update encoder RPM
motor.handleControl(cmd);        // Apply motorControlPacket
motor.getRPM();                  // Get encoder RPM (0 if no encoder)
motor.getCurrentDutyCycle();     // Current ramping duty %
motor.getTargetDutyCycle();      // Target duty %
motor.getDirection();            // true=forward
motor.isEnabled();               // true=running
motor.checkAndClearStateChanged(); // Consume state-changed flag
```

---

### C3 Board -- `C3/src/motorControl.cpp`

Implementation of `MotorController`. Contains duty cycle mapping, soft-start ramping, PWM mode selection, and encoder integration.

**Hardware mode**: Configured for **DRV8835 PH/EN mode** (MODE pin HIGH). PHASE pin is direction, ENABLE pin is speed PWM with linear response. No PWM inversion needed for either direction.

**`initialize()`** -- Configures `kPhasePin` as OUTPUT, attaches PWM to `kEnablePin` on channel 0 (20 kHz, 10-bit).

**`setDutyCycle(input)`** -- Maps 0-100 user input through `mapDutyCycle()`: inputs below 10 → stop (0%), otherwise linearly maps to 30-95% actual duty.

**`setEnabled(enable)`** -- If disabling, zeros both `current_duty_pct_` and `target_duty_pct_` for a clean restart when re-enabled. Then calls `applyPwm()`.

**`applyPwm()`** -- Atomic PWM update:
1. Sets PHASE pin directly (no PWM)
2. If not enabled or duty=0: PWM=0 (coast)
3. If enabled: PWM = `(current_duty * 1023) / 100` (linear duty response, both directions)

**`update()`** -- Called every loop. 
- First calls `encoder_->update()` if attached
- Ramps `current_duty_pct_` toward `target_duty_pct_` by 1% every 10 ms (soft start/stop)
- Handles pending direction changes (ramp to 0 → flip direction → ramp back up)

**`handleControl(cmd)`** -- **Atomic state update** (fixed Feb 2026):
- Directly sets `direction_`, `enabled_`, and maps `duty_cycle` to `target_duty_pct_`
- If disabling: zeroes both current and target for clean slate
- Calls `applyPwm()` once and sets `state_changed_` flag
- No side effects from nested function calls

**`attachEncoder(enc)`** -- Stores encoder pointer. `getRPM()` delegates to `encoder_->getRPM()` (returns 0 if no encoder).

---

### C3 Board -- `C3/include/ColorSensor.h`

Declares the `ColorSensor` class, `ColorMode`/`ColorCommand` enums, and both packet structs (`ColorControlPacket`, `ColorStatusPacket`).

**To use**:
1. Include: `#include "ColorSensor.h"`
2. Construct with I2C pins and LED pin: `ColorSensor sensor(sda, scl, led);`
3. Call `sensor.init()` once in `setup()` -- returns `false` if TCS34725 not found.
4. Call `sensor.update()` every loop iteration.
5. Use `sensor.checkAndClearStateChanged()` to consume the flag.

**Key API**:
```cpp
ColorSensor sensor(5, 6, 4);  // SDA: GPIO5, SCL: GPIO6, LED: GPIO4
sensor.init();                  // Init I2C, TCS34725, LED pin (HIGH by default)
sensor.update();                // Non-blocking read with autorange
sensor.handleControl(cmd);      // Apply ColorControlPacket (mode or LED)
sensor.getRed();                // 0-255, IR-compensated normalized
sensor.getGreen();              // 0-255
sensor.getBlue();               // 0-255
sensor.getLux();                // DN40-calculated lux
sensor.getMode();               // kColorRGB, kColorLux, or kColorRGBLux
sensor.isSaturated();           // true if clear channel near saturation
sensor.checkAndClearStateChanged(); // Consume state-changed flag
```

---

### C3 Board -- `C3/src/ColorSensor.cpp`

Implementation of `ColorSensor`. Contains autorange table, DN40 IR compensation, lux calculation, and LED control.

**`init()`** -- Calls `Wire.begin(sda, scl)`, sets LED pin HIGH (on), initialises TCS34725 with first autorange entry (60x/614ms). Sets `state_changed_ = true` to force initial status send.

**`update()`** -- Non-blocking: only reads when integration window has elapsed (minimum 200ms floor). Performs:
1. `getRawData()` -- reads R/G/B/Clear 16-bit counts
2. **Autorange** -- adjusts gain/integration if clear channel out of bounds, re-reads after settling
3. **DN40 IR compensation** -- estimates IR component, subtracts from all channels
4. **Saturation check** -- flags if near 75% of max count for short integrations
5. **Lux calculation** -- `(R*0.136 + G*1.0 + B*-0.444) / CPL` where CPL = (integration_ms * gain) / (GA * DF)
6. **RGB normalization** -- compensated R/G/B scaled to 0-255 relative to compensated clear
7. **Change detection** -- sets `state_changed_` if any channel changes beyond threshold

**`handleControl(cmd)`** -- Switches on command value:
- Commands 0-2: Set `mode_` and flag `state_changed_` (triggers immediate status packet)
- Command 3: `digitalWrite(led_pin_, HIGH)` -- LED on
- Command 4: `digitalWrite(led_pin_, LOW)` -- LED off

---

### C3 Board -- `C3/include/IRSensor.h`

Declares the `IRSensor` class, `IRMode` enum, and both packet structs (`IRControlPacket`, `IRStatusPacket`).

**Key API**:
```cpp
IRSensor sensor(4, 3);        // GPIO4 = DO, GPIO3 = AO
sensor.init();                 // Configure pins for current mode (default: digital)
sensor.update();               // Read active pin, debounce, set state_changed_ flag
sensor.handleControl(cmd);     // Apply an IRControlPacket (mode switch)
sensor.isObstacle();           // true if DO was LOW after debounce
sensor.getAnalogValue();       // Last analogRead() result (0-4095)
sensor.getMode();              // kIRDigital or kIRAnalog
sensor.checkAndClearStateChanged(); // Consume the state-changed flag
```

> **Note**: IR sensor is not active in the current build -- removed from `main.cpp` while color sensor is being tested. GPIO4 is shared with color sensor LED.

---

### C3 Board -- `C3/src/IRSensor.cpp`

Implementation of `IRSensor`. Contains the mode-exclusive reading logic.

**`init()`** -- Calls `activateDigitalMode()` (default). Sets GPIO4 as `INPUT`, GPIO3 as `INPUT` (high-Z).

**`update()`** -- Branches on current mode:
- *Digital*: `digitalRead(GPIO4)`. Raw value must be stable for 50 ms (debounce) before `obstacle_` updates and `state_changed_` is set.
- *Analog*: `analogRead(GPIO3)`. If the new value differs from the previous by more than 50 counts, `state_changed_` is set.

**`handleControl(cmd)`** -- On mode change, calls `activateDigitalMode()` or `activateAnalogMode()` before updating the stored mode and setting `state_changed_`.

**`activateDigitalMode()`** -- Sets GPIO4 `INPUT`, GPIO3 `INPUT`. Stops ADC sampling.

**`activateAnalogMode()`** -- Sets GPIO4 `INPUT` (high-Z, releases DO line). `analogRead()` handles ADC channel setup automatically.

---

### C3 Board -- `C3/include/ESPNOW_C3.h`

Public API for the C3 ESP-NOW transport layer. Uses forward declarations only -- does **not** pull in subsystem headers.

**To use**: Include in any C3 file that needs to initialise or register subsystems.

```cpp
#include "ESPNOW_C3.h"

void espnow_init();                              // Initialise ESP-NOW, add S3 as peer
void espnow_register_motor(MotorController *m);  // Register motor subsystem
void espnow_register_ir(IRSensor *ir);           // Register IR sensor
void espnow_register_color(ColorSensor *cs);     // Register color sensor
void espnow_update();                            // Call every loop -- on-change status
```

**Packet type enum** (`PacketType`) is defined here and shared by all `.h` files on C3. Includes `kPacketPoll = 9` for poll requests from the S3 hub. Forward declarations for `MotorController`, `IRSensor`, `ColorSensor`, `motorControlPacket`, `IRControlPacket`, `ColorControlPacket`.

---

### C3 Board -- `C3/src/ESPNOW_C3.cpp`

Internal implementation of the C3 transport layer.

**Registration pattern** -- Each subsystem is registered via a pointer stored in a `namespace`-scoped variable (`g_motor`, `g_ir`, `g_color`). Subsystems that are `nullptr` are silently skipped.

**`espnow_init()`** -- Calls `esp_now_init()`, registers send/receive callbacks, and calls `ensureS3Peer()` to add the S3's MAC as a peer immediately.

**`espnow_update()`** -- Called every loop. For each registered subsystem:
- Calls `checkAndClearStateChanged()` -- if `true`, sends a status packet immediately (on-change push).
- Periodic timers have been removed -- the S3 hub polls for regular updates.

**Status senders**:
- `sendMotorStatus()` -- Builds `MotorStatusPacket` from `g_motor`'s getters (includes RPM via `getRPM()`)
- `sendIRStatus()` -- Builds `IRStatusPacket` from `g_ir`'s getters
- `sendColorStatus()` -- Builds `ColorStatusPacket` from `g_color`'s getters (includes `mode` field)

**`onDataRecv()`** -- Dispatches incoming packets by `type` byte:
- `kPacketPoll` (9) -> Immediately sends status for all registered subsystems (poll response)
- `kPacketControl` (2) -> `g_motor->handleControl()`
- `kPacketIRControl` (4) -> `g_ir->handleControl()`
- `kPacketColorControl` (7) -> `g_color->handleControl()`

---

### C3 Board -- `C3/src/main.cpp`

Entry point. Declares subsystem objects and calls init/register/update functions.

In the multi-peripheral architecture, each physical C3 board runs one of the example harness files (renamed to `main.cpp`):

| File | Subsystem | Typical board |
|------|-----------|---------------|
| `motorExample.txt` | Motor + Encoder | Slot 3, 5 |
| `IRSensorExample.txt` | IR Sensor | Slot 2, 4 |
| `colourSensorExample.txt` | Color Sensor (TCS34725) | Slot 1 |

**To deploy a C3**: Copy the appropriate example.txt to `src/main.cpp`, build, and upload.

**To add a new subsystem**:
1. Include its `.h` file.
2. Declare an instance globally.
3. Call `instance.init()` in `setup()`.
4. Register it: `espnow_register_xxx(&instance)`.
5. Call `instance.update()` in `loop()`.

---

### S3 Board -- `S3/include/ESPNOW_S3.h`

Public API for the S3 hub transport layer. Defines the peripheral registry, all packet structs, and the hub API.

**Key types**:
- `PeripheralType` enum: `kPeripheralMotor`, `kPeripheralIR`, `kPeripheralColor`
- `DeviceConfig` struct: MAC address + type (used in `kDeviceList[]`)
- `PollPacket` struct: 1-byte poll request
- All control/status packet structs (must stay in sync with C3)

**Peripheral registry**: `kDeviceList[kNumDevices]` -- compile-time array of `DeviceConfig`. Edit this to add/remove/reorder C3 boards.

**To use**:
```cpp
#include "ESPNOW_S3.h"

void espnow_hub_init();                                          // Init ESP-NOW, add all peers
void espnow_hub_process();                                       // Poll FSM + JSON output
void espnow_send_motor_cmd(uint8_t slot, uint8_t duty, uint8_t dir, uint8_t enable);
void espnow_send_ir_cmd(uint8_t slot, uint8_t mode);
void espnow_send_color_cmd(uint8_t slot, uint8_t command);
PeripheralType espnow_get_device_type(uint8_t slot);             // Lookup type by slot
bool espnow_is_device_online(uint8_t slot);                      // Liveness check
void espnow_print_registry();                                    // Print device table
```

---

### S3 Board -- `S3/src/ESPNOW_S3.cpp`

Implementation of the S3 hub transport layer with round-robin polling and JSON output.

**`espnow_hub_init()`** -- Initialises ESP-NOW, registers callbacks, adds all `kNumDevices` C3 boards as peers.

**Per-device runtime state** (`DeviceState` struct):
- Common: `online`, `new_data` flag, `last_seen` timestamp
- Motor: `duty`, `dir`, `enable`, `rpm` + motor command verification struct
- IR: `mode`, `digital_state`, `analog_value`
- Color: `mode`, `r`, `g`, `b`, `lux`

**Poll state machine** (inside `espnow_hub_process()`):
1. Send `PollPacket` to device at `g_poll_idx`
2. Wait for response (flag set by `onDataRecv()`)
3. If response within 30 ms: output JSON, mark online, advance
4. If timeout: mark offline (if no recent contact), advance
5. Between polls, check all devices for unsolicited on-change data and print

**`onDataRecv()`** -- Identifies source device by MAC via `findDeviceByMac()`. Updates the matching `DeviceState` fields and sets `new_data = true`. For motor status, also verifies against expected state (resend on mismatch, up to 5 retries at 400 ms intervals).

**Motor command verification** (per-device):
- `espnow_send_motor_cmd()` arms verification: stores expected duty/dir/enable, sets `pending = true`
- Each incoming motor status is compared against expected dir + enable
- If mismatch persists, `espnow_hub_process()` auto-resends every 400 ms, up to 5 times

**JSON output** -- One JSON object per line on Serial:
```json
{"slot":3,"type":"motor","online":true,"duty":75,"dir":1,"en":1,"rpm":250}
{"slot":2,"type":"ir","online":true,"mode":0,"digital":1,"analog":0}
{"slot":1,"type":"color","online":true,"mode":2,"r":128,"g":64,"b":32,"lux":450}
{"slot":4,"type":"ir","online":false}
```

---

### S3 Board -- `S3/src/main.cpp`

Entry point for the S3 hub. Handles serial command input with slot routing and calls `espnow_hub_process()` for poll cycle + JSON output.

**Command format**: `<slot>:<command>` where slot is 1-5.

**Motor commands** (slots 3, 5):

| Command | Packet Sent | Effect |
|---------|-------------|--------|
| `3:75` | `motorControlPacket {2, 75, dir, 1}` | Set duty cycle to 75% on Motor-1 |
| `5:F` | `motorControlPacket {2, duty, 1, en}` | Set Motor-2 direction to forward |
| `3:R` | `motorControlPacket {2, duty, 0, en}` | Set Motor-1 direction to reverse |
| `5:S` | `motorControlPacket {2, 0, dir, 0}` | Stop Motor-2 (duty=0, enable=0) |
| `3:0`-`3:100` | varies | Set duty (< 10 = stop) |

**Color commands** (slot 1):

| Command | Packet Sent | Effect |
|---------|-------------|--------|
| `1:C` | `ColorControlPacket {7, 0}` | RGB-only display mode |
| `1:L` | `ColorControlPacket {7, 1}` | Lux-only display mode |
| `1:CL` or `1:B` | `ColorControlPacket {7, 2}` | RGB + Lux display mode |
| `1:O` | `ColorControlPacket {7, 3}` | Turn color sensor LED on |
| `1:X` | `ColorControlPacket {7, 4}` | Turn color sensor LED off |

**IR commands** (slots 2, 4):

| Command | Packet Sent | Effect |
|---------|-------------|--------|
| `2:D` | `IRControlPacket {4, 0}` | Switch IR-1 to digital mode |
| `4:A` | `IRControlPacket {4, 1}` | Switch IR-2 to analog mode |

The S3 maintains per-motor local state so that partial commands (e.g. just `F` for forward) send the full current state including duty.

**Loop structure**:
```
loop()
  +-- espnow_hub_process()  -> poll FSM + print JSON for new data
  +-- Serial.available()    -> read command, route to slot, send packet
```

---

## Message Flow

### Poll Cycle (S3 round-robin)
```
S3: sendPoll(slot 1)  -> PollPacket {9} to A0:76:4E:7B:3C:38
C3 Slot 1: onDataRecv() -> sendColorStatus()
    -> ColorStatusPacket {6, 2, 128, 64, 32, lux_h, lux_l}
S3: onDataRecv() -> g_dev[0].new_data = true
S3: espnow_hub_process() -> print JSON:
    {"slot":1,"type":"color","online":true,"mode":2,"r":128,"g":64,"b":32,"lux":450}

S3: sendPoll(slot 2) -> PollPacket {9} to A0:76:4E:4A:09:08
C3 Slot 2: responds with IRStatusPacket
S3: {"slot":2,"type":"ir","online":true,"mode":0,"digital":0,"analog":0}

... (slots 3, 4, 5) ...

S3: cycle repeats (~60ms total)
```

### Motor Control via Slot (S3 -> C3 -> S3)
```
S3: User types "3:75" (slot 3, duty 75%)
S3: espnow_send_motor_cmd(3, 75, 1, 1) -> motorControlPacket {2, 75, 1, 1}
    sent to A0:76:4E:7B:9A:B4 (Motor-1)

C3 Slot 3: onDataRecv() -> g_motor->handleControl(cmd)
C3: state_changed_ = true
C3: espnow_update() -> on-change push -> sendMotorStatus()
    -> MotorStatusPacket {3, 79, 1, 1, rpm_h, rpm_l}

S3: onDataRecv() -> updates g_dev[2], new_data = true
S3: espnow_hub_process() -> print JSON:
    {"slot":3,"type":"motor","online":true,"duty":79,"dir":1,"en":1,"rpm":250}
```

### Color Command via Slot (S3 -> C3 -> S3)
```
S3: User types "1:C" (slot 1, RGB mode)
S3: espnow_send_color_cmd(1, 0) -> ColorControlPacket {7, 0}
    sent to A0:76:4E:7B:3C:38 (Color-1)

C3 Slot 1: onDataRecv() -> g_color->handleControl(cmd)
C3: mode_ = kColorRGB, state_changed_ = true
C3: espnow_update() -> on-change push -> sendColorStatus()

S3: {"slot":1,"type":"color","online":true,"mode":0,"r":128,"g":64,"b":32,"lux":450}
```

### Device Offline Detection
```
S3: sendPoll(slot 4) -> PollPacket {9} to 34:B4:72:48:F1:A8

[30ms timeout -- no response]

S3: g_dev[3].online = false
S3: {"slot":4,"type":"ir","online":false}
S3: advance to slot 5
```

### Unsolicited On-Change Push (between polls)
```
S3: currently polling slot 2, waiting for response...

C3 Slot 3 (Motor-1): obstacle causes RPM change
C3: state_changed_ = true -> sendMotorStatus() [unsolicited]

S3: onDataRecv() from slot 3 MAC -> updates g_dev[2].new_data = true
S3: espnow_hub_process() -> prints slot 3 JSON between poll steps:
    {"slot":3,"type":"motor","online":true,"duty":79,"dir":1,"en":1,"rpm":180}
```

---

## Protocol Characteristics

| Property | Value |
|----------|-------|
| Max packet size | 250 bytes (ESP-NOW limit) |
| Typical latency (single packet) | < 10 ms |
| Poll timeout per device | 30 ms |
| Full poll cycle (5 devices) | ~50-75 ms |
| Effective refresh rate | ~13-20 Hz |
| On-change latency | < 10 ms (unsolicited push) |
| Motor command resend interval | 400 ms (max 5 retries) |
| Number of peers | 5 (max 20 unencrypted on ESP32) |
| Encryption | Disabled |
| Serial output format | JSON, one object per line |

---

## PlatformIO Dependencies

### C3 (`C3/platformio.ini`)
```ini
lib_deps =
    adafruit/Adafruit NeoPixel@^1.12.0
    adafruit/Adafruit TCS34725@^1.4.2
    adafruit/Adafruit BusIO@^1.16.1
```

### S3 (`S3/platformio.ini`)
```ini
lib_deps =
    adafruit/Adafruit NeoPixel@^1.12.0
```

---

## Motor Control Bug Fixes (February 2026)

### Issue 1: Motor didn't respond without USB (COM18) connected
**Root cause**: `handleControl()` called `Serial.print()` to log commands. When no USB host was present, the Serial output buffer blocked, freezing packet processing.

**Fix**: Removed all `Serial.print()` from `handleControl()`. Status feedback comes via ESPNOW packets to S3, not local serial.

### Issue 2: 0→100 duty ignored; had to re-input 100
**Root cause**: When enable=0, `handleControl()` never called `setDutyCycle()`, leaving `target_duty_pct_` at stale value (95 from previous run). When 100 was sent next, mapper returned 95, which equaled current → `update()` found nothing to do.

**Fix**: Rewrote `handleControl()` atomically. Now:
- If `enable == 0`: explicitly zero both `current_duty_pct_` and `target_duty_pct_` for a clean slate
- If `enable == 1`: set direction, map duty, set target, call `applyPwm()` once

### Issue 3: Reverse direction speed inverted (low duty = fast, high duty = slow)
**Root cause**: Code assumed **IN/IN mode** (MODE pin floating/LOW) with PWM inversion for reverse. User actually wired **PH/EN mode** (MODE pin HIGH).

**Fix**: Rewrote `applyPwm()` for PH/EN mode:
- PHASE pin = direction (HIGH or LOW, no PWM)
- ENABLE pin = speed PWM (linear: higher duty = faster, both directions)

### Issue 4: State drift; commands sometimes not acknowledged
**Root cause**: ESP-NOW is fire-and-forget. Packets can be lost. No verification on S3 side.

**Fix**: Added S3-side motor state verification layer (`ESPNOW_S3.cpp`):
- Each motor slot has its own `motor_exp` struct tracking: duty, dir, enable, sent_time, resend_count, pending
- `espnow_send_motor_cmd(slot, duty, dir, en)` arms verification for that slot
- Each incoming motor status from the matching MAC is compared against expected
- If direction or enable doesn't match and 400ms+ elapsed, auto-resend (up to 5 retries)

**Example flow** (multi-device):
```
S3: espnow_send_motor_cmd(3, 100, 1, 1)   // Slot 3 = Motor-1
    -> sends to A0:76:4E:7B:9A:B4, arms g_dev[2].motor_exp

[400ms passes, no matching status received from slot 3]

S3: auto-resends motorControlPacket to slot 3   // Resend #1
```

---

## Known Limitations

1. **Simultaneous DO+AO reading causes interference** -- solved by mode-exclusive pin reading (see design decision above).
2. **GPIO10 is unusable on ESP32-C3-MINI-1** -- tied to SPI flash; any `digitalRead()` on it will always return the same state regardless of external signal.
3. **GPIO4 pin conflict** -- used by both IR sensor (digital output) and color sensor (LED control). Each C3 now runs only one subsystem, avoiding the conflict.
4. **No packet loss detection** -- packets are fire-and-forget with no sequence numbers or ACK beyond the ESP-NOW layer's `onDataSent` callback. Motor commands use S3-side resend verification.
5. **Color sensor always reads all channels** -- mode only affects what the S3 displays, not what the C3 samples. All R/G/B/Lux values are always present in the status packet.
6. **Single encoder instance per C3** -- the `Encoder` class uses a static singleton for ISR routing; only one encoder per C3.
7. **Encoder gear ratio is approximate** -- `kGearRatio` is set to 21.3 for the 25GA370 280RPM variant. Adjust in `Encoder.h` if your motor has a different gear ratio.
8. **S3 MAC hardcoded on all C3s** -- each C3 has the S3's MAC (`30:ED:A0:27:8F:A4`) compiled in. If the S3 board changes, all C3 firmwares must be rebuilt.
9. **Poll timeout marks offline aggressively** -- a C3 that misses 3 consecutive polls (90 ms) is marked offline. Transient Wi-Fi interference may cause brief offline flickers.

---

## Adding a New Peripheral C3

To add a new C3 board (e.g. a third motor, an ultrasonic sensor):

**S3 side**:
1. Add the C3's MAC and type to `kDeviceList[]` in `ESPNOW_S3.h`.
2. Increment `kNumDevices`.
3. If it's a new sensor type: add packet structs, a new `PeripheralType` enum value, `onDataRecv()` dispatch branch, JSON printer, and `espnow_send_xxx_cmd()` function.
4. Add command handling in `main.cpp` for the new type.

**C3 side** (for a new sensor type):
1. Create `MySensor.h` / `MySensor.cpp` with `init()`, `update()`, `handleControl()`, `checkAndClearStateChanged()`.
2. Define control/status packet structs in `MySensor.h` and assign new `PacketType` values in `ESPNOW_C3.h`.
3. Add `espnow_register_mysensor(MySensor *s)` to `ESPNOW_C3.h`.
4. In `ESPNOW_C3.cpp`: add `g_mysensor` pointer, `sendMySensorStatus()`, dispatch in `onDataRecv()`, and send in poll handler.
5. Create a new example harness (e.g. `mySensorExample.txt`).

**For an existing sensor type** (e.g. adding a third IR sensor):
1. Flash the same IR example firmware to the new C3 board.
2. Add its MAC + `kPeripheralIR` to `kDeviceList[]` on S3.
3. No other code changes needed.

