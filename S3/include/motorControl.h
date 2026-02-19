#pragma once

#include <Arduino.h>

// Control packet received over ESP-NOW (motor-specific)
struct motorControlPacket {
  uint8_t type;        // Must be kPacketControl (2)
  uint8_t duty_cycle;  // 40-100
  uint8_t direction;   // 0=reverse, 1=forward
  uint8_t enable;      // 0=stop, 1=run
};

// Control packet used for sending
struct ControlPacket {
  uint8_t type;        // Must be kPacketControl (2) 
  uint8_t duty_cycle;  // 40-100
  uint8_t direction;   // 0=reverse, 1=forward
  uint8_t enable;      // 0=stop, 1=run
};