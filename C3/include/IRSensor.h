#ifndef IR_SENSOR_H
#define IR_SENSOR_H

#include <Arduino.h>

class IRSensor {
public:
  IRSensor(uint8_t pin);
  
  void init();
  bool isObstacle();
  int getRawValue();
  void printStatus();

private:
  uint8_t pin_;
};

#endif // IR_SENSOR_H
