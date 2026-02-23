// ═══════════════════════════════════════════════════════════════════
// S3 Hub — Multi-Peripheral Command Interface
//
// Serial command format:  <slot>:<command>
//   Slot = 1-5 (see registry in ESPNOW_S3.h)
//
// Motor commands (slots 3, 5):
//   F        Forward
//   R        Reverse
//   S        Stop (duty=0, enable=0)
//   0-100    Set duty cycle
//
// Color commands (slot 1):
//   C        RGB-only display mode
//   L        Lux-only display mode
//   CL / B   RGB+Lux display mode
//   O        LED on
//   X        LED off
//
// IR commands (slots 2, 4):
//   D        Digital mode
//   A        Analog mode
// ═══════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <WiFi.h>
#include "ESPNOW_S3.h"

// ── Per-motor local state (for partial commands like F/R/S) ────────
// Indexed by slot number (1-indexed), so [0] is unused, [slot] is used.
struct MotorLocal {
  uint8_t duty;
  uint8_t dir;
  uint8_t enable;
};
MotorLocal g_motor_state[kNumDevices + 1] = {};  // [slot] = state

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("\n[Hub] S3 MAC: ");
  Serial.println(WiFi.macAddress());

  espnow_hub_init();
  espnow_print_registry();

  // Initialise local motor state for all motor slots
  for (uint8_t s = 1; s <= kNumDevices; s++) {
    if (espnow_get_device_type(s) == kPeripheralMotor) {
      g_motor_state[s] = {0, 1, 0};  // duty=0, dir=FWD, enable=off
    }
  }

  Serial.println("[Hub] Ready. Format: <slot>:<command>");
}

// ── Command handler ───────────────────────────────────────────────

void handleCommand(const String &input) {
  // Find the colon separator
  int colon = input.indexOf(':');
  if (colon < 1) {
    Serial.println("{\"event\":\"error\",\"msg\":\"Missing slot:cmd format\"}");
    return;
  }

  uint8_t slot = input.substring(0, colon).toInt();
  if (slot < 1 || slot > kNumDevices) {
    Serial.println("{\"event\":\"error\",\"msg\":\"Invalid slot number\"}");
    return;
  }

  String cmd = input.substring(colon + 1);
  cmd.trim();
  cmd.toUpperCase();

  if (cmd.length() == 0) {
    Serial.println("{\"event\":\"error\",\"msg\":\"Empty command\"}");
    return;
  }

  PeripheralType ptype = espnow_get_device_type(slot);

  // ── Motor commands ──────────────────────────────────────────────
  if (ptype == kPeripheralMotor) {
    MotorLocal &m = g_motor_state[slot];

    if (cmd == "F") {
      m.dir = 1;
      m.enable = 1;
      espnow_send_motor_cmd(slot, m.duty, m.dir, m.enable);
    } else if (cmd == "R") {
      m.dir = 0;
      m.enable = 1;
      espnow_send_motor_cmd(slot, m.duty, m.dir, m.enable);
    } else if (cmd == "S") {
      m.duty = 0;
      m.enable = 0;
      espnow_send_motor_cmd(slot, 0, m.dir, 0);
    } else {
      // Try to parse as duty cycle number
      int duty = cmd.toInt();
      if (duty >= 0 && duty <= 100 && isDigit(cmd.charAt(0))) {
        m.duty = duty;
        m.enable = (duty >= 10) ? 1 : 0;
        if (m.enable == 0) m.duty = 0;
        espnow_send_motor_cmd(slot, m.duty, m.dir, m.enable);
      } else {
        Serial.println("{\"event\":\"error\",\"msg\":\"Unknown motor command\"}");
      }
    }
    return;
  }

  // ── Color commands ──────────────────────────────────────────────
  if (ptype == kPeripheralColor) {
    if (cmd == "C") {
      espnow_send_color_cmd(slot, 0);       // RGB only
    } else if (cmd == "L") {
      espnow_send_color_cmd(slot, 1);       // Lux only
    } else if (cmd == "CL" || cmd == "B") {
      espnow_send_color_cmd(slot, 2);       // RGB + Lux
    } else if (cmd == "O") {
      espnow_send_color_cmd(slot, 3);       // LED on
    } else if (cmd == "X") {
      espnow_send_color_cmd(slot, 4);       // LED off
    } else {
      Serial.println("{\"event\":\"error\",\"msg\":\"Unknown color command\"}");
    }
    return;
  }

  // ── IR commands ─────────────────────────────────────────────────
  if (ptype == kPeripheralIR) {
    if (cmd == "D") {
      espnow_send_ir_cmd(slot, 0);          // Digital mode
    } else if (cmd == "A") {
      espnow_send_ir_cmd(slot, 1);          // Analog mode
    } else {
      Serial.println("{\"event\":\"error\",\"msg\":\"Unknown IR command\"}");
    }
    return;
  }
}

void loop() {
  espnow_hub_process();

  // ── Read serial commands ──────────────────────────────────────
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      handleCommand(line);
    }
  }

  delay(1);
}