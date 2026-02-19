#include <Arduino.h>
#include <WiFi.h>
#include "motorControl.h"
#include "ESPNOW.h"

namespace {
constexpr uint8_t kC3MacAddr[6] = {0xA0, 0x76, 0x4E, 0x7B, 0x9A, 0xB4};

ControlPacket g_control{ kPacketControl, 50, 1, 1 };

bool isNumber(const String &text) {
  if (text.isEmpty()) {
    return false;
  }
  for (size_t i = 0; i < text.length(); ++i) {
    if (!isDigit(text[i])) {
      return false;
    }
  }
  return true;
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("S3 MAC Address: ");
  Serial.println(WiFi.macAddress());

  espnow_init_sender(kC3MacAddr);

  Serial.println("Type a command or message, then press Enter.");
  Serial.println("Commands: forward, reverse, stop, 0-100 (speed, <10=stop)");
}

void loop() {
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

  if (line == "forward") {
    g_control.direction = 1;
    g_control.enable = 1;
    espnow_send_control(g_control);
    return;
  }

  if (line == "reverse") {
    g_control.direction = 0;
    g_control.enable = 1;
    espnow_send_control(g_control);
    return;
  }

  if (line == "stop") {
    g_control.enable = 0;
    espnow_send_control(g_control);
    return;
  }

  if (isNumber(line)) {
    const int value = line.toInt();
    if (value >= 0 && value <= 100) {
      g_control.duty_cycle = static_cast<uint8_t>(value);
      g_control.enable = 1;
      espnow_send_control(g_control);
    } else {
      Serial.println("Speed must be 0-100 (0-9=stop, 10-100 maps to 30-90 duty).");
    }
    return;
  }

  espnow_send_text(line);
}