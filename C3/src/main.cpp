#include <Arduino.h>
#include <WiFi.h>
#include "motorControl.h"
#include "ESPNOW.h"
#include "IRSensor.h"

//MotorController motor;
IRSensor ir_sensor1(2);
IRSensor ir_sensor2(3);

void setup() {
  Serial.begin(115200);
  delay(500);

 // WiFi.mode(WIFI_STA);
  //WiFi.disconnect();

 // Serial.print("C3 MAC Address: ");
 // Serial.println(WiFi.macAddress());

 // motor.initialize();
//motor.setEnabled(false);

  ir_sensor1.init();
  ir_sensor2.init();

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
    //ir_sensor1.printStatus();
    ir_sensor2.printStatus();
    last_print = now;
  }
  
  delay(10);
}