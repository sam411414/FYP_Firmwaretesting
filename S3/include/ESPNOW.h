#pragma once

#include <Arduino.h>
#include "motorControl.h"

// ── ESP-NOW protocol constants ──────────────────────────────────────
constexpr size_t kMaxTextLen = 240;  // Keep under ESP-NOW 250-byte limit

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

// Status packet received from C3
struct StatusPacket {
  uint8_t type;        // Must be kPacketStatus (3)
  uint8_t duty_cycle;  // Current actual duty cycle
  uint8_t direction;   // 0=reverse, 1=forward
  uint8_t enable;      // 0=disabled, 1=enabled
};

// ── Public API for S3 sender ────────────────────────────────────────

// Initialize ESP-NOW and add the C3 target as a peer.
// Pass the C3 MAC address to add as peer.
void espnow_init_sender(const uint8_t *target_mac);

// Send a text message to the currently added peer
void espnow_send_text(const String &message);

// Send a control packet to the currently added peer  
void espnow_send_control(const ControlPacket &control);