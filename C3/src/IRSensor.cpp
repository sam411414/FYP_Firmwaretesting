#include "IRSensor.h"

IRSensor::IRSensor(uint8_t digital_pin, uint8_t analog_pin)
    : digital_pin_(digital_pin), analog_pin_(analog_pin),
      mode_(kIRDigital), obstacle_(false), analog_value_(0),
      state_changed_(false), raw_digital_(false), debounce_time_(0) {
}

void IRSensor::init() {
  // Default mode is digital — configure pins accordingly
  activateDigitalMode();
}

void IRSensor::update() {
  if (mode_ == kIRDigital) {
    // Only read digital pin — ADC is disabled
    bool raw = digitalRead(digital_pin_) == LOW;
    if (raw != raw_digital_) {
      raw_digital_ = raw;
      debounce_time_ = millis();
    }
    if (millis() - debounce_time_ >= kDebounceMs && obstacle_ != raw_digital_) {
      obstacle_ = raw_digital_;
      state_changed_ = true;
    }
  } else {
    // Only read analog pin — digital pin is floating
    int prev_analog = analog_value_;
    analog_value_ = analogRead(analog_pin_);
    if (abs(analog_value_ - prev_analog) > 50) {
      state_changed_ = true;
    }
  }
}

void IRSensor::handleControl(const IRControlPacket &cmd) {
  IRMode new_mode = (cmd.mode == 1) ? kIRAnalog : kIRDigital;
  if (new_mode != mode_) {
    mode_ = new_mode;
    if (mode_ == kIRDigital) {
      activateDigitalMode();
    } else {
      activateAnalogMode();
    }
    state_changed_ = true;
  }
}

void IRSensor::activateDigitalMode() {
  // Enable digital input, release analog pin
  pinMode(digital_pin_, INPUT);
  pinMode(analog_pin_, INPUT);  // High-Z: stop loading the AO line
}

void IRSensor::activateAnalogMode() {
  // Enable analog reading, release digital pin
  pinMode(digital_pin_, INPUT);  // High-Z: stop loading the DO line
  // analogRead() will configure the ADC channel automatically
}
