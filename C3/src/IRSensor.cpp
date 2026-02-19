#include "IRSensor.h"

IRSensor::IRSensor(uint8_t pin) : pin_(pin) {
}

void IRSensor::init() {
  pinMode(pin_, INPUT);
  Serial.print("IR Sensor initialized on pin ");
  Serial.println(pin_);
}

bool IRSensor::isObstacle() {
  // This IR sensor returns LOW when obstacle detected, HIGH when clear
  return digitalRead(pin_) == LOW;
}

int IRSensor::getRawValue() {
  return digitalRead(pin_);
}

void IRSensor::printStatus() {
  Serial.print("IR Sensor (pin ");
  Serial.print(pin_);
  Serial.print("): ");
  if (isObstacle()) {
    Serial.println("OBSTACLE DETECTED");
  } else {
    Serial.println("Clear");
  }
}
