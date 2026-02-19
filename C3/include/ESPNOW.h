#pragma once

#include <Arduino.h>
#include "motorControl.h"

// ── ESP-NOW protocol constants ──────────────────────────────────────
constexpr size_t kMaxTextLen = 240;  // Keep under ESP-NOW 250-byte limit
constexpr unsigned long kStatusReportIntervalMs = 5000;  // Status report every 5 seconds

enum PacketType : uint8_t {
  kPacketText    = 1,
  kPacketControl = 2,
  kPacketStatus  = 3
};

// Text message packet
struct TextPacket {
  uint8_t type;
  char    text[kMaxTextLen];
};

// Status packet sent from C3 to S3
struct StatusPacket {
  uint8_t type;        // Must be kPacketStatus (3)
  uint8_t duty_cycle;  // Current actual duty cycle
  uint8_t direction;   // 0=reverse, 1=forward
  uint8_t enable;      // 0=disabled, 1=enabled
};

// ── Public API ──────────────────────────────────────────────────────

// Initialise ESP-NOW and register the receive callback.
// Pass a pointer to the MotorController so incoming motorControlPackets
// can be dispatched automatically.
void espnow_init(MotorController *motor);

// Send current motor status to S3
void espnow_send_status(const MotorController *motor);

// Update function to handle periodic status reporting
void espnow_update();
