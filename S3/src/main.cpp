#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

namespace {
constexpr uint8_t kC3MacAddr[6] = {0xA0, 0x76, 0x4E, 0x7B, 0x9A, 0xB4};
constexpr size_t kMaxTextLen = 240;

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
  uint8_t duty_cycle; // 0-100 input (maps to 30-90% actual, <10=stop)
  uint8_t direction;  // 0=reverse, 1=forward
  uint8_t enable;     // 0=stop, 1=run
};

ControlPacket g_control{ kPacketControl, 50, 1, 1 };

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

bool addPeer(const uint8_t *mac) {
  esp_now_peer_info_t peer_info = {};
  memcpy(peer_info.peer_addr, mac, 6);
  peer_info.channel = 0; // Auto-select channel
  peer_info.encrypt = false;
  return esp_now_add_peer(&peer_info) == ESP_OK;
}

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

void sendText(const String &line) {
  TextPacket packet = {};
  packet.type = kPacketText;

  String payload = line;
  if (payload.length() >= kMaxTextLen) {
    payload = payload.substring(0, kMaxTextLen - 1);
    Serial.println("Message truncated to 239 chars.");
  }
  payload.toCharArray(packet.text, sizeof(packet.text));

  esp_now_send(kC3MacAddr, reinterpret_cast<const uint8_t *>(&packet),
               sizeof(packet));
}

void sendControl() {
  esp_now_send(kC3MacAddr, reinterpret_cast<const uint8_t *>(&g_control),
               sizeof(g_control));
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("S3 MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("C3 Target MAC: ");
  printMac(kC3MacAddr);
  Serial.println();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(onDataSent);

  if (!addPeer(kC3MacAddr)) {
    Serial.println("Failed to add C3 peer");
    return;
  }

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
    sendControl();
    return;
  }

  if (line == "reverse") {
    g_control.direction = 0;
    g_control.enable = 1;
    sendControl();
    return;
  }

  if (line == "stop") {
    g_control.enable = 0;
    sendControl();
    return;
  }

  if (isNumber(line)) {
    const int value = line.toInt();
    if (value >= 0 && value <= 100) {
      g_control.duty_cycle = static_cast<uint8_t>(value);
      g_control.enable = 1;
      sendControl();
    } else {
      Serial.println("Speed must be 0-100 (0-9=stop, 10-100 maps to 30-90 duty).");
    }
    return;
  }

  sendText(line);
}