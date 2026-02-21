# ESP-NOW Communication Protocol Documentation

## Overview

This document describes the ESP-NOW packet communication protocol between the S3 (Command Interface) and C3 (Controller) ESP32 development boards, and how to use every source file in the project.

---

## System Architecture

```
┌─────────────────────┐    ESP-NOW     ┌──────────────────────┐
│   S3 Board          │◄──────────────►│  C3 Board            │
│ (Command Interface) │                │  (Controller)        │
│                     │  Commands      │                      │
│ MAC: 30:ED:A0:      │◄───────────────│  MAC: A0:76:4E:      │
│      27:8F:A4       │                │       7B:9A:B4       │
│                     │  Status        │                      │
│                     │───────────────►│                      │
└─────────────────────┘                └──────────────────────┘
```

- **S3 → C3**: Sends commands (IR mode switch)
- **C3 → S3**: Sends status (IR sensor readings)
- **Bidirectional**: Full duplex

---

## Hardware Pin Reference (C3 Board)

| Pin | Function | Notes |
|-----|----------|-------|
| GPIO4 | IR Sensor — Digital Output (DO) | `INPUT`, LOW = obstacle |
| GPIO3 | IR Sensor — Analog Output (AO) | `analogRead()`, 0–4095 |
| GPIO8 | NeoPixel LED | Reserved |
| GPIO9 | BOOT button | Reserved |
| GPIO10+ | SPI Flash | **Do NOT use for GPIO** |
| GPIO2, GPIO5, GPIO6, GPIO7 | Available GPIOs | Safe for future use |

> **Important**: GPIO10 on the ESP32-C3-MINI-1 module is tied to SPI flash and cannot be used as general GPIO.

---

## Key Design Decision — Mode-Exclusive Pin Reading

The IR sensor cannot reliably drive both its **DO (digital)** and **AO (analog)** outputs simultaneously when both are being read. Continuously running `analogRead()` while also calling `digitalRead()` causes the ADC to load the sensor's internal circuitry and interferes with the LM393 comparator driving the DO line.

**Solution**: Only the active mode's pin is read at any time.

- In **digital mode**: only `digitalRead(GPIO4)` is called. `analogRead(GPIO3)` is never called.
- In **analog mode**: only `analogRead(GPIO3)` is called. `digitalRead(GPIO4)` is not called.
- On mode switch, `activateDigitalMode()` / `activateAnalogMode()` reconfigure `pinMode` to put the unused pin into high-Z (`INPUT`) to stop loading the sensor output.

---

## Packet Types

```cpp
enum PacketType : uint8_t {
  kPacketText      = 1,  // Text message (unused in current build)
  kPacketControl   = 2,  // Motor control (S3→C3) — reserved
  kPacketStatus    = 3,  // Motor status  (C3→S3) — reserved
  kPacketIRControl = 4,  // IR mode switch (S3→C3)
  kPacketIRStatus  = 5   // IR sensor status (C3→S3)
};
```

### IRControlPacket — Switch IR mode (S3 → C3)

```cpp
struct IRControlPacket {
  uint8_t type;   // Always kPacketIRControl (4)
  uint8_t mode;   // 0 = digital, 1 = analog
};
```

**Size**: 2 bytes

### IRStatusPacket — IR sensor reading (C3 → S3)

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
**Analog**: reconstruct as `(analog_high << 8) | analog_low` (0–4095, 12-bit ADC)  
**Sent**: immediately on state change + every 500 ms (periodic heartbeat)

---

## File Reference

### C3 Board — `C3/include/IRSensor.h`

Declares the `IRSensor` class, `IRMode` enum, and both packet structs (`IRControlPacket`, `IRStatusPacket`).

**To use**:
1. Include from anywhere that needs IR types: `#include "IRSensor.h"`
2. Construct with digital and analog pin numbers: `IRSensor sensor(digital_pin, analog_pin);`
3. Call `sensor.init()` once in `setup()`.
4. Call `sensor.update()` every loop iteration to refresh readings and set the state-changed flag.
5. Use `sensor.checkAndClearStateChanged()` to consume the flag (returns `true` once per change).

**Key API**:
```cpp
IRSensor sensor(4, 3);        // GPIO4 = DO, GPIO3 = AO
sensor.init();                 // Configure pins for current mode (default: digital)
sensor.update();               // Read active pin, debounce, set state_changed_ flag
sensor.handleControl(cmd);     // Apply an IRControlPacket (mode switch)
sensor.isObstacle();           // true if DO was LOW after debounce
sensor.getAnalogValue();       // Last analogRead() result (0–4095)
sensor.getMode();              // kIRDigital or kIRAnalog
sensor.checkAndClearStateChanged(); // Consume the state-changed flag
```

---

### C3 Board — `C3/src/IRSensor.cpp`

Implementation of `IRSensor`. Contains the mode-exclusive reading logic.

**`init()`** — Calls `activateDigitalMode()` (default). Sets GPIO4 as `INPUT`, GPIO3 as `INPUT` (high-Z).

**`update()`** — Branches on current mode:
- *Digital*: `digitalRead(GPIO4)`. Raw value must be stable for 50 ms (debounce) before `obstacle_` updates and `state_changed_` is set.
- *Analog*: `analogRead(GPIO3)`. If the new value differs from the previous by more than 50 counts, `state_changed_` is set.

**`handleControl(cmd)`** — On mode change, calls `activateDigitalMode()` or `activateAnalogMode()` before updating the stored mode and setting `state_changed_`.

**`activateDigitalMode()`** — Sets GPIO4 `INPUT`, GPIO3 `INPUT`. Stops ADC sampling.

**`activateAnalogMode()`** — Sets GPIO4 `INPUT` (high-Z, releases DO line). `analogRead()` handles ADC channel setup automatically.

---

### C3 Board — `C3/include/ESPNOW_C3.h`

Public API for the C3 ESP-NOW transport layer. Uses forward declarations only — does **not** pull in subsystem headers.

**To use**: Include in any C3 file that needs to initialise or register subsystems.

```cpp
#include "ESPNOW_C3.h"

void espnow_init();                         // Initialise ESP-NOW, add S3 as peer
void espnow_register_motor(MotorController *motor); // Register motor (unused if commented out)
void espnow_register_ir(IRSensor *ir);      // Register IR sensor
void espnow_update();                       // Call every loop — dispatches status packets
```

**Packet type enum** (`PacketType`) is defined here and shared by both `.h` files on C3.

---

### C3 Board — `C3/src/ESPNOW_C3.cpp`

Internal implementation of the C3 transport layer.

**Registration pattern** — Each subsystem is registered via a pointer stored in a `namespace`-scoped variable (`g_motor`, `g_ir`). Subsystems that are `nullptr` are silently skipped.

**`espnow_init()`** — Calls `esp_now_init()`, registers send/receive callbacks, and calls `ensureS3Peer()` to add the S3's MAC as a peer immediately.

**`espnow_update()`** — Called every loop. For each registered subsystem:
- Calls `checkAndClearStateChanged()` — if `true`, sends a status packet immediately.
- Checks the periodic timer — if elapsed, sends a status packet regardless of change.

**`sendIRStatus()`** — Builds an `IRStatusPacket` from `g_ir`'s getters and calls `esp_now_send()` to the S3 MAC.

**`onDataRecv()`** — Dispatches incoming packets by `type` byte:
- `kPacketControl` → `g_motor->handleControl()`
- `kPacketIRControl` → `g_ir->handleControl()`

**Periodic intervals**:
| Subsystem | Interval |
|-----------|----------|
| Motor status | Every 5000 ms |
| IR status | Every 500 ms |

---

### C3 Board — `C3/src/main.cpp`

Entry point. Declares subsystem objects and calls init/register/update functions.

**To add a new subsystem**:
1. Include its `.h` file.
2. Declare an instance globally.
3. Call `instance.init()` in `setup()`.
4. Register it: `espnow_register_xxx(&instance)`.
5. Call `instance.update()` in `loop()`.

**Current state**:
```cpp
IRSensor ir_sensor1(4, 3);   // GPIO4 = DO, GPIO3 = AO
// MotorController motor;    // Commented out — re-enable when motor is wired
```

---

### S3 Board — `S3/include/ESPNOW_S3.h`

Public API for the S3 ESP-NOW transport layer. Declares the same `IRControlPacket` and `IRStatusPacket` structs as the C3 side (must stay in sync).

**To use**:
```cpp
#include "ESPNOW_S3.h"

void espnow_init_sender(const uint8_t *target_mac); // Init and add C3 as peer
void espnow_send_ir_control(uint8_t mode);          // Send IRControlPacket (0=digital, 1=analog)
void espnow_process();                              // Call every loop — prints buffered status
```

---

### S3 Board — `S3/src/ESPNOW_S3.cpp`

Implementation of the S3 transport layer.

**`espnow_init_sender(mac)`** — Initialises ESP-NOW, registers send/receive callbacks, adds C3's MAC as a peer.

**`onDataRecv()`** — Runs in the Wi-Fi task (not `loop()`). **Never call `Serial.print()` directly here** — it will be interrupted and produce truncated output. Instead, formats the output string into a fixed buffer with `snprintf()` and sets a `volatile bool g_ir_new` flag.

**`espnow_process()`** — Called from `loop()`. Checks `g_ir_new`; if set, clears it and calls `Serial.println(g_ir_line)`. This guarantees the full string prints without interruption.

**Serial output format**:
| Mode | Output |
|------|--------|
| Digital — obstacle | `[IR] OBSTACLE` |
| Digital — clear | `[IR] Clear` |
| Analog | `[IR] analog: 2048` |

---

### S3 Board — `S3/src/main.cpp`

Entry point for the S3. Handles serial command input and calls `espnow_process()` to print incoming status.

**Available commands** (typed into serial monitor):
| Command | Effect |
|---------|--------|
| `digital` | Sends `IRControlPacket {4, 0}` — switch C3 to digital mode |
| `analog` | Sends `IRControlPacket {4, 1}` — switch C3 to analog mode |

**Loop structure**:
```
loop()
  ├── espnow_process()     → print any buffered IR status from C3
  └── Serial.available()   → read command, send IRControlPacket
```

---

## Message Flow

### Mode Switch (S3 → C3 → S3)
```
S3: User types "analog"
S3: espnow_send_ir_control(1) → IRControlPacket {4, 1}

C3: onDataRecv() → g_ir->handleControl(cmd)
C3: activateAnalogMode() — GPIO4 goes high-Z, ADC enabled on GPIO3
C3: state_changed_ = true
C3: espnow_update() detects flag → sendIRStatus()
    → IRStatusPacket {5, 1, obstacle, analog_high, analog_low}

S3: onDataRecv() → snprintf("[IR] analog: XXXX") → g_ir_new = true
S3: loop() → espnow_process() → Serial.println("[IR] analog: XXXX")
```

### Periodic IR Status (every 500 ms)
```
C3: espnow_update() timer fires
C3: sendIRStatus() — reads current mode/state from g_ir
C3: Sends IRStatusPacket to S3

S3: Receives, formats, prints from loop()
```

---

## Protocol Characteristics

| Property | Value |
|----------|-------|
| Max packet size | 250 bytes (ESP-NOW limit) |
| Typical latency | < 10 ms |
| IR status interval | 500 ms + on change |
| Motor status interval | 5000 ms + on change |
| Encryption | Disabled |
| Retransmission | None (fire-and-forget) |

---

## Known Limitations

1. **Simultaneous DO+AO reading causes interference** — solved by mode-exclusive pin reading (see design decision above).
2. **GPIO10 is unusable on ESP32-C3-MINI-1** — tied to SPI flash; any `digitalRead()` on it will always return the same state regardless of external signal.
3. **Single peer** — S3 is hardcoded to one C3 MAC and vice versa.
4. **No packet loss detection** — packets are fire-and-forget with no sequence numbers or ACK beyond the ESP-NOW layer's `onDataSent` callback.

---

## Adding a New Sensor Subsystem

To add a new sensor (e.g. ultrasonic, temperature) following the same pattern:

**C3 side**:
1. Create `MySensor.h` / `MySensor.cpp` with `init()`, `update()`, `handleControl()`, `checkAndClearStateChanged()`.
2. Add new packet type values to the `PacketType` enum in `ESPNOW_C3.h`.
3. Add `espnow_register_mysensor(MySensor *s)` to `ESPNOW_C3.h`.
4. In `ESPNOW_C3.cpp`: add `g_mysensor` pointer, `sendMySensorStatus()`, dispatch in `onDataRecv()`, periodic timer in `espnow_update()`.
5. In `main.cpp`: declare instance, call `init()`, call `espnow_register_mysensor()`, call `update()` in loop.

**S3 side**:
1. Add matching packet structs to `ESPNOW_S3.h`.
2. Add dispatch branch in `onDataRecv()` in `ESPNOW_S3.cpp` using the same snprintf-buffer pattern.
3. Add command handling in `main.cpp`.

│ (Command Interface)              │ (Control Systems)│
│                 │                │                 │
│ MAC: 30:ED:A0:  │  Commands      │ MAC: A0:76:4E:  │
│      27:8F:A4   │◄───────────────│      7B:9A:B4   │
│                 │                │                 │
│                 │  Status        │                 │
│                 │────────────────►│                 │
└─────────────────┘                └─────────────────┘
```

## Communication Direction

- **S3 → C3**: Sends commands (motor control, IR sensor control)
- **C3 → S3**: Sends status (motor state, IR sensor state)
- **Bidirectional**: Full duplex communication

## Packet Structure

### Packet Types Enum
```c
enum PacketType : uint8_t {
  kPacketText      = 1,  // Text message
  kPacketControl   = 2,  // Motor control (S3→C3)
  kPacketStatus    = 3,  // Motor status (C3→S3)
  kPacketIRControl = 4,  // IR sensor control (S3→C3)
  kPacketIRStatus  = 5   // IR sensor status (C3→S3)
};
```

### 1. TextPacket Structure

**Purpose**: Send text messages for debugging/communication

```c
struct TextPacket {
  uint8_t type;           // Always kPacketText (1)
  char text[240];         // Text payload (null-terminated)
};
```

**Size**: 241 bytes total
**Usage**: Debugging, status messages, user commands as text

### 2. ControlPacket Structure (Motor)

**Purpose**: Motor control commands

```c
struct ControlPacket {
  uint8_t type;           // Always kPacketControl (2)
  uint8_t duty_cycle;     // PWM duty cycle (35-90%)
  uint8_t direction;      // 0=reverse, 1=forward
  uint8_t enable;         // 0=stop, 1=run
};
```

**Size**: 4 bytes total
**Usage**: Motor speed, direction, and enable/disable control

### 3. StatusPacket Structure (Motor)

**Purpose**: Motor status feedback from C3

```c
struct StatusPacket {
  uint8_t type;           // Always kPacketStatus (3)
  uint8_t duty_cycle;     // Current actual duty cycle
  uint8_t direction;      // 0=reverse, 1=forward
  uint8_t enable;         // 0=disabled, 1=enabled
};
```

**Size**: 4 bytes total
**Sent**: On state change (immediate) + every 5 seconds (periodic)

### 4. IRControlPacket Structure

**Purpose**: IR sensor mode control (digital vs analog output)

```c
struct IRControlPacket {
  uint8_t type;           // Always kPacketIRControl (4)
  uint8_t mode;           // 0=digital, 1=analog
};
```

**Size**: 2 bytes total
**Usage**: Switch IR sensor between binary (obstacle/clear) and analog (distance) modes

### 5. IRStatusPacket Structure

**Purpose**: IR sensor status feedback from C3

```c
struct IRStatusPacket {
  uint8_t type;           // Always kPacketIRStatus (5)
  uint8_t mode;           // 0=digital, 1=analog (current mode)
  uint8_t digital_state;  // 0=clear, 1=obstacle
  uint8_t analog_high;    // analog value >> 8 (high byte)
  uint8_t analog_low;     // analog value & 0xFF (low byte)
};
```

**Size**: 5 bytes total
**Sent**: On state change (immediate) + every 500ms (periodic)
**Analog Value**: Reconstructed as `(analog_high << 8) | analog_low` (0-4095 on ESP32-C3 12-bit ADC)

## S3 Board (Transmitter) Behavior

### MAC Address Configuration
- **Target MAC**: `A0:76:4E:7B:9A:B4` (Hardcoded C3 MAC address)
- **Own MAC**: `30:ED:A0:27:8F:A4` (Dynamic, shown in serial output)

### Transmission Method
```c
esp_now_send(kC3MacAddr, reinterpret_cast<uint8_t*>(&packet), sizeof(packet));
```

### Command Processing
The S3 processes serial commands and converts them to packets:

| Serial Command | Packet Type | Details |
|---|---|---|
| `"forward"` | ControlPacket | `{2, current_duty, 1, 1}` |
| `"reverse"` | ControlPacket | `{2, current_duty, 0, 1}` |
| `"stop"` | ControlPacket | `{2, current_duty, x, 0}` |
| `"0"-"100"` | ControlPacket | `{2, value, current_dir, 1}` |
| `"digital"` | IRControlPacket | `{4, 0}` — Switch to digital (binary) mode |
| `"analog"` | IRControlPacket | `{4, 1}` — Switch to analog (distance) mode |
| Text messages | TextPacket | `{1, "message"}` |

### Default Control State
```c
ControlPacket g_control{ kPacketControl, 50, 1, 1 };
// Type: 2, Duty: 50%, Direction: Forward, Enabled: True
```

## C3 Board (Receiver/Status Reporter) Behavior

### MAC Address
- **Own MAC**: `A0:76:4E:7B:9A:B4` (Must match S3's target address)

### Packet Reception Handler
```c
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len)
```

### Processing Logic

#### TextPacket Processing
1. Check if `type == kPacketText (1)`
2. Copy data safely with bounds checking
3. Null-terminate text for safety
4. Print to serial: `"RX from [MAC] | [text]"`

#### MotorControlPacket Processing  
1. Check if `type == kPacketControl (2)` and `len >= sizeof(motorControlPacket)`
2. Copy packet data
3. Apply motor control via `motor->handleControl(cmd)`
4. Motor updates ramped over time, status sent on state change

#### IRControlPacket Processing
1. Check if `type == kPacketIRControl (4)` and `len >= sizeof(IRControlPacket)`
2. Copy packet data
3. Apply mode change via `ir->handleControl(cmd)`
4. IR state sent immediately on mode change

### Status Broadcasting (C3 → S3)

C3 sends status packets to S3 under two conditions:

**Motor Status (every 5 seconds + on state change)**:
- After `motor.update()` detects a state change
```c
espnow_send_status(motor);  // Sends StatusPacket
```

**IR Status (every 500ms + on significant change)**:
- After `ir.update()` detects obstacle state change or analog value shift > ±50/4095
```c
espnow_send_ir_status(ir);  // Sends IRStatusPacket
```

## Message Flow Examples

### Motor Control Message Flow
```
S3: User types "forward"
S3: Creates ControlPacket {2, 50, 1, 1}
S3: esp_now_send() → status: OK

C3: onDataRecv() receives packet
C3: Identifies kPacketControl
C3: motor.handleControl(cmd) initiates ramp-up
C3: In loop(): motor.update() applies ramping
C3: Motor state changes → state_changed_ flag set
C3: espnow_update() detects flag, sends StatusPacket {3, actual_duty, 1, 1}

S3: onDataRecv() receives StatusPacket
S3: Parses and displays: "[MOTOR] duty=X dir=FWD enable=ON"
```

### IR Mode Switch Message Flow
```
S3: User types "analog"
S3: Creates IRControlPacket {4, 1}
S3: esp_now_send() → status: OK

C3: onDataRecv() receives packet
C3: Identifies kPacketIRControl
C3: ir.handleControl(cmd) sets mode to kIRAnalog
C3: state_changed_ flag set
C3: espnow_update() detects flag, sends IRStatusPacket
    {5, 1, digital_state, analog_high, analog_low}

S3: onDataRecv() receives IRStatusPacket
S3: Reconstructs analog_value = (high<<8)|low
S3: Displays: "[IR] mode=ANALOG digital=X analog=YYYY"
```

### IR Periodic Status Flow
```
C3: Loop runs every 10ms
C3: ir_sensor1.update() reads both GPIO10 (digital) and GPIO2 (analog)
C3: Detects analog change > ±50 → state_changed_ flag set
C3: espnow_update() runs, timer >= 500ms → sends IRStatusPacket

S3: Receives packet, displays latest IR state
```

### Text Message Flow
```
S3: User types "hello world" (not a recognized command)
S3: Creates TextPacket {1, "hello world"}
S3: esp_now_send() → status: OK

C3: onDataRecv() receives packet
C3: Identifies kPacketText
C3: Prints "RX from 30:ED:A0:27:8F:A4 | hello world"
```

## Protocol Characteristics

### Reliability
- **Status Confirmation**: S3 receives `ESP_NOW_SEND_SUCCESS` or `ESP_NOW_SEND_FAIL`
- **No ACK**: Current implementation is fire-and-forget
- **Packet Loss**: No retransmission mechanism

### Performance  
- **Range**: Up to 50-200 meters (depending on obstacles)
- **Latency**: < 10ms typical
- **Bandwidth**: 250 bytes max per packet, ~1MB/s theoretical

### Security
- **Encryption**: Currently disabled (`peer_info.encrypt = false`)
- **Authentication**: MAC address filtering only

## Limitations & Future Improvements

### Current Limitations
1. **Single Target**: S3 can only communicate with one hardcoded C3
2. **No Text Feedback from C3**: C3 doesn't reply with text packets
3. **No Error Counters**: Lost packets not tracked
4. **Analog Granularity**: 16-bit analog transmitted in 2 bytes—adequate for 12-bit ADC

### Potential Enhancements
1. **Dynamic MAC Discovery**: Automatic peer discovery via broadcast beacon
2. **Packet Sequencing**: Add sequence numbers for ordering guarantees
3. **Multiple Sensors**: Support more than one IR sensor, combine status packets
4. **Encryption**: Enable ESP-NOW encryption for security
5. **Subsystem Abstraction**: Generic packet handler for pluggable components
6. **Configuration Persistence**: Store calibration offsets on C3 EEPROM

## Debugging Information

### S3 Serial Output Examples
```
S3 MAC Address: 30:ED:A0:27:8F:A4

=== Motor Commands ===
forward, reverse, stop, 0-100 (duty %)

=== IR Sensor Commands ===
digital, analog

[MOTOR] duty=50 dir=FWD enable=ON
[MOTOR] duty=75 dir=FWD enable=ON
[IR] mode=DIGITAL digital=CLEAR analog=412
[IR] mode=DIGITAL digital=OBSTACLE analog=3890
[IR] mode=ANALOG digital=CLEAR analog=412
```

### C3 Serial Output Examples (Debugging)
```
C3 ready for motor control via ESP-NOW.
C3 ESP-NOW ready (motor + IR).
```

**C3 Only Prints on RX (if text packets are sent):**
```
RX from 30:ED:A0:27:8F:A4 | debug message
```