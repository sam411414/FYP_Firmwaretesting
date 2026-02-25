#include "Ultrasonic.h"

Ultrasonic::Ultrasonic(uint8_t trig_pin, uint8_t echo_pin)
  : trig_pin_(trig_pin), echo_pin_(echo_pin) {}

void Ultrasonic::init() {
  pinMode(trig_pin_, OUTPUT);
  pinMode(echo_pin_, INPUT);
  digitalWrite(trig_pin_, LOW);
  Serial.print("Ultrasonic initialized TRIG=");
  Serial.print(trig_pin_);
  Serial.print(" ECHO=");
  Serial.println(echo_pin_);
}

bool Ultrasonic::isObstacle() {
  int d = getRawValue();
  // Consider obstacle if < 20 cm (tunable)
  return (d >= 0 && d < 20);
}

int Ultrasonic::getRawValue() {
  // Trigger a 10us pulse
  digitalWrite(trig_pin_, LOW);
  delayMicroseconds(2);
  digitalWrite(trig_pin_, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig_pin_, LOW);

  // Measure echo pulse width (microseconds)
  unsigned long duration = pulseIn(echo_pin_, HIGH, timeout_us_);
  if (duration == 0) {
    last_distance_cm_ = -1; // timeout/no object
    return -1;
  }

  // Convert duration to cm: speed of sound ~343 m/s -> 29.1 us/cm (round trip)/2
  int distance_cm = static_cast<int>(duration / 58UL);
  last_distance_cm_ = distance_cm;
  return distance_cm;
}

void Ultrasonic::printStatus() {
  int d = getRawValue();
  Serial.print("Ultrasonic (TRIG ");
  Serial.print(trig_pin_);
  Serial.print(" ECHO ");
  Serial.print(echo_pin_);
  Serial.print("): ");
  if (d < 0) {
    Serial.println("No echo / out of range");
  } else {
    Serial.print(d);
    Serial.println(" cm");
  }
}
