#pragma once

#include <Arduino.h>
#include "motorControl.h"

// ── ESP-NOW protocol constants ──────────────────────────────────────
constexpr size_t kMaxTextLen = 240;  // Keep under ESP-NOW 250-byte limit

enum PacketType : uint8_t {
  kPacketText    = 1,
  kPacketControl = 2
};

// Text message packet
struct TextPacket {
  uint8_t type;
  char    text[kMaxTextLen];
};

// ── Public API ──────────────────────────────────────────────────────

// Initialise ESP-NOW and register the receive callback.
// Pass a pointer to the MotorController so incoming ControlPackets
// can be dispatched automatically.
void espnow_init(MotorController *motor);
