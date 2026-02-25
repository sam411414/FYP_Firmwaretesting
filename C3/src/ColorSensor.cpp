#include "ColorSensor.h"
#include <Wire.h>

// ── Autorange table (dim → bright) ─────────────────────────────────
// Hysteresis: min_count of first entry = 0 (start), max_count of last = 0 (end)
const ColorSensor::AgcEntry ColorSensor::kAgcTable[] = {
  { TCS34725_GAIN_60X, TCS34725_INTEGRATIONTIME_614MS,     0, 20000 },
  { TCS34725_GAIN_60X, TCS34725_INTEGRATIONTIME_154MS,  4990, 63000 },
  { TCS34725_GAIN_16X, TCS34725_INTEGRATIONTIME_154MS, 16790, 63000 },
  { TCS34725_GAIN_4X,  TCS34725_INTEGRATIONTIME_154MS, 15740, 63000 },
  { TCS34725_GAIN_1X,  TCS34725_INTEGRATIONTIME_154MS, 15740,     0 }
};

ColorSensor::ColorSensor(uint8_t sda_pin, uint8_t scl_pin, uint8_t led_pin)
    : sda_pin_(sda_pin), scl_pin_(scl_pin), led_pin_(led_pin),
      tcs_(kAgcTable[0].integration_time, kAgcTable[0].gain),
      ready_(false), state_changed_(false), saturated_(false),
      mode_(kColorRGBLux), r_(0), g_(0), b_(0), lux_(0),
      last_read_time_(0), agc_index_(0) {
}

bool ColorSensor::init() {
  Wire.begin(sda_pin_, scl_pin_);

  // LED pin: HIGH = on (default), LOW = off
  pinMode(led_pin_, OUTPUT);
  digitalWrite(led_pin_, HIGH);  // LED on by default

  if (!tcs_.begin()) {
    Serial.println("[Color] TCS34725 not found — check wiring (SDA/SCL/VCC/GND)");
    return false;
  }

  applyGainTime();
  ready_ = true;
  state_changed_ = true;  // Force an initial status send
  Serial.println("[Color] TCS34725 ready (autorange enabled)");
  return true;
}

void ColorSensor::applyGainTime() {
  tcs_.setGain(kAgcTable[agc_index_].gain);
  tcs_.setIntegrationTime(kAgcTable[agc_index_].integration_time);
}

uint16_t ColorSensor::getIntegrationMs() const {
  return (256 - kAgcTable[agc_index_].integration_time) * 2.4f;
}

uint16_t ColorSensor::getGainMultiplier() const {
  switch (kAgcTable[agc_index_].gain) {
    case TCS34725_GAIN_1X:  return 1;
    case TCS34725_GAIN_4X:  return 4;
    case TCS34725_GAIN_16X: return 16;
    case TCS34725_GAIN_60X: return 60;
    default:                return 1;
  }
}

void ColorSensor::update() {
  if (!ready_) return;

  // Non-blocking: wait for at least the integration window before reading
  uint16_t interval = getIntegrationMs();
  if (interval < 200) interval = 200;  // floor at 200ms to stay responsive
  if (millis() - last_read_time_ < interval) return;
  last_read_time_ = millis();

  uint16_t r_raw, g_raw, b_raw, c_raw;
  tcs_.getRawData(&r_raw, &g_raw, &b_raw, &c_raw);

  // ── Autorange ─────────────────────────────────────────────────────
  bool ranged = false;
  if (kAgcTable[agc_index_].max_count != 0 && c_raw > kAgcTable[agc_index_].max_count) {
    agc_index_++;
    ranged = true;
  } else if (kAgcTable[agc_index_].min_count != 0 && c_raw < kAgcTable[agc_index_].min_count) {
    agc_index_--;
    ranged = true;
  }
  if (ranged) {
    applyGainTime();
    // Wait for new integration window + shock absorber, then re-read
    delay((uint16_t)((256 - kAgcTable[agc_index_].integration_time) * 2.4f * 2));
    tcs_.getRawData(&r_raw, &g_raw, &b_raw, &c_raw);
  }

  // ── DN40 IR compensation ──────────────────────────────────────────
  uint16_t ir_estimate = (r_raw + g_raw + b_raw > c_raw)
                             ? (r_raw + g_raw + b_raw - c_raw) / 2
                             : 0;
  int32_t r_comp = (int32_t)r_raw - ir_estimate;
  int32_t g_comp = (int32_t)g_raw - ir_estimate;
  int32_t b_comp = (int32_t)b_raw - ir_estimate;
  int32_t c_comp = (int32_t)c_raw - ir_estimate;
  if (r_comp < 0) r_comp = 0;
  if (g_comp < 0) g_comp = 0;
  if (b_comp < 0) b_comp = 0;
  if (c_comp < 0) c_comp = 0;

  // ── Saturation check ──────────────────────────────────────────────
  uint16_t atime = kAgcTable[agc_index_].integration_time;
  uint16_t sat = ((256 - atime) > 63) ? 65535 : 1024 * (256 - atime);
  uint16_t sat75 = (getIntegrationMs() < 150) ? (sat - sat / 4) : sat;
  saturated_ = (getIntegrationMs() < 150 && c_raw > sat75);

  // ── DN40 lux calculation ──────────────────────────────────────────
  float cpl = (float)(getIntegrationMs() * getGainMultiplier()) / (kGA * kDF);
  float raw_lux = (kRCoef * r_comp + kGCoef * g_comp + kBCoef * b_comp) / cpl;
  uint16_t new_lux = (raw_lux > 0) ? (uint16_t)raw_lux : 0;

  // ── Normalize compensated RGB to 0-255 ────────────────────────────
  uint8_t new_r = 0, new_g = 0, new_b = 0;
  if (c_comp > 0) {
    new_r = static_cast<uint8_t>(constrain((r_comp * 255L) / c_comp, 0, 255));
    new_g = static_cast<uint8_t>(constrain((g_comp * 255L) / c_comp, 0, 255));
    new_b = static_cast<uint8_t>(constrain((b_comp * 255L) / c_comp, 0, 255));
  }

  // ── Change detection ──────────────────────────────────────────────
  if (abs((int)new_r   - (int)r_)   > kChangeThreshold ||
      abs((int)new_g   - (int)g_)   > kChangeThreshold ||
      abs((int)new_b   - (int)b_)   > kChangeThreshold ||
      abs((int)new_lux - (int)lux_) > kLuxThreshold) {
    state_changed_ = true;
  }

  r_   = new_r;
  g_   = new_g;
  b_   = new_b;
  lux_ = new_lux;
}

void ColorSensor::handleControl(const ColorControlPacket &cmd) {
  switch (cmd.command) {
    case kColorCmdRGB:
    case kColorCmdLux:
    case kColorCmdRGBLux: {
      ColorMode new_mode = static_cast<ColorMode>(cmd.command);
      if (new_mode != mode_) {
        mode_ = new_mode;
        state_changed_ = true;
      }
      break;
    }
    case kColorCmdLedOn:
      digitalWrite(led_pin_, HIGH);
      break;
    case kColorCmdLedOff:
      digitalWrite(led_pin_, LOW);
      break;
    default:
      break;
  }
}
