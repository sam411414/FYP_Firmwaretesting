#pragma once

#include <Arduino.h>

// Forward declarations
class MotorController;
class IRSensor;
class ColorSensor;
struct motorControlPacket;
struct IRControlPacket;
struct ColorControlPacket;

enum PacketType : uint8_t {
  kPacketText         = 1,
  kPacketControl      = 2,  // Motor control    (S3→C3)
  kPacketStatus       = 3,  // Motor status     (C3→S3)
  kPacketIRControl    = 4,  // IR control       (S3→C3)
  kPacketIRStatus     = 5,  // IR status        (C3→S3)
  kPacketColorStatus  = 6,  // Color status     (C3→S3)
  kPacketColorControl = 7,  // Color control    (S3→C3)
  kPacketMacAddr      = 8,  // MAC announce     (C3→S3)  [reserved]
  kPacketPoll         = 9   // Poll request     (S3→C3)
};

// Initialise ESP-NOW transport layer (call once before registering subsystems)
void espnow_init();

// Register subsystems independently
void espnow_register_motor(MotorController *motor);
void espnow_register_ir(IRSensor *ir);
void espnow_register_color(ColorSensor *cs);

// Call every loop — dispatches periodic status for all registered subsystems
void espnow_update();
