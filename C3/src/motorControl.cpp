#include "motorControl.h"

MotorController::MotorController() 
  : freq_hz_(kDefaultFreqHz), duty_pct_(100), direction_(true), enabled_(true) {
}

void MotorController::initialize() {
  // Configure direction pin
  pinMode(kPhasePin, OUTPUT);
  digitalWrite(kPhasePin, direction_ ? HIGH : LOW);
  
  // Configure PWM pin  
  ledcAttachPin(kEnablePin, kPwmChannel);
  
  // Apply initial PWM settings
  applyPwm();
}

void MotorController::setDutyCycle(uint8_t duty_percent) {
  // Clamp duty cycle to valid range
  if (duty_percent < 40) duty_percent = 40;
  if (duty_percent > 100) duty_percent = 100;
  
  duty_pct_ = duty_percent;
  applyPwm();
}

void MotorController::setDirection(bool forward) {
  direction_ = forward;
  digitalWrite(kPhasePin, direction_ ? HIGH : LOW);
}

void MotorController::setEnabled(bool enabled) {
  enabled_ = enabled;
  applyPwm();
}

void MotorController::applyPwm() {
  // Configure PWM channel with current frequency and resolution
  ledcSetup(kPwmChannel, freq_hz_, kPwmResolutionBits);
  
  // Calculate duty value (0-255 for 8-bit resolution)
  const uint32_t duty_value = enabled_ ? (255u * duty_pct_) / 100u : 0u;
  
  // Apply PWM signal
  ledcWrite(kPwmChannel, duty_value);
}