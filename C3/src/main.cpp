#include <Arduino.h>
#include <WiFi.h>
#include "motorControl.h"
#include "ESPNOW_C3.h"
#include "IRSensor.h"

//MotorController motor;
IRSensor ir_sensor1(10, 3);  // digital: GPIO10, analog: GPIO3

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Init transport
  espnow_init();

  // Register subsystems independently
  //motor.initialize();
  //motor.setEnabled(false);
  //espnow_register_motor(&motor);

  ir_sensor1.init();
  espnow_register_ir(&ir_sensor1);
}

void loop() {
  //motor.update();
  ir_sensor1.update();
  espnow_update();

  delay(10);
}