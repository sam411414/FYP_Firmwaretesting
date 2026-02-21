#pragma once

#include <Arduino.h>

// Forward declarations
class MotorController;
class IRSensor;
struct motorControlPacket;
struct IRControlPacket;

enum PacketType : uint8_t {
  kPacketText      = 1,
  kPacketControl   = 2,  // Motor control  (S3→C3)
  kPacketStatus    = 3,  // Motor status   (C3→S3)
  kPacketIRControl = 4,  // IR control     (S3→C3)
  kPacketIRStatus  = 5   // IR status      (C3→S3)
};

// Initialise ESP-NOW transport layer (call once before registering subsystems)
void espnow_init();

// Register subsystems independently
void espnow_register_motor(MotorController *motor);
void espnow_register_ir(IRSensor *ir);

// Call every loop — dispatches periodic status for all registered subsystems
void espnow_update();
