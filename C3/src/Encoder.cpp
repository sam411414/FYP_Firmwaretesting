#include "Encoder.h"

Encoder *Encoder::instance_ = nullptr;

Encoder::Encoder(uint8_t pin_a, uint8_t pin_b)
    : pin_a_(pin_a), pin_b_(pin_b),
      pulse_count_(0), last_sample_time_(0), rpm_(0) {
  instance_ = this;
}

void Encoder::init() {
  pinMode(pin_a_, INPUT_PULLUP);
  pinMode(pin_b_, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(pin_a_), isrA, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pin_b_), isrB, CHANGE);

  last_sample_time_ = millis();
}

void Encoder::update() {
  unsigned long now = millis();
  unsigned long elapsed = now - last_sample_time_;

  if (elapsed >= kRpmSampleIntervalMs) {
    // Atomically read and reset count
    noInterrupts();
    int32_t count = pulse_count_;
    pulse_count_ = 0;
    interrupts();

    // Convert count → output-shaft RPM
    float revolutions = static_cast<float>(abs(count)) / kCountsPerOutputRev;
    rpm_ = static_cast<uint16_t>((revolutions * 60000.0f) / static_cast<float>(elapsed));

    last_sample_time_ = now;
  }
}

void Encoder::resetCount() {
  noInterrupts();
  pulse_count_ = 0;
  interrupts();
}

// ── Static ISR trampolines ──────────────────────────────────────────

void IRAM_ATTR Encoder::isrA() {
  if (instance_) instance_->handleA();
}

void IRAM_ATTR Encoder::isrB() {
  if (instance_) instance_->handleB();
}

// ── Full quadrature decoding (4× resolution) ───────────────────────
//
// Standard quadrature truth table:
//   Phase A changed:  A == B → reverse (--),  A != B → forward (++)
//   Phase B changed:  A == B → forward (++),  A != B → reverse (--)
//
// If rotation appears inverted, swap the encoder wires.

void IRAM_ATTR Encoder::handleA() {
  if (digitalRead(pin_a_) == digitalRead(pin_b_)) {
    pulse_count_--;
  } else {
    pulse_count_++;
  }
}

void IRAM_ATTR Encoder::handleB() {
  if (digitalRead(pin_a_) == digitalRead(pin_b_)) {
    pulse_count_++;
  } else {
    pulse_count_--;
  }
}
