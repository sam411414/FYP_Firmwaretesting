#include <Arduino.h>
#include <WiFi.h>
#include "ESPNOW_S3.h"

namespace {
constexpr uint8_t kC3MacAddr[6] = {0xA0, 0x76, 0x4E, 0x7B, 0x9A, 0xB4};
} // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("S3 MAC Address: ");
  Serial.println(WiFi.macAddress());

  espnow_init_sender(kC3MacAddr);

  Serial.println("=== IR Sensor Commands ===");
  Serial.println("digital, analog");
  Serial.println("=== Color Sensor Commands ===");
  Serial.println("c, l, cl, ledon, ledoff");
}

void loop() {
  espnow_process();  // Print any buffered status from C3

  if (!Serial.available()) {
    delay(10);
    return;
  }

  String line = Serial.readStringUntil('\n');
  line.trim();
  line.toLowerCase();
  if (line.isEmpty()) {
    return;
  }

  if (line == "digital") {
    espnow_send_ir_control(0);
    return;
  }

  if (line == "analog") {
    espnow_send_ir_control(1);
    return;
  }

  if (line == "c") {
    espnow_send_color_control(0);
    return;
  }

  if (line == "l") {
    espnow_send_color_control(1);
    return;
  }

  if (line == "cl") {
    espnow_send_color_control(2);
    return;
  }

  if (line == "ledon") {
    espnow_send_color_control(3);
    return;
  }

  if (line == "ledoff") {
    espnow_send_color_control(4);
    return;
  }

  Serial.println("Unknown command. Use: digital, analog, c, l, cl, ledon, ledoff");
}