#include "ESPNOW_C3.h"
#include "motorControl.h"
#include "IRSensor.h"
#include "ColorSensor.h"

#include <WiFi.h>
#include <esp_now.h>

namespace {

// Internal struct — only used for sending motor status
struct StatusPacket {
  uint8_t type;
  uint8_t duty_cycle;
  uint8_t direction;
  uint8_t enable;
};

// ── Registered subsystems (nullptr = not registered) ────────────────
MotorController *g_motor = nullptr;
IRSensor        *g_ir    = nullptr;
ColorSensor     *g_color = nullptr;

constexpr uint8_t kS3MacAddr[6] = {0x30, 0xED, 0xA0, 0x27, 0x8F, 0xA4};
uint8_t g_s3_mac[6] = {0};
bool g_s3_peer_added = false;

// Per-subsystem periodic timers
constexpr unsigned long kMotorStatusIntervalMs = 5000;
constexpr unsigned long kIRStatusIntervalMs    = 500;
constexpr unsigned long kColorStatusIntervalMs = 500;
unsigned long g_last_motor_status_time = 0;
unsigned long g_last_ir_status_time    = 0;
unsigned long g_last_color_status_time = 0;

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {}

bool ensureS3Peer() {
  if (g_s3_peer_added) return true;
  memcpy(g_s3_mac, kS3MacAddr, sizeof(g_s3_mac));
  esp_now_peer_info_t peer_info = {};
  memcpy(peer_info.peer_addr, g_s3_mac, 6);
  peer_info.channel = 0;
  peer_info.encrypt = false;
  if (esp_now_add_peer(&peer_info) == ESP_OK) {
    g_s3_peer_added = true;
    return true;
  }
  Serial.println("Failed to add S3 peer");
  return false;
}

void sendMotorStatus() {
  if (g_motor == nullptr || !ensureS3Peer()) return;

  StatusPacket status = {};
  status.type = kPacketStatus;
  status.duty_cycle = g_motor->getCurrentDutyCycle();
  status.direction = g_motor->getDirection() ? 1 : 0;
  status.enable = g_motor->isEnabled() ? 1 : 0;

  esp_now_send(g_s3_mac, reinterpret_cast<const uint8_t *>(&status), sizeof(status));
}

void sendIRStatus() {
  if (g_ir == nullptr || !ensureS3Peer()) return;

  IRStatusPacket pkt = {};
  pkt.type = kPacketIRStatus;
  pkt.mode = static_cast<uint8_t>(g_ir->getMode());
  pkt.digital_state = g_ir->isObstacle() ? 1 : 0;
  int analog = g_ir->getAnalogValue();
  pkt.analog_high = static_cast<uint8_t>((analog >> 8) & 0xFF);
  pkt.analog_low  = static_cast<uint8_t>(analog & 0xFF);

  esp_now_send(g_s3_mac, reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
}

void sendColorStatus() {
  if (g_color == nullptr || !ensureS3Peer()) return;

  ColorStatusPacket pkt = {};
  pkt.type     = kPacketColorStatus;
  pkt.mode     = static_cast<uint8_t>(g_color->getMode());
  pkt.r        = g_color->getRed();
  pkt.g        = g_color->getGreen();
  pkt.b        = g_color->getBlue();
  uint16_t lux = g_color->getLux();
  pkt.lux_high = static_cast<uint8_t>((lux >> 8) & 0xFF);
  pkt.lux_low  = static_cast<uint8_t>( lux       & 0xFF);

  esp_now_send(g_s3_mac, reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
}

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len <= 0) return;

  const uint8_t type = data[0];

  // ── Motor control packet (only if motor registered) ──────────────
  if (type == kPacketControl &&
      len >= static_cast<int>(sizeof(motorControlPacket)) &&
      g_motor != nullptr) {
    motorControlPacket cmd = {};
    memcpy(&cmd, data, sizeof(cmd));
    g_motor->handleControl(cmd);
    return;
  }

  // ── IR control packet (only if IR registered) ────────────────────
  if (type == kPacketIRControl &&
      len >= static_cast<int>(sizeof(IRControlPacket)) &&
      g_ir != nullptr) {
    IRControlPacket cmd = {};
    memcpy(&cmd, data, sizeof(cmd));
    g_ir->handleControl(cmd);
    return;
  }

  // ── Color control packet (only if color registered) ──────────────
  if (type == kPacketColorControl &&
      len >= static_cast<int>(sizeof(ColorControlPacket)) &&
      g_color != nullptr) {
    ColorControlPacket cmd = {};
    memcpy(&cmd, data, sizeof(cmd));
    g_color->handleControl(cmd);
    return;
  }
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────

void espnow_init() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);
  ensureS3Peer();
}


void espnow_register_motor(MotorController *motor) {
  g_motor = motor;
}

void espnow_register_ir(IRSensor *ir) {
  g_ir = ir;
}

void espnow_register_color(ColorSensor *cs) {
  g_color = cs;
}

void espnow_update() {
  unsigned long now = millis();

  // ── Motor status ──────────────────────────────────────────────────
  if (g_motor != nullptr) {
    if (g_motor->checkAndClearStateChanged()) {
      sendMotorStatus();
    }
    if (now - g_last_motor_status_time >= kMotorStatusIntervalMs) {
      g_last_motor_status_time = now;
      sendMotorStatus();
    }
  }

  // ── IR status ─────────────────────────────────────────────────────
  if (g_ir != nullptr) {
    if (g_ir->checkAndClearStateChanged()) {
      sendIRStatus();
    }
    if (now - g_last_ir_status_time >= kIRStatusIntervalMs) {
      g_last_ir_status_time = now;
      sendIRStatus();
    }
  }

  // ── Color status ──────────────────────────────────────────────────
  if (g_color != nullptr) {
    if (g_color->checkAndClearStateChanged()) {
      sendColorStatus();
    }
    if (now - g_last_color_status_time >= kColorStatusIntervalMs) {
      g_last_color_status_time = now;
      sendColorStatus();
    }
  }
}
