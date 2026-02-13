#pragma once

#include <Arduino.h>

// Motor control pin definitions
constexpr int kEnablePin = 2;  // BENABLE - PWM output pin
constexpr int kPhasePin = 3;   // BPHASE - Direction control pin

// PWM configuration constants
constexpr int kPwmChannel = 0;
constexpr int kPwmResolutionBits = 8;
constexpr uint32_t kDefaultFreqHz = 20000;

// Motor control class for DRV8835 interface
class MotorController {
public:
  MotorController();
  
  // Initialize motor controller hardware
  void initialize();
  
  // Set duty cycle (40-100%)
  void setDutyCycle(uint8_t duty_percent);
  
  // Set motor direction (true = forward, false = reverse)
  void setDirection(bool forward);

  // Enable or disable motor output
  void setEnabled(bool enabled);
  
  // Get current settings
  uint8_t getDutyCycle() const { return duty_pct_; }
  uint32_t getFrequency() const { return freq_hz_; }
  bool getDirection() const { return direction_; }
  bool isEnabled() const { return enabled_; }

private:
  uint32_t freq_hz_;
  uint8_t duty_pct_;  
  bool direction_;
  bool enabled_;
  
  // Internal PWM application
  void applyPwm();
};