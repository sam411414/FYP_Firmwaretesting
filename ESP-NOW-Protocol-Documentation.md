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

## Packet Types

```cpp
enum PacketType : uint8_t {
  kPacketText         = 1,  // Text message (unused in current build)
  kPacketControl      = 2,  // Motor control    (S3->C3)
  kPacketStatus       = 3,  // Motor status     (C3->S3)
  kPacketIRControl    = 4,  // IR mode switch   (S3->C3)
  kPacketIRStatus     = 5,  // IR sensor status (C3->S3)
  kPacketColorStatus  = 6,  // Color status     (C3->S3)
  kPacketColorControl = 7   // Color control    (S3->C3)
};
```

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
**Sent**: immediately on state change + every 1000 ms (periodic heartbeat)

**S3 display format**:
```
[Motor] 75% FWD ON 250RPM
[Motor] 0% FWD OFF 0RPM
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
**Sent**: immediately on state change + every 500 ms (periodic heartbeat)

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
**Sent**: immediately on significant change + every 500 ms (periodic heartbeat)
**Change thresholds**: RGB channel differs by >8 counts, or lux differs by >10

**S3 display format** (depends on `mode` field):

| Mode | Output |
|------|--------|
| 0 (RGB) | `[Color] R:128 G:64 B:32` |
| 1 (Lux) | `[Color] 450lx` |
| 2 (RGB+Lux) | `[Color] R:128 G:64 B:32  450lx` |

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
void espnow_update();                            // Call every loop -- dispatches status packets
```

**Packet type enum** (`PacketType`) is defined here and shared by all `.h` files on C3. Forward declarations for `MotorController`, `IRSensor`, `ColorSensor`, `motorControlPacket`, `IRControlPacket`, `ColorControlPacket`.

---

### C3 Board -- `C3/src/ESPNOW_C3.cpp`

Internal implementation of the C3 transport layer.

**Registration pattern** -- Each subsystem is registered via a pointer stored in a `namespace`-scoped variable (`g_motor`, `g_ir`, `g_color`). Subsystems that are `nullptr` are silently skipped.

**`espnow_init()`** -- Calls `esp_now_init()`, registers send/receive callbacks, and calls `ensureS3Peer()` to add the S3's MAC as a peer immediately.

**`espnow_update()`** -- Called every loop. For each registered subsystem:
- Calls `checkAndClearStateChanged()` -- if `true`, sends a status packet immediately.
- Checks the periodic timer -- if elapsed, sends a status packet regardless of change.

**Status senders**:
- `sendMotorStatus()` -- Builds `MotorStatusPacket` from `g_motor`'s getters (includes RPM via `getRPM()`)
- `sendIRStatus()` -- Builds `IRStatusPacket` from `g_ir`'s getters
- `sendColorStatus()` -- Builds `ColorStatusPacket` from `g_color`'s getters (includes `mode` field)

**`onDataRecv()`** -- Dispatches incoming packets by `type` byte:
- `kPacketControl` (2) -> `g_motor->handleControl()`
- `kPacketIRControl` (4) -> `g_ir->handleControl()`
- `kPacketColorControl` (7) -> `g_color->handleControl()`

**Periodic intervals**:
| Subsystem | Interval |
|-----------|----------|
| Motor status | Every 1000 ms |
| IR status | Every 500 ms |
| Color status | Every 500 ms |

---

### C3 Board -- `C3/src/main.cpp`

Entry point. Declares subsystem objects and calls init/register/update functions.

**To add a new subsystem**:
1. Include its `.h` file.
2. Declare an instance globally.
3. Call `instance.init()` in `setup()`.
4. Register it: `espnow_register_xxx(&instance)`.
5. Call `instance.update()` in `loop()`.

**Current state**:
```cpp
MotorController motor;
Encoder encoder(kEncoderPinA, kEncoderPinB);  // GPIO0 = Phase A, GPIO1 = Phase B
ColorSensor color_sensor(5, 6, 4);             // SDA: GPIO5, SCL: GPIO6, LED: GPIO4
// IR sensor removed while color sensor testing (GPIO4 conflict)
```

---

### S3 Board -- `S3/include/ESPNOW_S3.h`

Public API for the S3 ESP-NOW transport layer. Declares the same packet structs as the C3 side (must stay in sync).

Packet structs defined here: `motorControlPacket`, `MotorStatusPacket`, `IRControlPacket`, `IRStatusPacket`, `ColorControlPacket`, `ColorStatusPacket`.

**To use**:
```cpp
#include "ESPNOW_S3.h"

void espnow_init_sender(const uint8_t *target_mac);  // Init and add C3 as peer
void espnow_send_motor_control(uint8_t duty, uint8_t direction, uint8_t enable);  // Send motorControlPacket
void espnow_send_ir_control(uint8_t mode);            // Send IRControlPacket (0=digital, 1=analog)
void espnow_send_color_control(uint8_t command);      // Send ColorControlPacket (0-4)
void espnow_process();                                // Call every loop -- prints buffered status
```

---

### S3 Board -- `S3/src/ESPNOW_S3.cpp`

Implementation of the S3 transport layer with motor state verification.

**`espnow_init_sender(mac)`** -- Initialises ESP-NOW, registers send/receive callbacks, adds C3's MAC as a peer.

**Motor state verification** (added Feb 2026):
- Struct `MotorExpected` stores commanded state: duty, direction, enable, sent_time, resend_count, pending flag
- New function `espnow_set_motor_expected(duty, dir, en)` arms verification:
  - Stores expected state
  - Zeros resend counter
  - Sets pending=true
- `onDataRecv()` compares incoming motor status (direction, enable) against expected
  - If match found: pending=false (acknowledged)
  - If mismatch: stays pending for resend
- `espnow_process()` resends pending commands every 400ms, up to 5 retries
  - After 5 failed resends, gives up (user should re-input command)

**`onDataRecv()`** -- Runs in the Wi-Fi task (not `loop()`). **Never call `Serial.print()` directly** -- it will be interrupted. Instead:
1. Parses incoming packet by type byte
2. Formats output string into fixed buffer with `snprintf()`
3. Sets volatile flag to signal `loop()` that buffer is ready
4. For motor status: verifies against expected state

**Receive buffers**:
| Buffer | Flag | Source Packet | Verified? |
|--------|------|---------------|-----------|
| `g_motor_line[64]` | `g_motor_new` | `MotorStatusPacket` | Yes (state compared) |
| `g_ir_line[32]` | `g_ir_new` | `IRStatusPacket` | No |
| `g_color_line[48]` | `g_color_new` | `ColorStatusPacket` | No |

**`espnow_process()`** -- Called from `loop()`. For each flag:
1. **motor**: Check if pending command needs resend (400ms elapsed, < 5 resends)
2. **all**: If flag set, print buffered line and clear flag

**`espnow_send_motor_control(duty, direction, enable)`** -- Builds `motorControlPacket` and sends. **Always call `espnow_set_motor_expected()` after this** (done in `motor.cpp` harness).

**`espnow_set_motor_expected(duty, direction, enable)`** -- Arms state verification. Compare-point is direction + enable bits only (duty may still be ramping on C3 side).

**`espnow_send_ir_control(mode)`** -- Builds and sends `IRControlPacket {4, mode}`.

**`espnow_send_color_control(command)`** -- Builds and sends `ColorControlPacket {7, command}`.

**Serial output formats**:
| Packet | Mode | Output |
|--------|------|--------|
| Motor | -- | `[Motor] 75% FWD ON 250RPM` |
| IR | Digital -- obstacle | `[IR] OBSTACLE` |
| IR | Digital -- clear | `[IR] Clear` |
| IR | Analog | `[IR] analog: 2048` |
| Color | 0 (RGB) | `[Color] R:128 G:64 B:32` |
| Color | 1 (Lux) | `[Color] 450lx` |
| Color | 2 (RGB+Lux) | `[Color] R:128 G:64 B:32  450lx` |

---

### S3 Board -- `S3/src/main.cpp`

Entry point for the S3. Handles serial command input and calls `espnow_process()` to print incoming status.

**Available commands** (typed into serial monitor):

| Command | Packet Sent | Effect |
|---------|-------------|--------|
| `0`-`100` | `motorControlPacket {2, N, dir, 1}` | Set motor duty cycle (< 10 = stop) |
| `F` | `motorControlPacket {2, duty, 1, en}` | Set motor direction to forward |
| `R` | `motorControlPacket {2, duty, 0, en}` | Set motor direction to reverse |
| `S` | `motorControlPacket {2, 0, dir, 0}` | Stop motor (duty=0, enable=0) |
| `digital` | `IRControlPacket {4, 0}` | Switch C3 IR to digital mode |
| `analog` | `IRControlPacket {4, 1}` | Switch C3 IR to analog mode |
| `c` | `ColorControlPacket {7, 0}` | Switch color to RGB-only display |
| `l` | `ColorControlPacket {7, 1}` | Switch color to lux-only display |
| `cl` | `ColorControlPacket {7, 2}` | Switch color to RGB+Lux display |
| `ledon` | `ColorControlPacket {7, 3}` | Turn color sensor LED on |
| `ledoff` | `ColorControlPacket {7, 4}` | Turn color sensor LED off |

The S3 maintains motor state locally (`g_motor_duty`, `g_motor_dir`, `g_motor_enable`) so that partial commands (e.g. just changing direction) send the full current state.

**Loop structure**:
```
loop()
  +-- espnow_process()     -> print any buffered Motor/IR/Color status from C3
  +-- Serial.available()   -> read command, send control packet
```

---

## Message Flow

### Motor Control (S3 -> C3 -> S3)
```
S3: User types "75" (duty cycle)
S3: espnow_send_motor_control(75, 1, 1) -> motorControlPacket {2, 75, 1, 1}

C3: onDataRecv() -> g_motor->handleControl(cmd)
C3: setDirection(forward), setEnabled(true), setDutyCycle(75)
C3: mapDutyCycle(75) -> mapped to ~79% actual duty
C3: state_changed_ = true
C3: espnow_update() detects flag -> sendMotorStatus()
    -> MotorStatusPacket {3, 79, 1, 1, rpm_high, rpm_low}

S3: onDataRecv() -> snprintf("[Motor] 79% FWD ON 250RPM") -> g_motor_new = true
S3: loop() -> espnow_process() -> Serial.println("[Motor] 79% FWD ON 250RPM")
```

### Motor Direction Change (S3 -> C3 -> S3)
```
S3: User types "R" (reverse)
S3: espnow_send_motor_control(75, 0, 1) -> motorControlPacket {2, 75, 0, 1}

C3: onDataRecv() -> g_motor->handleControl(cmd)
C3: setDirection(false) -> GPIO3 LOW
C3: state_changed_ = true -> sendMotorStatus()

S3: [Motor] 79% REV ON 248RPM
```

### Motor Stop (S3 -> C3 -> S3)
```
S3: User types "S" (stop)
S3: espnow_send_motor_control(0, 0, 0) -> motorControlPacket {2, 0, 0, 0}

C3: setEnabled(false) -> PWM output = 0
C3: state_changed_ = true -> sendMotorStatus()

S3: [Motor] 0% REV OFF 0RPM
```

### IR Mode Switch (S3 -> C3 -> S3)
```
S3: User types "analog"
S3: espnow_send_ir_control(1) -> IRControlPacket {4, 1}

C3: onDataRecv() -> g_ir->handleControl(cmd)
C3: activateAnalogMode() -- GPIO4 goes high-Z, ADC enabled on GPIO3
C3: state_changed_ = true
C3: espnow_update() detects flag -> sendIRStatus()
    -> IRStatusPacket {5, 1, obstacle, analog_high, analog_low}

S3: onDataRecv() -> snprintf("[IR] analog: XXXX") -> g_ir_new = true
S3: loop() -> espnow_process() -> Serial.println("[IR] analog: XXXX")
```

### Color Mode Switch (S3 -> C3 -> S3)
```
S3: User types "c"
S3: espnow_send_color_control(0) -> ColorControlPacket {7, 0}

C3: onDataRecv() -> g_color->handleControl(cmd)
C3: mode_ = kColorRGB, state_changed_ = true
C3: espnow_update() detects flag -> sendColorStatus()
    -> ColorStatusPacket {6, 0, r, g, b, lux_high, lux_low}

S3: onDataRecv() -> mode==0 -> snprintf("[Color] R:%d G:%d B:%d")
S3: loop() -> espnow_process() -> Serial.println("[Color] R:128 G:64 B:32")
```

### Color LED Control (S3 -> C3, no response)
```
S3: User types "ledoff"
S3: espnow_send_color_control(4) -> ColorControlPacket {7, 4}

C3: onDataRecv() -> g_color->handleControl(cmd)
C3: digitalWrite(led_pin_, LOW) -- LED turns off
C3: (no state_changed_ flag set -- no status packet sent)
```

### Periodic Status (every 500-1000 ms)
```
C3: espnow_update() timer fires for each registered subsystem
C3: sendMotorStatus() (1s) / sendIRStatus() (500ms) / sendColorStatus() (500ms)
C3: Sends packet to S3

S3: Receives, formats into buffer, prints from loop()
```

---

## Protocol Characteristics

| Property | Value |
|----------|-------|
| Max packet size | 250 bytes (ESP-NOW limit) |
| Typical latency | < 10 ms |
| IR status interval | 500 ms + on change |
| Color status interval | 500 ms + on change |
| Motor status interval | 1000 ms + on change |
| Encryption | Disabled |
| Retransmission | None (fire-and-forget) |

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
- `espnow_set_motor_expected(duty, dir, en)` stores the commanded state
- Each incoming motor status is compared against expected
- If direction or enable doesn't match and 400ms+ elapsed, auto-resend (up to 5 retries)
- Call `espnow_set_motor_expected()` after every motor command from S3

**Example flow**:
```
S3: espnow_send_motor_control(100, 1, 1)
S3: espnow_set_motor_expected(100, 1, 1)  // Arm verification

[400ms passes, no matching status received]

S3: espnow_send_motor_control(100, 1, 1)  // Resend
```

---

## Known Limitations

1. **Simultaneous DO+AO reading causes interference** -- solved by mode-exclusive pin reading (see design decision above).
2. **GPIO10 is unusable on ESP32-C3-MINI-1** -- tied to SPI flash; any `digitalRead()` on it will always return the same state regardless of external signal.
3. **GPIO4 pin conflict** -- used by both IR sensor (digital output) and color sensor (LED control). Only one can be active at a time; IR is currently removed from `main.cpp`.
4. **Single peer** -- S3 is hardcoded to one C3 MAC and vice versa.
5. **No packet loss detection** -- packets are fire-and-forget with no sequence numbers or ACK beyond the ESP-NOW layer's `onDataSent` callback.
6. **Color sensor always reads all channels** -- mode only affects what the S3 displays, not what the C3 samples. All R/G/B/Lux values are always present in the status packet.
7. **Single encoder instance** -- the `Encoder` class uses a static singleton for ISR routing; only one encoder is supported at a time.
8. **Encoder gear ratio is approximate** -- `kGearRatio` is set to 21.3 for the 25GA370 280RPM variant. Adjust in `Encoder.h` if your motor has a different gear ratio.

---

## Adding a New Sensor Subsystem

To add a new sensor (e.g. ultrasonic, temperature) following the same pattern:

**C3 side**:
1. Create `MySensor.h` / `MySensor.cpp` with `init()`, `update()`, `handleControl()`, `checkAndClearStateChanged()`.
2. Define control/status packet structs in `MySensor.h` and assign new `PacketType` values in `ESPNOW_C3.h`.
3. Add `espnow_register_mysensor(MySensor *s)` to `ESPNOW_C3.h`.
4. In `ESPNOW_C3.cpp`: add `g_mysensor` pointer, `sendMySensorStatus()`, dispatch in `onDataRecv()`, periodic timer in `espnow_update()`.
5. In `main.cpp`: declare instance, call `init()`, call `espnow_register_mysensor()`, call `update()` in loop.

**S3 side**:
1. Add matching packet structs to `ESPNOW_S3.h`.
2. Add `espnow_send_mysensor_control()` function in `ESPNOW_S3.h`/`.cpp`.
3. Add dispatch branch in `onDataRecv()` in `ESPNOW_S3.cpp` using the same snprintf-buffer pattern.
4. Add command handling in `main.cpp`.

