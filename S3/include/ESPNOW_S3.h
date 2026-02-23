#pragma once

#include <Arduino.h>

enum PacketType : uint8_t {
  kPacketControl      = 2,  // Motor control  (S3→C3)
  kPacketStatus       = 3,  // Motor status   (C3→S3)
  kPacketIRControl    = 4,  // IR control     (S3→C3)
  kPacketIRStatus     = 5,  // IR status      (C3→S3)
  kPacketColorStatus  = 6,  // Color status   (C3→S3)
  kPacketColorControl = 7,  // Color control  (S3→C3)
  kPacketMacAddr      = 8   // MAC address    (C3→S3)
};

// Motor control packet sent to C3
struct motorControlPacket {
  uint8_t type;        // Must be kPacketControl (2)
  uint8_t duty_cycle;  // 0-100 (mapped on C3 side)
  uint8_t direction;   // 0=reverse, 1=forward
  uint8_t enable;      // 0=stop, 1=run
};

// Motor status packet received from C3
struct MotorStatusPacket {
  uint8_t type;        // Must be kPacketStatus (3)
  uint8_t duty_cycle;  // Current mapped duty cycle %
  uint8_t direction;   // 0=reverse, 1=forward
  uint8_t enable;      // 0=stopped, 1=running
  uint8_t rpm_high;    // RPM >> 8
  uint8_t rpm_low;     // RPM & 0xFF
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

// Color control packet sent to C3
struct ColorControlPacket {
  uint8_t type;      // Must be kPacketColorControl (7)
  uint8_t command;   // 0=RGB, 1=Lux, 2=RGB+Lux, 3=LED on, 4=LED off
};

// Color status packet received from C3
struct ColorStatusPacket {
  uint8_t type;       // Must be kPacketColorStatus (6)
  uint8_t mode;       // 0=RGB, 1=Lux, 2=RGB+Lux
  uint8_t r;          // IR-compensated red, normalized 0-255
  uint8_t g;          // IR-compensated green, normalized 0-255
  uint8_t b;          // IR-compensated blue, normalized 0-255
  uint8_t lux_high;   // Lux >> 8
  uint8_t lux_low;    // Lux & 0xFF
};

// MAC address announcement packet received from C3
struct MacAddrPacket {
  uint8_t type;       // Must be kPacketMacAddr (8)
  uint8_t mac[6];     // C3's MAC address in hex
};

// Initialize ESP-NOW and add the C3 target as a peer.
void espnow_init_sender(const uint8_t *target_mac);

// Send a motor control packet
void espnow_send_motor_control(uint8_t duty, uint8_t direction, uint8_t enable);

// Set expected motor state for verification (auto-resends on mismatch)
void espnow_set_motor_expected(uint8_t duty, uint8_t direction, uint8_t enable);

// Send an IR control packet (mode: 0=digital, 1=analog)
void espnow_send_ir_control(uint8_t mode);

// Send a color control packet (command: 0=RGB, 1=Lux, 2=RGB+Lux, 3=LED on, 4=LED off)
void espnow_send_color_control(uint8_t command);

// Call from loop() to print any buffered status
void espnow_process();

// Get the C3's MAC address if received (returns true if MAC is valid)
bool espnow_get_c3_mac(uint8_t *out_mac);