#pragma once

#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════
// ESP-NOW Hub (S3) — Multi-Peripheral Transport Layer
// Manages a registry of C3 peripherals, polls them round-robin,
// and accepts unsolicited on-change packets from any registered C3.
// ═══════════════════════════════════════════════════════════════════

// ── Packet types (must match C3 ESPNOW_C3.h) ──────────────────────
enum PacketType : uint8_t {
  kPacketControl      = 2,  // Motor control  (S3→C3)
  kPacketStatus       = 3,  // Motor status   (C3→S3)
  kPacketIRControl    = 4,  // IR control     (S3→C3)
  kPacketIRStatus     = 5,  // IR status      (C3→S3)
  kPacketColorStatus  = 6,  // Color status   (C3→S3)
  kPacketColorControl = 7,  // Color control  (S3→C3)
  kPacketMacAddr      = 8,  // MAC announce   (reserved)
  kPacketPoll         = 9   // Poll request   (S3→C3)
};

// ── Peripheral type ────────────────────────────────────────────────
enum PeripheralType : uint8_t {
  kPeripheralMotor = 0,
  kPeripheralIR    = 1,
  kPeripheralColor = 2
};

// ── Packet structs (must stay in sync with C3) ────────────────────

struct motorControlPacket {
  uint8_t type;        // kPacketControl (2)
  uint8_t duty_cycle;  // 0-100
  uint8_t direction;   // 0=reverse, 1=forward
  uint8_t enable;      // 0=stop, 1=run
};

struct MotorStatusPacket {
  uint8_t type;        // kPacketStatus (3)
  uint8_t duty_cycle;
  uint8_t direction;
  uint8_t enable;
  uint8_t rpm_high;
  uint8_t rpm_low;
};

struct IRControlPacket {
  uint8_t type;   // kPacketIRControl (4)
  uint8_t mode;   // 0=digital, 1=analog
};

struct IRStatusPacket {
  uint8_t type;
  uint8_t mode;
  uint8_t digital_state;
  uint8_t analog_high;
  uint8_t analog_low;
};

struct ColorControlPacket {
  uint8_t type;      // kPacketColorControl (7)
  uint8_t command;   // 0=RGB, 1=Lux, 2=RGB+Lux, 3=LED on, 4=LED off
};

struct ColorStatusPacket {
  uint8_t type;
  uint8_t mode;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t lux_high;
  uint8_t lux_low;
};

struct PollPacket {
  uint8_t type;   // kPacketPoll (9)
};

// ── Device configuration (compile-time registry entry) ─────────────
struct DeviceConfig {
  uint8_t mac[6];
  PeripheralType type;
};

// ═══════════════════════════════════════════════════════════════════
// Peripheral Registry — edit this table to add/remove/reorder C3s.
// Slot numbers shown to user are 1-indexed (slot 1 = index 0).
// ═══════════════════════════════════════════════════════════════════
constexpr uint8_t kNumDevices = 5;

constexpr DeviceConfig kDeviceList[kNumDevices] = {
  /* Slot 1 */ {{0xA0, 0x76, 0x4E, 0x7B, 0x3C, 0x38}, kPeripheralColor},  // Color-1
  /* Slot 2 */ {{0xA0, 0x76, 0x4E, 0x4A, 0x09, 0x08}, kPeripheralIR},     // IR-1
  /* Slot 3 */ {{0xA0, 0x76, 0x4E, 0x7B, 0x9A, 0xB4}, kPeripheralMotor},  // Motor-1
  /* Slot 4 */ {{0x34, 0xB4, 0x72, 0x48, 0xF1, 0xA8}, kPeripheralIR},     // IR-2
  /* Slot 5 */ {{0xA0, 0x76, 0x4E, 0x4D, 0xD2, 0xE0}, kPeripheralMotor},  // Motor-2
};

// ── Timing constants ───────────────────────────────────────────────
constexpr unsigned long kPollTimeoutMs     = 30;   // Max wait for poll response
constexpr unsigned long kPollInterDeviceMs = 2;    // Gap between successive polls
constexpr unsigned long kMotorResendMs     = 400;  // Motor command resend interval
constexpr int           kMotorMaxResends   = 5;    // Max motor resend attempts

// ── Public API ─────────────────────────────────────────────────────

/// Initialise ESP-NOW, add all peripherals as peers, print registry.
void espnow_hub_init();

/// Call every loop() — runs poll FSM, prints JSON for new data.
void espnow_hub_process();

/// Send motor control to a specific slot (1-indexed).
void espnow_send_motor_cmd(uint8_t slot, uint8_t duty, uint8_t dir, uint8_t enable);

/// Send IR control to a specific slot (1-indexed).
void espnow_send_ir_cmd(uint8_t slot, uint8_t mode);

/// Send color control to a specific slot (1-indexed).
void espnow_send_color_cmd(uint8_t slot, uint8_t command);

/// Get the peripheral type for a slot (1-indexed). Returns the PeripheralType enum.
PeripheralType espnow_get_device_type(uint8_t slot);

/// Check if a device is currently online (responded to last poll).
bool espnow_is_device_online(uint8_t slot);

/// Print the peripheral registry to Serial (human-readable, called once at startup).
void espnow_print_registry();