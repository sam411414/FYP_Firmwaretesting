#include <Arduino.h>
#include <WiFi.h>
#include "motorControl.h"
<<<<<<< Updated upstream
#include "ESPNOW_C3.h"
#include "ColorSensor.h"

//MotorController motor;
ColorSensor color_sensor(5, 6, 4); // SDA: GPIO5, SCL: GPIO6, LED: GPIO4
=======
#include "ESPNOW.h"
#include "Ultrasonic.h"

//MotorController motor;
Ultrasonic ultrasonic(2, 3); // TRIG=2, ECHO=3
>>>>>>> Stashed changes

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

<<<<<<< Updated upstream
  color_sensor.init();
  espnow_register_color(&color_sensor);
}

void loop() {
  //motor.update();
  color_sensor.update();
  espnow_update();

=======
  ultrasonic.init();

 // espnow_init(&motor);
}

void loop() {
  // Separate delay timing for responsiveness
  static unsigned long last_read = 0;
  static unsigned long last_print = 0;
  const unsigned long read_interval = 50;    // Read every 50ms
  const unsigned long print_interval = 200;  // Print every 200ms
  
  unsigned long now = millis();
  
  //motor.update();     // Handle duty cycle ramping
  //espnow_update();    // Handle periodic status reporting
  
  // IR Sensor debugging with separate intervals
  if (now - last_read >= read_interval) {
    last_read = now;
    // Readings happen here implicitly when printStatus() is called
  }
  
  if (now - last_print >= print_interval) {
    ultrasonic.printStatus();
    last_print = now;
  }
  
>>>>>>> Stashed changes
  delay(10);
}