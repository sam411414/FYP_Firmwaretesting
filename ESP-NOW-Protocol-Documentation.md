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

- **S3 -> C3**: Sends control commands (IR mode switch, color mode/LED control)
- **C3 -> S3**: Sends status packets (IR readings, color sensor readings)
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
| GPIO2 | Motor -- Enable (DRV8835) | Reserved (motor commented out) |
| GPIO3 | Motor -- Phase (DRV8835) / IR Analog (AO) | Reserved (IR not active in current build) |
| GPIO4 | Color Sensor -- LED control | `OUTPUT`, HIGH = LED on, LOW = LED off |
| GPIO5 | Color Sensor -- I2C SDA | TCS34725 |
| GPIO6 | Color Sensor -- I2C SCL | TCS34725 |
| GPIO8 | NeoPixel LED | Reserved |
| GPIO9 | BOOT button | Reserved |
| GPIO10+ | SPI Flash | **Do NOT use for GPIO** |

> **Important**: GPIO10 on the ESP32-C3-MINI-1 module is tied to SPI flash and cannot be used as general GPIO.

> **Pin conflict note**: GPIO4 is used by both the IR sensor (digital output) and the color sensor (LED control). When both sensors need to be active simultaneously, the IR digital pin must be moved to a different GPIO.

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
  kPacketControl      = 2,  // Motor control    (S3->C3) -- reserved
  kPacketStatus       = 3,  // Motor status     (C3->S3) -- reserved
  kPacketIRControl    = 4,  // IR mode switch   (S3->C3)
  kPacketIRStatus     = 5,  // IR sensor status (C3->S3)
  kPacketColorStatus  = 6,  // Color status     (C3->S3)
  kPacketColorControl = 7   // Color control    (S3->C3)
};
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
- `sendMotorStatus()` -- Builds `StatusPacket` from `g_motor`'s getters
- `sendIRStatus()` -- Builds `IRStatusPacket` from `g_ir`'s getters
- `sendColorStatus()` -- Builds `ColorStatusPacket` from `g_color`'s getters (includes `mode` field)

**`onDataRecv()`** -- Dispatches incoming packets by `type` byte:
- `kPacketControl` (2) -> `g_motor->handleControl()`
- `kPacketIRControl` (4) -> `g_ir->handleControl()`
- `kPacketColorControl` (7) -> `g_color->handleControl()`

**Periodic intervals**:
| Subsystem | Interval |
|-----------|----------|
| Motor status | Every 5000 ms |
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
// MotorController motor;              // Commented out -- re-enable when motor is wired
ColorSensor color_sensor(5, 6, 4);     // SDA: GPIO5, SCL: GPIO6, LED: GPIO4
// IR sensor removed while color sensor testing (GPIO4 conflict)
```

---

### S3 Board -- `S3/include/ESPNOW_S3.h`

Public API for the S3 ESP-NOW transport layer. Declares the same packet structs as the C3 side (must stay in sync).

Packet structs defined here: `IRControlPacket`, `IRStatusPacket`, `ColorControlPacket`, `ColorStatusPacket`.

**To use**:
```cpp
#include "ESPNOW_S3.h"

void espnow_init_sender(const uint8_t *target_mac);  // Init and add C3 as peer
void espnow_send_ir_control(uint8_t mode);            // Send IRControlPacket (0=digital, 1=analog)
void espnow_send_color_control(uint8_t command);      // Send ColorControlPacket (0-4)
void espnow_process();                                // Call every loop -- prints buffered status
```

---

### S3 Board -- `S3/src/ESPNOW_S3.cpp`

Implementation of the S3 transport layer.

**`espnow_init_sender(mac)`** -- Initialises ESP-NOW, registers send/receive callbacks, adds C3's MAC as a peer.

**`onDataRecv()`** -- Runs in the Wi-Fi task (not `loop()`). **Never call `Serial.print()` directly here** -- it will be interrupted and produce truncated output. Instead, formats the output string into fixed buffers with `snprintf()` and sets volatile flags.

**Receive buffers**:
| Buffer | Flag | Source Packet |
|--------|------|---------------|
| `g_ir_line[32]` | `g_ir_new` | `IRStatusPacket` |
| `g_color_line[48]` | `g_color_new` | `ColorStatusPacket` |

**`espnow_process()`** -- Called from `loop()`. Checks each flag; if set, clears it and calls `Serial.println()` on the corresponding buffer.

**`espnow_send_ir_control(mode)`** -- Builds and sends `IRControlPacket {4, mode}`.

**`espnow_send_color_control(command)`** -- Builds and sends `ColorControlPacket {7, command}`.

**Serial output formats**:
| Packet | Mode | Output |
|--------|------|--------|
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
| `digital` | `IRControlPacket {4, 0}` | Switch C3 IR to digital mode |
| `analog` | `IRControlPacket {4, 1}` | Switch C3 IR to analog mode |
| `c` | `ColorControlPacket {7, 0}` | Switch color to RGB-only display |
| `l` | `ColorControlPacket {7, 1}` | Switch color to lux-only display |
| `cl` | `ColorControlPacket {7, 2}` | Switch color to RGB+Lux display |
| `ledon` | `ColorControlPacket {7, 3}` | Turn color sensor LED on |
| `ledoff` | `ColorControlPacket {7, 4}` | Turn color sensor LED off |

**Loop structure**:
```
loop()
  +-- espnow_process()     -> print any buffered IR/Color status from C3
  +-- Serial.available()   -> read command, send control packet
```

---

## Message Flow

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

### Periodic Status (every 500 ms)
```
C3: espnow_update() timer fires for each registered subsystem
C3: sendIRStatus() / sendColorStatus() -- reads current state from subsystem
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
| Motor status interval | 5000 ms + on change |
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

## Known Limitations

1. **Simultaneous DO+AO reading causes interference** -- solved by mode-exclusive pin reading (see design decision above).
2. **GPIO10 is unusable on ESP32-C3-MINI-1** -- tied to SPI flash; any `digitalRead()` on it will always return the same state regardless of external signal.
3. **GPIO4 pin conflict** -- used by both IR sensor (digital output) and color sensor (LED control). Only one can be active at a time; IR is currently removed from `main.cpp`.
4. **Single peer** -- S3 is hardcoded to one C3 MAC and vice versa.
5. **No packet loss detection** -- packets are fire-and-forget with no sequence numbers or ACK beyond the ESP-NOW layer's `onDataSent` callback.
6. **Color sensor always reads all channels** -- mode only affects what the S3 displays, not what the C3 samples. All R/G/B/Lux values are always present in the status packet.

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

