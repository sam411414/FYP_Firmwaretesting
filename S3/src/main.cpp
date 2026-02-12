#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

namespace {
constexpr uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr char kApSsid[] = "S3-ESP-NOW";
constexpr char kApPass[] = "espnow123";
constexpr uint8_t kWifiChannel = 6;
constexpr int kMinInput = 10;
constexpr int kMaxInput = 100;
constexpr uint32_t kMinFreqHz = 60000;
constexpr uint32_t kMaxFreqHz = 160000;
constexpr int kLedPin = 2;

struct ControlPacket {
  uint32_t freq_hz;
  uint8_t direction;
};

ControlPacket g_packet{ kMinFreqHz, 1 };

uint32_t mapInputToFreq(int value) {
  if (value < kMinInput) {
    value = kMinInput;
  }
  if (value > kMaxInput) {
    value = kMaxInput;
  }
  const uint32_t span = kMaxFreqHz - kMinFreqHz;
  const uint32_t scaled = static_cast<uint32_t>(value - kMinInput) * span;
  return kMinFreqHz + (scaled / static_cast<uint32_t>(kMaxInput - kMinInput));
}

void onDataSent(const uint8_t *, esp_now_send_status_t status) {
  Serial.print("ESP-NOW send: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

bool parseLine(String line, ControlPacket &out_packet) {
  line.trim();
  line.toLowerCase();

  if (line == "forward") {
    out_packet.direction = 1;
    return true;
  }
  if (line == "reverse") {
    out_packet.direction = 0;
    return true;
  }

  bool is_number = true;
  for (size_t i = 0; i < line.length(); ++i) {
    if (!isDigit(line[i])) {
      is_number = false;
      break;
    }
  }
  if (!is_number || line.isEmpty()) {
    return false;
  }

  const int value = line.toInt();
  out_packet.freq_hz = mapInputToFreq(value);
  return true;
}

void blinkConnectionConfirmed() {
  for (int i = 0; i < 5; i++) {
    digitalWrite(kLedPin, HIGH);
    delay(200);
    digitalWrite(kLedPin, LOW);
    delay(200);
  }
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, LOW);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(kApSsid, kApPass, kWifiChannel);
  Serial.print("S3 MAC: ");
  Serial.println(WiFi.softAPmacAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peer_info = {};
  memcpy(peer_info.peer_addr, kBroadcastMac, sizeof(kBroadcastMac));
  peer_info.channel = kWifiChannel;
  peer_info.encrypt = false;

  if (esp_now_add_peer(&peer_info) != ESP_OK) {
    Serial.println("ESP-NOW add peer failed");
    return;
  }

  Serial.println("ESP-NOW connection established!");
  blinkConnectionConfirmed();
  Serial.println("Type 10-100 to set freq, or 'forward'/'reverse'.");
}

void loop() {
  if (!Serial.available()) {
    return;
  }

  String line = Serial.readStringUntil('\n');
  ControlPacket updated = g_packet;
  if (!parseLine(line, updated)) {
    Serial.println("Invalid input. Use 10-100, forward, or reverse.");
    return;
  }

  g_packet = updated;
  const esp_err_t result = esp_now_send(kBroadcastMac, reinterpret_cast<uint8_t*>(&g_packet), sizeof(g_packet));
  if (result != ESP_OK) {
    Serial.println("ESP-NOW send failed");
  }
}