#include "ESPNOW_S3.h"

#include <WiFi.h>
#include <esp_now.h>

namespace {

uint8_t g_target_mac[6] = {0};

// Received C3 MAC address from announcement packet
volatile bool g_c3_mac_received = false;
uint8_t g_c3_mac[6] = {0};
char g_c3_mac_line[48] = {0};

// Buffered motor status for safe printing from loop()
volatile bool g_motor_new = false;
char g_motor_line[64] = {0};

// Buffered IR status for safe printing from loop()
volatile bool g_ir_new = false;
char g_ir_line[32] = {0};

// Buffered color status for safe printing from loop()
volatile bool g_color_new = false;
char g_color_line[48] = {0};

// ── Motor state verification ─────────────────────────────────────
struct MotorExpected {
  uint8_t duty;       // Raw user duty (0-100) — NOT the mapped value
  uint8_t direction;  // 0=reverse, 1=forward
  uint8_t enable;     // 0=stop, 1=run
  unsigned long sent_time;
  int resend_count;
  bool pending;       // true = waiting for C3 to match
};
MotorExpected g_motor_exp = {0, 1, 0, 0, 0, false};
constexpr unsigned long kResendDelayMs = 400;
constexpr int kMaxResends = 5;

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {}

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len <= 0) return;

  const uint8_t type = data[0];

  // MAC address announcement packet
  if (type == kPacketMacAddr && len >= static_cast<int>(sizeof(MacAddrPacket))) {
    MacAddrPacket mp = {};
    memcpy(&mp, data, sizeof(mp));
    memcpy(g_c3_mac, mp.mac, 6);
    snprintf(g_c3_mac_line, sizeof(g_c3_mac_line),
             "[C3 MAC] %02X:%02X:%02X:%02X:%02X:%02X",
             mp.mac[0], mp.mac[1], mp.mac[2], mp.mac[3], mp.mac[4], mp.mac[5]);
    g_c3_mac_received = true;
    return;
  }

  // Motor status packet
  if (type == kPacketStatus && len >= static_cast<int>(sizeof(MotorStatusPacket))) {
    MotorStatusPacket ms = {};
    memcpy(&ms, data, sizeof(ms));
    uint16_t rpm = (static_cast<uint16_t>(ms.rpm_high) << 8) | ms.rpm_low;
    snprintf(g_motor_line, sizeof(g_motor_line), "[Motor] %d%% %s %s %dRPM",
             ms.duty_cycle,
             ms.direction ? "FWD" : "REV",
             ms.enable ? "ON" : "OFF",
             rpm);
    g_motor_new = true;

    // ── Verify against expected state ──────────────────────────────
    if (g_motor_exp.pending) {
      bool dir_ok = (ms.direction == g_motor_exp.direction);
      bool en_ok  = (ms.enable == g_motor_exp.enable);
      // Duty check: if disabled, expect 0. If enabled, allow ramping
      // (motor may still be ramping — only flag mismatch for en/dir)
      if (dir_ok && en_ok) {
        g_motor_exp.pending = false;  // C3 confirmed
      }
    }
    return;
  }

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
    return;
  }

  if (type == kPacketColorStatus && len >= static_cast<int>(sizeof(ColorStatusPacket))) {
    ColorStatusPacket cs = {};
    memcpy(&cs, data, sizeof(cs));

    uint16_t lux = (static_cast<uint16_t>(cs.lux_high) << 8) | cs.lux_low;
    switch (cs.mode) {
      case 0:  // RGB only
        snprintf(g_color_line, sizeof(g_color_line),
                 "[Color] R:%d G:%d B:%d", cs.r, cs.g, cs.b);
        break;
      case 1:  // Lux only
        snprintf(g_color_line, sizeof(g_color_line),
                 "[Color] %dlx", lux);
        break;
      default: // RGB + Lux
        snprintf(g_color_line, sizeof(g_color_line),
                 "[Color] R:%d G:%d B:%d  %dlx", cs.r, cs.g, cs.b, lux);
        break;
    }
    g_color_new = true;
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

void espnow_send_motor_control(uint8_t duty, uint8_t direction, uint8_t enable) {
  motorControlPacket pkt = {};
  pkt.type      = kPacketControl;
  pkt.duty_cycle = duty;
  pkt.direction  = direction;
  pkt.enable     = enable;
  esp_now_send(g_target_mac, reinterpret_cast<const uint8_t *>(&pkt),
               sizeof(pkt));
}

void espnow_set_motor_expected(uint8_t duty, uint8_t direction, uint8_t enable) {
  g_motor_exp.duty      = duty;
  g_motor_exp.direction = direction;
  g_motor_exp.enable    = enable;
  g_motor_exp.sent_time = millis();
  g_motor_exp.resend_count = 0;
  g_motor_exp.pending   = true;
}

void espnow_send_ir_control(uint8_t mode) {
  IRControlPacket pkt = {};
  pkt.type = kPacketIRControl;
  pkt.mode = mode;
  esp_now_send(g_target_mac, reinterpret_cast<const uint8_t *>(&pkt),
               sizeof(pkt));
}

void espnow_send_color_control(uint8_t command) {
  ColorControlPacket pkt = {};
  pkt.type    = kPacketColorControl;
  pkt.command = command;
  esp_now_send(g_target_mac, reinterpret_cast<const uint8_t *>(&pkt),
               sizeof(pkt));
}

bool espnow_get_c3_mac(uint8_t *out_mac) {
  bool has_mac = (g_c3_mac[0] != 0 || g_c3_mac[1] != 0 || g_c3_mac[2] != 0 ||
                  g_c3_mac[3] != 0 || g_c3_mac[4] != 0 || g_c3_mac[5] != 0);
  if (has_mac && out_mac) {
    memcpy(out_mac, g_c3_mac, 6);
  }
  return has_mac;
}

void espnow_process() {
  // ── Motor state verification resend ─────────────────────────────
  if (g_motor_exp.pending &&
      g_motor_exp.resend_count < kMaxResends &&
      millis() - g_motor_exp.sent_time >= kResendDelayMs) {
    espnow_send_motor_control(g_motor_exp.duty,
                              g_motor_exp.direction,
                              g_motor_exp.enable);
    g_motor_exp.sent_time = millis();
    g_motor_exp.resend_count++;
  }

  if (g_c3_mac_received) {
    g_c3_mac_received = false;
    Serial.println(g_c3_mac_line);
  }
  if (g_motor_new) {
    g_motor_new = false;
    Serial.println(g_motor_line);
  }
  if (g_ir_new) {
    g_ir_new = false;
    Serial.println(g_ir_line);
  }
  if (g_color_new) {
    g_color_new = false;
    Serial.println(g_color_line);
  }
}