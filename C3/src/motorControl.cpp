#include "motorControl.h"
#include "Encoder.h"

MotorController::MotorController() 
  : freq_hz_(kDefaultFreqHz), current_duty_pct_(0), target_duty_pct_(0),
    saved_target_duty_(0), last_ramp_time_(0), direction_(true),
    pending_direction_(true), direction_change_pending_(false), enabled_(true),
    state_changed_(false), encoder_(nullptr) {
}

void MotorController::initialize() {
  // Configure direction pin
  pinMode(kPhasePin, OUTPUT);
  
  // Configure PWM pin  
  ledcAttachPin(kEnablePin, kPwmChannel);
  
  // Apply initial PWM settings
  applyPwm();
}

void MotorController::setDutyCycle(uint8_t duty_percent) {
  // Clamp input to valid range (0-100)
  if (duty_percent > 100) duty_percent = 100;
  
  // Map user input to actual duty cycle
  target_duty_pct_ = mapDutyCycle(duty_percent);
  
  // Don't call applyPwm() here - let update() handle ramping
}

void MotorController::setDirection(bool forward) {
  direction_ = forward;
  applyPwm();
}

void MotorController::setEnabled(bool enabled) {
  enabled_ = enabled;
  if (!enabled_) {
    // Zero duty for clean restart when re-enabled
    target_duty_pct_ = 0;
    current_duty_pct_ = 0;
    direction_change_pending_ = false;
  }
  applyPwm();
}

void MotorController::applyPwm() {
  // Configure PWM channel with current frequency and resolution
  ledcSetup(kPwmChannel, freq_hz_, kPwmResolutionBits);
  
  // ── DRV8835 PH/EN mode (MODE pin HIGH) ───────────────────────────
  //   PHASE pin = direction  (HIGH = one way, LOW = other)
  //   ENABLE pin = speed PWM (duty directly proportional to speed)
  //   No inversion needed — both directions are linear.
  
  digitalWrite(kPhasePin, direction_ ? HIGH : LOW);
  
  if (!enabled_ || current_duty_pct_ == 0) {
    ledcWrite(kPwmChannel, 0);
    return;
  }
  
  const uint32_t duty_value = (1023u * current_duty_pct_) / 100u;
  ledcWrite(kPwmChannel, duty_value);
}

void MotorController::update() {
  // Update encoder RPM if attached
  if (encoder_) {
    encoder_->update();
  }

  // If at target, check if a direction change needs completing
  if (current_duty_pct_ == target_duty_pct_) {
    if (direction_change_pending_ && current_duty_pct_ == 0) {
      // Motor has ramped to 0 — safe to flip direction
      direction_ = pending_direction_;
      direction_change_pending_ = false;
      // Now ramp back up to the saved target
      target_duty_pct_ = saved_target_duty_;
    }
    if (current_duty_pct_ == target_duty_pct_) {
      return;  // Nothing more to do
    }
  }
  
  unsigned long current_time = millis();
  
  // Check if 10ms has elapsed since last ramp update
  if (current_time - last_ramp_time_ >= 10) {
    // Update timestamp
    last_ramp_time_ = current_time;
    
    // Ramp toward target by 1%
    if (current_duty_pct_ < target_duty_pct_) {
      current_duty_pct_++;
    } else if (current_duty_pct_ > target_duty_pct_) {
      current_duty_pct_--;
    }
    
    // Apply the new duty cycle
    applyPwm();
  }
}

uint8_t MotorController::mapDutyCycle(uint8_t input_percent) {
  // If input is below threshold, stop the motor
  if (input_percent < kDutyStopThreshold) {
    return 0;
  }
  
  // Map 0-100 input linearly to kDutyLowerLimit..kDutyUpperLimit
  // Formula: actual_duty = lower + (input * (upper - lower)) / 100
  uint8_t range = kDutyUpperLimit - kDutyLowerLimit;
  uint8_t mapped_duty = kDutyLowerLimit + (static_cast<uint16_t>(input_percent) * range) / 100;
  
  // Clamp to limits
  if (mapped_duty < kDutyLowerLimit) mapped_duty = kDutyLowerLimit;
  if (mapped_duty > kDutyUpperLimit) mapped_duty = kDutyUpperLimit;
  
  return mapped_duty;
}

void MotorController::handleControl(const motorControlPacket &cmd) {
  // Store previous state for change detection
  uint8_t prev_target = target_duty_pct_;
  bool prev_direction = direction_;
  bool prev_enabled = enabled_;
  
  // Update direction (phase pin updated by applyPwm below)
  direction_ = (cmd.direction == 1);
  
  if (cmd.enable == 1) {
    // Enabling motor with a duty cycle
    enabled_ = true;
    // Map and set target — ramp will handle the rest
    uint8_t mapped = mapDutyCycle(cmd.duty_cycle);
    target_duty_pct_ = mapped;
  } else {
    // Disabling: zero everything for a clean state
    enabled_ = false;
    target_duty_pct_ = 0;
    current_duty_pct_ = 0;
    direction_change_pending_ = false;
  }
  
  // Apply current state to hardware immediately
  applyPwm();
  
  // Send immediate status update if state changed
  if (prev_target != target_duty_pct_ || prev_direction != direction_ || prev_enabled != enabled_) {
    state_changed_ = true;
  }
}

void MotorController::attachEncoder(Encoder *enc) {
  encoder_ = enc;
}

uint16_t MotorController::getRPM() const {
  return encoder_ ? encoder_->getRPM() : 0;
}