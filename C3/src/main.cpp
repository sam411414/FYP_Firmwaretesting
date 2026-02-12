#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

namespace {
constexpr char kApSsid[] = "S3-ESP-NOW";
constexpr char kApPass[] = "espnow123";
constexpr uint8_t kWifiChannel = 6;

constexpr int kEnablePin = 2;  // BENABLE
constexpr int kPhasePin = 3;   // BPHASE
constexpr int kLedPin = 8;     // Built-in LED for connection confirmation
constexpr int kPwmChannel = 0;
constexpr int kPwmResolutionBits = 8;
constexpr uint32_t kDefaultFreqHz = 60000;

struct ControlPacket {
  uint32_t freq_hz;
  uint8_t direction;
};

ControlPacket g_packet{ kDefaultFreqHz, 1 };
bool g_connection_confirmed = false;

void applyPwm(uint32_t freq_hz) {
  ledcSetup(kPwmChannel, freq_hz, kPwmResolutionBits);
  const uint32_t duty = (1u << kPwmResolutionBits) / 2u;
  ledcWrite(kPwmChannel, duty);
}

void blinkConnectionConfirmed() {
  for (int i = 0; i < 5; i++) {
    digitalWrite(kLedPin, HIGH);
    delay(200);
    digitalWrite(kLedPin, LOW);
    delay(200);
  }
}

void onDataRecv(const uint8_t *, const uint8_t *data, int len) {
  if (len != sizeof(ControlPacket)) {
    return;
  }

  ControlPacket incoming = {};
  memcpy(&incoming, data, sizeof(incoming));
  g_packet = incoming;

  // Blink LED on first successful packet to confirm connection
  if (!g_connection_confirmed) {
    g_connection_confirmed = true;
    Serial.println("ESP-NOW connection confirmed!");
    blinkConnectionConfirmed();
  }

  digitalWrite(kPhasePin, g_packet.direction ? HIGH : LOW);
  applyPwm(g_packet.freq_hz);
}
} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(kPhasePin, OUTPUT);
  digitalWrite(kPhasePin, HIGH);
  
  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, LOW);

  ledcAttachPin(kEnablePin, kPwmChannel);
  applyPwm(kDefaultFreqHz);

  WiFi.mode(WIFI_STA);
  WiFi.begin(kApSsid, kApPass, kWifiChannel);
  Serial.print("C3 MAC: ");
  Serial.println(WiFi.macAddress());

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(100);
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  Serial.println("C3 ready for ESP-NOW control.");
}

void loop() {
  delay(100);
}