#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "motorControl.h"

namespace {
constexpr size_t kMaxTextLen = 240; // Keep under ESP-NOW 250-byte payload limit

enum PacketType : uint8_t {
  kPacketText = 1,
  kPacketControl = 2
};

struct TextPacket {
  uint8_t type;
  char text[kMaxTextLen];
};

struct ControlPacket {
  uint8_t type;
  uint8_t duty_cycle; // 40-100
  uint8_t direction;  // 0=reverse, 1=forward
  uint8_t enable;     // 0=stop, 1=run
};

MotorController motor;

void printMac(const uint8_t *mac) {
  char buffer[18];
  snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print(buffer);
}

void handleControl(const ControlPacket &cmd) {
  motor.setDirection(cmd.direction == 1);
  motor.setEnabled(cmd.enable == 1);
  if (cmd.enable == 1) {
    motor.setDutyCycle(cmd.duty_cycle);
  }

  Serial.print("CTRL duty=");
  Serial.print(cmd.duty_cycle);
  Serial.print(" dir=");
  Serial.print(cmd.direction ? "FWD" : "REV");
  Serial.print(" enable=");
  Serial.println(cmd.enable ? "ON" : "OFF");
}

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len <= 0) {
    return;
  }

  const uint8_t type = data[0];

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

  if (type == kPacketControl && len >= static_cast<int>(sizeof(ControlPacket))) {
    ControlPacket cmd = {};
    memcpy(&cmd, data, sizeof(cmd));
    handleControl(cmd);
  }
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("C3 MAC Address: ");
  Serial.println(WiFi.macAddress());

  motor.initialize();
  motor.setEnabled(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  Serial.println("C3 ready for ESP-NOW text/control.");
}

void loop() {
  delay(10);
}
