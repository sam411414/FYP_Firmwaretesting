#include "ESPNOW.h"

#include <WiFi.h>
#include <esp_now.h>

namespace {

MotorController *g_motor = nullptr;

void printMac(const uint8_t *mac) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print(buffer);
}

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len <= 0) return;

  const uint8_t type = data[0];

  // ── Text packet ──────────────────────────────────────────────────
  if (type == kPacketText) {
    TextPacket incoming = {};
    const size_t copy_len = static_cast<size_t>(len) < sizeof(TextPacket)
                                ? static_cast<size_t>(len)
                                : sizeof(TextPacket);
    memcpy(&incoming, data, copy_len);
    incoming.text[sizeof(incoming.text) - 1] = '\0';

    Serial.print("RX from ");
    printMac(mac);
    Serial.print(" | ");
    Serial.println(incoming.text);
    return;
  }

  // ── Control packet → forward to motor ────────────────────────────
  if (type == kPacketControl &&
      len >= static_cast<int>(sizeof(ControlPacket)) &&
      g_motor != nullptr) {
    ControlPacket cmd = {};
    memcpy(&cmd, data, sizeof(cmd));
    g_motor->handleControl(cmd);
  }
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────

void espnow_init(MotorController *motor) {
  g_motor = motor;

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  Serial.println("C3 ready for ESP-NOW text/control.");
}
