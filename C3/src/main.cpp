// ═══════════════════════════════════════════════════════════════════
// Color Sensor Test Harness — rename to main.cpp to use
// Tests: ColorSensor (TCS34725) via ESP-NOW from S3
// ═══════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <WiFi.h>
#include "ColorSensor.h"
#include "ESPNOW_C3.h"

ColorSensor color_sensor(5, 6, 4);  // SDA: GPIO5, SCL: GPIO6, LED: GPIO4

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println("[C3] Color sensor test harness");
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  // Init transport
  espnow_init();

  // Color sensor
  color_sensor.init();
  espnow_register_color(&color_sensor);

  Serial.println("[C3] Color sensor registered, waiting for S3 commands...");
}

void loop() {
  color_sensor.update();
  espnow_update();
  delay(10);
}
