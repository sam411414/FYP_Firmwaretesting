#include <Arduino.h>
#include <WiFi.h>
#include "motorControl.h"
#include "ESPNOW.h"

MotorController motor;

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.print("C3 MAC Address: ");
  Serial.println(WiFi.macAddress());

  motor.initialize();
  motor.setEnabled(false);

  espnow_init(&motor);
}

void loop() {
  motor.update();  // Handle duty cycle ramping
  delay(10);
}
