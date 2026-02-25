#pragma once

#include <Arduino.h>

class Ultrasonic {
public:
  Ultrasonic(uint8_t trig_pin, uint8_t echo_pin);

  void init();
  bool isObstacle();
  int getRawValue(); // returns distance in cm (or -1 on timeout)
  void printStatus();

private:
  uint8_t trig_pin_;
  uint8_t echo_pin_;
  unsigned long timeout_us_ = 30000; // 30ms timeout (~5 meters)
  int last_distance_cm_ = -1;
};
