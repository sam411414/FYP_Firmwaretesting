#pragma once

#include <Arduino.h>

enum PacketType : uint8_t {
  kPacketIRControl = 4,  // IR control  (S3→C3)
  kPacketIRStatus  = 5   // IR status   (C3→S3)
};

// IR control packet sent to C3
struct IRControlPacket {
  uint8_t type;   // Must be kPacketIRControl (4)
  uint8_t mode;   // 0=digital, 1=analog
};

// IR status packet received from C3
struct IRStatusPacket {
  uint8_t type;            // Must be kPacketIRStatus (5)
  uint8_t mode;            // 0=digital, 1=analog
  uint8_t digital_state;   // 0=clear, 1=obstacle
  uint8_t analog_high;     // analog value >> 8
  uint8_t analog_low;      // analog value & 0xFF
};

// Initialize ESP-NOW and add the C3 target as a peer.
void espnow_init_sender(const uint8_t *target_mac);

// Send an IR control packet (mode: 0=digital, 1=analog)
void espnow_send_ir_control(uint8_t mode);

// Call from loop() to print any buffered status
void espnow_process();