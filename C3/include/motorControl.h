#pragma once

#include <Arduino.h>

// Control packet received over ESP-NOW (motor-specific)
struct motorControlPacket {
  uint8_t type;        // Must be kPacketControl (2)
  uint8_t duty_cycle;  // 40-100
  uint8_t direction;   // 0=reverse, 1=forward
  uint8_t enable;      // 0=stop, 1=run
};

// Motor control pin definitions
constexpr int kEnablePin = 2;  // BENABLE - PWM output pin
constexpr int kPhasePin = 3;   // BPHASE - Direction control pin

// PWM configuration constants
constexpr int kPwmChannel = 0;
constexpr int kPwmResolutionBits = 10;
constexpr uint32_t kDefaultFreqHz = 20000;

// Duty cycle mapping limits (actual PWM duty percentages)
constexpr uint8_t kDutyLowerLimit = 30;  // Minimum actual duty cycle (%)
constexpr uint8_t kDutyUpperLimit = 95;  // Maximum actual duty cycle (%)
constexpr uint8_t kDutyStopThreshold = 15; // Input below this = stop

// Motor control class for DRV8835 interface
class MotorController {
public:
  MotorController();
  
  // Initialize motor controller hardware
  void initialize();
  
  // Set duty cycle (0-100 input, maps to 30-90% actual, <10 = stop)
  void setDutyCycle(uint8_t duty_percent);
  
  // Set motor direction (true = forward, false = reverse)
  void setDirection(bool forward);

  // Enable or disable motor output
  void setEnabled(bool enabled);
  
  // Update ramping - call this frequently in loop()
  void update();
  
  // Handle an incoming motorControlPacket
  void handleControl(const motorControlPacket &cmd);

  // Get current settings
  uint8_t getCurrentDutyCycle() const { return current_duty_pct_; }
  uint8_t getTargetDutyCycle() const { return target_duty_pct_; }
  uint32_t getFrequency() const { return freq_hz_; }
  bool getDirection() const { return direction_; }
  bool isEnabled() const { return enabled_; }
  
  // Check and clear state change flag
  bool checkAndClearStateChanged() { 
    bool changed = state_changed_; 
    state_changed_ = false; 
    return changed; 
  }

private:
  uint32_t freq_hz_;
  uint8_t current_duty_pct_;  // Actual current duty cycle being applied
  uint8_t target_duty_pct_;   // Target duty cycle to ramp toward
  uint8_t saved_target_duty_; // Saved target for direction-change ramp-back
  unsigned long last_ramp_time_;  // Last time ramping was updated
  bool direction_;
  bool pending_direction_;        // Direction to switch to after ramp-down
  bool direction_change_pending_; // True while mid-direction-change
  bool enabled_;
  bool state_changed_;            // Flag indicating motor state has changed
  
  // Internal PWM application
  void applyPwm();
  
  // Map user input (0-100) to actual duty cycle (30-90% or 0)
  uint8_t mapDutyCycle(uint8_t input_percent);
};