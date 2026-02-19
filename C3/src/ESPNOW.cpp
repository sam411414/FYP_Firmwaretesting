#include "ESPNOW.h"

#include <WiFi.h>
#include <esp_now.h>

namespace {

MotorController *g_motor = nullptr;
constexpr uint8_t kS3MacAddr[6] = {0x30, 0xED, 0xA0, 0x27, 0x8F, 0xA4};
uint8_t g_s3_mac[6] = {0};
bool g_s3_peer_added = false;
unsigned long g_last_status_time = 0;

void printMac(const uint8_t *mac) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print(buffer);
}

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  // Optional: Add logging for send status if needed
  // Serial.print("TX to ");
  // printMac(mac);
  // Serial.print(" | status: ");
  // Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

bool addPeer(const uint8_t *mac) {
  esp_now_peer_info_t peer_info = {};
  memcpy(peer_info.peer_addr, mac, 6);
  peer_info.channel = 0; // Auto-select channel
  peer_info.encrypt = false;
  return esp_now_add_peer(&peer_info) == ESP_OK;
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
      len >= static_cast<int>(sizeof(motorControlPacket)) &&
      g_motor != nullptr) {
    motorControlPacket cmd = {};
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
  esp_now_register_send_cb(onDataSent);
  Serial.println("C3 ready for ESP-NOW text/control.");
}

void espnow_send_status(const MotorController *motor) {
  if (motor == nullptr) return;
  
  if (!g_s3_peer_added) {
    memcpy(g_s3_mac, kS3MacAddr, sizeof(g_s3_mac));
    if (addPeer(g_s3_mac)) {
      g_s3_peer_added = true;
      Serial.print("Added S3 peer: ");
      printMac(g_s3_mac);
      Serial.println();
    } else {
      Serial.println("Failed to add S3 peer");
      return;
    }
  }
  
  StatusPacket status = {};
  status.type = kPacketStatus;
  status.duty_cycle = motor->getCurrentDutyCycle();
  status.direction = motor->getDirection() ? 1 : 0;
  status.enable = motor->isEnabled() ? 1 : 0;
  
  esp_now_send(g_s3_mac, reinterpret_cast<const uint8_t *>(&status), sizeof(status));
}

void espnow_update() {
  unsigned long current_time = millis();
  
  // Check for motor state changes and send immediate update
  if (g_motor != nullptr && g_motor->checkAndClearStateChanged()) {
    espnow_send_status(g_motor);
  }
  
  // Send periodic status updates every 5 seconds
  if (current_time - g_last_status_time >= kStatusReportIntervalMs) {
    g_last_status_time = current_time;
    espnow_send_status(g_motor);
  }
}
