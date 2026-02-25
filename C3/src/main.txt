// ═══════════════════════════════════════════════════════════════════
// C3 main.cpp — MAC Address Extractor with ESP-NOW Broadcast
// Outputs the C3 board's MAC address continuously via serial and ESP-NOW
// ═══════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

namespace {
  constexpr uint8_t kPacketMacAddr = 8;
  struct MacAddrPacket {
    uint8_t type;       // kPacketMacAddr (8)
    uint8_t mac[6];     // C3's MAC address
  };
  uint8_t g_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
}

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {}

void setup() {
  Serial.begin(115200);
  delay(1000);  // Wait for serial to stabilize
  
  Serial.println("\n\n=== C3 MAC Address Extractor (Broadcasting) ===\n");
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  
  esp_now_register_send_cb(onDataSent);
  
  // Add broadcast as peer
  esp_now_peer_info_t peer_info = {};
  memcpy(peer_info.peer_addr, g_broadcast_mac, 6);
  peer_info.channel = 0;
  peer_info.encrypt = false;
  if (esp_now_add_peer(&peer_info) != ESP_OK) {
    Serial.println("Failed to add broadcast peer");
  }
}

void loop() {
  delay(2000);
  
  // Get MAC address
  uint8_t mac[6];
  WiFi.macAddress(mac);
  
  // Print to serial
  char mac_str[24];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  
  Serial.print("[C3 MAC] ");
  Serial.println(mac_str);
  
  // Send via ESP-NOW to S3 (broadcast)
  MacAddrPacket pkt = {};
  pkt.type = kPacketMacAddr;
  memcpy(pkt.mac, mac, 6);
  esp_now_send(g_broadcast_mac, reinterpret_cast<const uint8_t *>(&pkt),
               sizeof(pkt));
}