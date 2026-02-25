#pragma once

#include <Arduino.h>

// ── Encoder pin assignments (ESP32-C3-MINI-1) ──────────────────────
// GPIO0 and GPIO1 are free, support interrupts, and are not strapping pins.
// Connect encoder Phase A → GPIO0, Phase B → GPIO1.
constexpr uint8_t kEncoderPinA = 0;
constexpr uint8_t kEncoderPinB = 1;

// ── 25GA370 280RPM DC Gear Motor with Encoder specifications ───────
constexpr float kEncoderPPR = 11.0f;   // Pulses per revolution (encoder disk)
constexpr float kGearRatio  = 21.3f;   // Gearbox ratio (adjust if your variant differs)

// Full quadrature decoding: 4 edges per pulse × PPR × gear ratio
constexpr float kCountsPerOutputRev = kEncoderPPR * 4.0f * kGearRatio;

// RPM recalculation sampling interval (ms)
constexpr unsigned long kRpmSampleIntervalMs = 100;

class Encoder {
public:
  Encoder(uint8_t pin_a = kEncoderPinA, uint8_t pin_b = kEncoderPinB);

  // Configure pins and attach interrupts
  void init();

  // Recalculate RPM — call frequently from loop()
  void update();

  // Getters
  uint16_t getRPM() const { return rpm_; }
  int32_t  getCount() const { return pulse_count_; }

  // Reset the accumulated pulse count
  void resetCount();

  // ISR member handlers (public so static trampolines can reach them)
  void IRAM_ATTR handleA();
  void IRAM_ATTR handleB();

private:
  uint8_t pin_a_;
  uint8_t pin_b_;

  volatile int32_t pulse_count_;   // Accumulated quadrature count
  unsigned long last_sample_time_;
  uint16_t rpm_;

  // Singleton for ISR routing (one encoder instance supported)
  static Encoder *instance_;
  static void IRAM_ATTR isrA();
  static void IRAM_ATTR isrB();
};
