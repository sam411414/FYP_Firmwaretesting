# ESP-NOW Communication Protocol Documentation

## Overview

This document describes the ESP-NOW packet communication protocol between the S3 (Command Interface) and C3 (Motor Controller) ESP32 development boards.

## System Architecture

```
┌─────────────────┐    ESP-NOW     ┌─────────────────┐
│   S3 Board      │────────────────► C3 Board       │
│ (Command Interface)              │ (Motor Controller)│
│                 │                │                 │
│ MAC: 30:ED:A0:  │                │ MAC: A0:76:4E:  │
│      27:8F:A4   │                │      7B:9A:B4   │
└─────────────────┘                └─────────────────┘
```

## Communication Direction

- **S3 → C3**: Sends commands and text messages
- **C3**: Receives and processes packets (current implementation is unidirectional)

## Packet Structure

### Packet Types Enum
```c
enum PacketType : uint8_t {
  kPacketText = 1,     // Text message packet
  kPacketControl = 2   // Motor control packet
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

### 2. ControlPacket Structure

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

| Serial Command | Packet Type | Action |
|---------------|-------------|--------|
| `"forward"`   | ControlPacket | `{2, current_duty, 1, 1}` |
| `"reverse"`   | ControlPacket | `{2, current_duty, 0, 1}` |
| `"stop"`      | ControlPacket | `{2, current_duty, x, 0}` |
| `"40"-"100"`  | ControlPacket | `{2, value, current_dir, 1}` |
| Text messages | TextPacket    | `{1, "message"}` |

### Default Control State
```c
ControlPacket g_control{ kPacketControl, 50, 1, 1 };
// Type: 2, Duty: 50%, Direction: Forward, Enabled: True
```

## C3 Board (Receiver) Behavior

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

#### ControlPacket Processing  
1. Check if `type == kPacketControl (2)` and `len >= sizeof(ControlPacket)`
2. Copy packet data
3. Apply motor control:
   ```c
   motor.setDirection(cmd.direction == 1);    // Set motor direction
   motor.setEnabled(cmd.enable == 1);         // Enable/disable motor
   if (cmd.enable == 1) {
       motor.setDutyCycle(cmd.duty_cycle);    // Set PWM duty cycle
   }
   ```
4. Print control status to serial

## Message Flow Examples

### Control Message Flow
```
S3: User types "forward"
S3: Creates ControlPacket {2, 50, 1, 1}
S3: esp_now_send() → status: OK
C3: onDataRecv() receives 4 bytes
C3: Identifies kPacketControl
C3: motor.setDirection(true), motor.setEnabled(true), motor.setDutyCycle(50)
C3: Prints "CTRL duty=50 dir=FWD enable=ON"
```

### Text Message Flow
```
S3: User types "hello world"  
S3: Creates TextPacket {1, "hello world"}
S3: esp_now_send() → status: OK
C3: onDataRecv() receives 241 bytes
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
1. **Unidirectional**: Only S3→C3 communication
2. **No Status Feedback**: C3 doesn't report motor status back to S3
3. **No Error Handling**: Lost packets are not detected or retransmitted
4. **Single Target**: S3 can only communicate with one hardcoded C3

### Potential Enhancements
1. **Bidirectional Communication**: Add C3→S3 status packets
2. **Dynamic MAC Discovery**: Automatic peer discovery
3. **Packet Sequencing**: Add sequence numbers for reliability
4. **Multiple Motor Control**: Support multiple C3 boards
5. **Encryption**: Enable ESP-NOW encryption for security

## Debugging Information

### S3 Serial Output
```
S3 Motor Command Interface Ready!
Commands: blink, forward, reverse, stop, 40-100 (duty %)
ESP-NOW send: OK
```

### C3 Serial Output
```
C3 MAC Address: A0:76:4E:7B:9A:B4
C3 ready to receive ESP-NOW messages.
RX from 30:ED:A0:27:8F:A4 | hello
CTRL duty=75 dir=FWD enable=ON
```