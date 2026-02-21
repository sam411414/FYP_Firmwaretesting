#include <Arduino.h>
#include <WiFi.h>
#include "motorControl.h"
#include "ESPNOW_C3.h"
#include "ColorSensor.h"

//MotorController motor;
ColorSensor color_sensor(5, 6, 4); // SDA: GPIO5, SCL: GPIO6, LED: GPIO4

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

  color_sensor.init();
  espnow_register_color(&color_sensor);
}

void loop() {
  //motor.update();
  color_sensor.update();
  espnow_update();

  delay(10);
}