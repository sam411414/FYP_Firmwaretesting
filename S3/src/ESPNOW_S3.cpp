#include "ESPNOW_S3.h"

#include <WiFi.h>
#include <esp_now.h>

// ═══════════════════════════════════════════════════════════════════
// ESP-NOW Hub (S3) — Multi-Peripheral Transport Layer
//
// Architecture:
//   - S3 polls each registered C3 round-robin via kPacketPoll.
//   - C3 responds immediately with its current status packet.
//   - C3 may also push unsolicited packets on state change (hybrid).
//   - S3 outputs one JSON line per device update on Serial.
// ═══════════════════════════════════════════════════════════════════

namespace {

// ── Per-device runtime state ──────────────────────────────────────

struct DeviceState {
  // Common
  bool     online;
  bool     new_data;        // Set in onDataRecv, consumed by process()
  unsigned long last_seen;  // millis() of last packet

  // Motor-specific
  uint8_t  motor_duty;
  uint8_t  motor_dir;
  uint8_t  motor_enable;
  uint16_t motor_rpm;

  // Motor command verification
  struct {
    uint8_t  duty;
    uint8_t  dir;
    uint8_t  enable;
    unsigned long sent_time;
    int      resend_count;
    bool     pending;
  } motor_exp;

  // IR-specific
  uint8_t  ir_mode;
  uint8_t  ir_digital;
  uint16_t ir_analog;

  // Color-specific
  uint8_t  color_mode;
  uint8_t  color_r;
  uint8_t  color_g;
  uint8_t  color_b;
  uint16_t color_lux;
};

DeviceState g_dev[kNumDevices] = {};

// ── Poll state machine ────────────────────────────────────────────

int           g_poll_idx     = 0;      // Current device being polled (0-based)
bool          g_poll_waiting = false;   // Waiting for response from g_poll_idx
unsigned long g_poll_sent_at = 0;       // millis() when poll was sent

// ── Helper: find device index by MAC (returns -1 if not found) ────

int findDeviceByMac(const uint8_t *mac) {
  for (int i = 0; i < kNumDevices; i++) {
    if (memcmp(kDeviceList[i].mac, mac, 6) == 0) return i;
  }
  return -1;
}

// ── Helper: type name string ──────────────────────────────────────

const char *typeName(PeripheralType t) {
  switch (t) {
    case kPeripheralMotor: return "motor";
    case kPeripheralIR:    return "ir";
    case kPeripheralColor: return "color";
    default:               return "unknown";
  }
}

// ── JSON output helpers ───────────────────────────────────────────

void printMotorJson(int idx) {
  const DeviceState &d = g_dev[idx];
  char buf[128];
  snprintf(buf, sizeof(buf),
    "{\"slot\":%d,\"type\":\"motor\",\"online\":true,"
    "\"duty\":%d,\"dir\":%d,\"en\":%d,\"rpm\":%d}",
    idx + 1, d.motor_duty, d.motor_dir, d.motor_enable, d.motor_rpm);
  Serial.println(buf);
}

void printIRJson(int idx) {
  const DeviceState &d = g_dev[idx];
  char buf[96];
  snprintf(buf, sizeof(buf),
    "{\"slot\":%d,\"type\":\"ir\",\"online\":true,"
    "\"mode\":%d,\"digital\":%d,\"analog\":%d}",
    idx + 1, d.ir_mode, d.ir_digital, d.ir_analog);
  Serial.println(buf);
}

void printColorJson(int idx) {
  const DeviceState &d = g_dev[idx];
  char buf[128];
  snprintf(buf, sizeof(buf),
    "{\"slot\":%d,\"type\":\"color\",\"online\":true,"
    "\"mode\":%d,\"r\":%d,\"g\":%d,\"b\":%d,\"lux\":%d}",
    idx + 1, d.color_mode, d.color_r, d.color_g, d.color_b, d.color_lux);
  Serial.println(buf);
}

void printOfflineJson(int idx) {
  char buf[64];
  snprintf(buf, sizeof(buf),
    "{\"slot\":%d,\"type\":\"%s\",\"online\":false}",
    idx + 1, typeName(kDeviceList[idx].type));
  Serial.println(buf);
}

void printDeviceJson(int idx) {
  switch (kDeviceList[idx].type) {
    case kPeripheralMotor: printMotorJson(idx); break;
    case kPeripheralIR:    printIRJson(idx);    break;
    case kPeripheralColor: printColorJson(idx); break;
  }
}

// ── ESP-NOW callbacks ─────────────────────────────────────────────

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  (void)mac; (void)status;
}

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len <= 0) return;

  int idx = findDeviceByMac(mac);
  if (idx < 0) return;  // Packet from unknown device — ignore

  const uint8_t pkt_type = data[0];
  DeviceState &d = g_dev[idx];
  d.last_seen = millis();
  d.online = true;

  // ── Motor status ──────────────────────────────────────────────
  if (pkt_type == kPacketStatus &&
      len >= static_cast<int>(sizeof(MotorStatusPacket)) &&
      kDeviceList[idx].type == kPeripheralMotor) {
    MotorStatusPacket ms = {};
    memcpy(&ms, data, sizeof(ms));
    d.motor_duty   = ms.duty_cycle;
    d.motor_dir    = ms.direction;
    d.motor_enable = ms.enable;
    d.motor_rpm    = (static_cast<uint16_t>(ms.rpm_high) << 8) | ms.rpm_low;
    d.new_data = true;

    // Verify against expected state
    if (d.motor_exp.pending) {
      if (ms.direction == d.motor_exp.dir && ms.enable == d.motor_exp.enable) {
        d.motor_exp.pending = false;  // Acknowledged
      }
    }
    return;
  }

  // ── IR status ─────────────────────────────────────────────────
  if (pkt_type == kPacketIRStatus &&
      len >= static_cast<int>(sizeof(IRStatusPacket)) &&
      kDeviceList[idx].type == kPeripheralIR) {
    IRStatusPacket ir = {};
    memcpy(&ir, data, sizeof(ir));
    d.ir_mode    = ir.mode;
    d.ir_digital = ir.digital_state;
    d.ir_analog  = (static_cast<uint16_t>(ir.analog_high) << 8) | ir.analog_low;
    d.new_data = true;
    return;
  }

  // ── Color status ──────────────────────────────────────────────
  if (pkt_type == kPacketColorStatus &&
      len >= static_cast<int>(sizeof(ColorStatusPacket)) &&
      kDeviceList[idx].type == kPeripheralColor) {
    ColorStatusPacket cs = {};
    memcpy(&cs, data, sizeof(cs));
    d.color_mode = cs.mode;
    d.color_r    = cs.r;
    d.color_g    = cs.g;
    d.color_b    = cs.b;
    d.color_lux  = (static_cast<uint16_t>(cs.lux_high) << 8) | cs.lux_low;
    d.new_data = true;
    return;
  }
}

// ── Peer management ───────────────────────────────────────────────

bool addPeer(const uint8_t *mac) {
  esp_now_peer_info_t info = {};
  memcpy(info.peer_addr, mac, 6);
  info.channel = 0;
  info.encrypt = false;
  return esp_now_add_peer(&info) == ESP_OK;
}

// ── Poll helpers ──────────────────────────────────────────────────

void sendPoll(int idx) {
  PollPacket pkt = {kPacketPoll};
  esp_now_send(kDeviceList[idx].mac,
               reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
}

void advancePoll() {
  g_poll_idx = (g_poll_idx + 1) % kNumDevices;
  g_poll_waiting = false;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════

void espnow_hub_init() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("{\"event\":\"error\",\"msg\":\"ESP-NOW init failed\"}");
    return;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  // Add every C3 as a peer
  for (int i = 0; i < kNumDevices; i++) {
    if (!addPeer(kDeviceList[i].mac)) {
      char buf[80];
      snprintf(buf, sizeof(buf),
        "{\"event\":\"error\",\"msg\":\"Failed to add peer slot %d\"}", i + 1);
      Serial.println(buf);
    }
  }

  // Zero all runtime state
  memset(g_dev, 0, sizeof(g_dev));
}

void espnow_hub_process() {
  unsigned long now = millis();

  // ── Motor command resend (per-device) ─────────────────────────
  for (int i = 0; i < kNumDevices; i++) {
    if (kDeviceList[i].type != kPeripheralMotor) continue;
    auto &exp = g_dev[i].motor_exp;
    if (exp.pending && exp.resend_count < kMotorMaxResends &&
        now - exp.sent_time >= kMotorResendMs) {
      motorControlPacket pkt = {};
      pkt.type       = kPacketControl;
      pkt.duty_cycle = exp.duty;
      pkt.direction  = exp.dir;
      pkt.enable     = exp.enable;
      esp_now_send(kDeviceList[i].mac,
                   reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
      exp.sent_time = now;
      exp.resend_count++;
    }
  }

  // ── Poll state machine ────────────────────────────────────────
  if (!g_poll_waiting) {
    // Send poll to current device
    g_dev[g_poll_idx].new_data = false;  // Clear before waiting
    sendPoll(g_poll_idx);
    g_poll_sent_at = now;
    g_poll_waiting = true;
  } else {
    // Waiting for response
    if (g_dev[g_poll_idx].new_data) {
      // Response received — output and advance
      printDeviceJson(g_poll_idx);
      g_dev[g_poll_idx].new_data = false;
      advancePoll();
    } else if (now - g_poll_sent_at >= kPollTimeoutMs) {
      // Timeout — mark offline if we haven't heard in a while
      if (now - g_dev[g_poll_idx].last_seen > kPollTimeoutMs * 3) {
        if (g_dev[g_poll_idx].online) {
          g_dev[g_poll_idx].online = false;
          printOfflineJson(g_poll_idx);
        }
      }
      advancePoll();
    }
  }

  // ── Print unsolicited on-change data from other devices ───────
  for (int i = 0; i < kNumDevices; i++) {
    if (i == g_poll_idx) continue;  // Handled by poll FSM
    if (g_dev[i].new_data) {
      printDeviceJson(i);
      g_dev[i].new_data = false;
    }
  }
}

void espnow_send_motor_cmd(uint8_t slot, uint8_t duty, uint8_t dir, uint8_t enable) {
  if (slot < 1 || slot > kNumDevices) return;
  int idx = slot - 1;
  if (kDeviceList[idx].type != kPeripheralMotor) return;

  motorControlPacket pkt = {};
  pkt.type       = kPacketControl;
  pkt.duty_cycle = duty;
  pkt.direction  = dir;
  pkt.enable     = enable;
  esp_now_send(kDeviceList[idx].mac,
               reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));

  // Arm state verification
  auto &exp = g_dev[idx].motor_exp;
  exp.duty         = duty;
  exp.dir          = dir;
  exp.enable       = enable;
  exp.sent_time    = millis();
  exp.resend_count = 0;
  exp.pending      = true;
}

void espnow_send_ir_cmd(uint8_t slot, uint8_t mode) {
  if (slot < 1 || slot > kNumDevices) return;
  int idx = slot - 1;
  if (kDeviceList[idx].type != kPeripheralIR) return;

  IRControlPacket pkt = {};
  pkt.type = kPacketIRControl;
  pkt.mode = mode;
  esp_now_send(kDeviceList[idx].mac,
               reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
}

void espnow_send_color_cmd(uint8_t slot, uint8_t command) {
  if (slot < 1 || slot > kNumDevices) return;
  int idx = slot - 1;
  if (kDeviceList[idx].type != kPeripheralColor) return;

  ColorControlPacket pkt = {};
  pkt.type    = kPacketColorControl;
  pkt.command = command;
  esp_now_send(kDeviceList[idx].mac,
               reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
}

PeripheralType espnow_get_device_type(uint8_t slot) {
  if (slot < 1 || slot > kNumDevices) return kPeripheralMotor;
  return kDeviceList[slot - 1].type;
}

bool espnow_is_device_online(uint8_t slot) {
  if (slot < 1 || slot > kNumDevices) return false;
  return g_dev[slot - 1].online;
}

void espnow_print_registry() {
  Serial.println("[Hub] Peripheral Registry:");
  for (int i = 0; i < kNumDevices; i++) {
    const auto &d = kDeviceList[i];
    char buf[64];
    snprintf(buf, sizeof(buf),
      "  Slot %d: %-6s %02X:%02X:%02X:%02X:%02X:%02X",
      i + 1, typeName(d.type),
      d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
    Serial.println(buf);
  }
}