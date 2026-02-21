#include "ESPNOW_S3.h"

#include <WiFi.h>
#include <esp_now.h>

namespace {

uint8_t g_target_mac[6] = {0};

// Buffered IR status for safe printing from loop()
volatile bool g_ir_new = false;
char g_ir_line[32] = {0};  // Pre-formatted output line

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {}

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len <= 0) return;

  const uint8_t type = data[0];

  if (type == kPacketIRStatus && len >= static_cast<int>(sizeof(IRStatusPacket))) {
    IRStatusPacket ir_status = {};
    memcpy(&ir_status, data, sizeof(ir_status));

    if (ir_status.mode == 0) {
      snprintf(g_ir_line, sizeof(g_ir_line), "[IR] %s",
               ir_status.digital_state ? "OBSTACLE" : "Clear");
    } else {
      int analog_value = (static_cast<int>(ir_status.analog_high) << 8) | ir_status.analog_low;
      snprintf(g_ir_line, sizeof(g_ir_line), "[IR] analog: %d", analog_value);
    }
    g_ir_new = true;
  }
}

bool addPeer(const uint8_t *mac) {
  esp_now_peer_info_t peer_info = {};
  memcpy(peer_info.peer_addr, mac, 6);
  peer_info.channel = 0;
  peer_info.encrypt = false;
  return esp_now_add_peer(&peer_info) == ESP_OK;
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────

void espnow_init_sender(const uint8_t *target_mac) {
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
}

void espnow_send_ir_control(uint8_t mode) {
  IRControlPacket pkt = {};
  pkt.type = kPacketIRControl;
  pkt.mode = mode;
  esp_now_send(g_target_mac, reinterpret_cast<const uint8_t *>(&pkt),
               sizeof(pkt));
}

void espnow_process() {
  if (g_ir_new) {
    g_ir_new = false;
    Serial.println(g_ir_line);
  }
}