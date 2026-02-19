#include "ESPNOW.h"

#include <WiFi.h>
#include <esp_now.h>

namespace {

uint8_t g_target_mac[6] = {0};

void printMac(const uint8_t *mac) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print(buffer);
}

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.print("TX to ");
  printMac(mac);
  Serial.print(" | status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len <= 0) return;

  const uint8_t type = data[0];

  if (type == kPacketStatus && len >= static_cast<int>(sizeof(StatusPacket))) {
    StatusPacket status = {};
    memcpy(&status, data, sizeof(status));

    Serial.print("STATUS from ");
    printMac(mac);
    Serial.print(" | CTRL duty=");
    Serial.print(status.duty_cycle);
    Serial.print(" dir=");
    Serial.print(status.direction ? "FWD" : "REV");
    Serial.print(" enable=");
    Serial.println(status.enable ? "ON" : "OFF");
    return;
  }
}

bool addPeer(const uint8_t *mac) {
  esp_now_peer_info_t peer_info = {};
  memcpy(peer_info.peer_addr, mac, 6);
  peer_info.channel = 0; // Auto-select channel
  peer_info.encrypt = false;
  return esp_now_add_peer(&peer_info) == ESP_OK;
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────

void espnow_init_sender(const uint8_t *target_mac) {
  // Store the target MAC
  memcpy(g_target_mac, target_mac, 6);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  if (!addPeer(g_target_mac)) {
    Serial.println("Failed to add target peer");
    return;
  }

  Serial.print("ESP-NOW initialized. Target MAC: ");
  printMac(g_target_mac);
  Serial.println();
}

void espnow_send_text(const String &message) {
  TextPacket packet = {};
  packet.type = kPacketText;

  String payload = message;
  if (payload.length() >= kMaxTextLen) {
    payload = payload.substring(0, kMaxTextLen - 1);
    Serial.println("Message truncated to 239 chars.");
  }
  payload.toCharArray(packet.text, sizeof(packet.text));

  esp_now_send(g_target_mac, reinterpret_cast<const uint8_t *>(&packet),
               sizeof(packet));
}

void espnow_send_control(const ControlPacket &control) {
  esp_now_send(g_target_mac, reinterpret_cast<const uint8_t *>(&control),
               sizeof(control));
}