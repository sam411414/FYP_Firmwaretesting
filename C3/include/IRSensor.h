#ifndef IR_SENSOR_H
#define IR_SENSOR_H

#include <Arduino.h>

// IR sensor operating mode
enum IRMode : uint8_t {
  kIRDigital = 0,
  kIRAnalog  = 1
};

// Control packet received over ESP-NOW (IR-specific)
struct IRControlPacket {
  uint8_t type;   // Must be kPacketIRControl (4)
  uint8_t mode;   // 0=digital, 1=analog
};

// Status packet sent from C3 to S3 (IR-specific)
struct IRStatusPacket {
  uint8_t type;            // Must be kPacketIRStatus (5)
  uint8_t mode;            // 0=digital, 1=analog
  uint8_t digital_state;   // 0=clear, 1=obstacle
  uint8_t analog_high;     // analog value >> 8
  uint8_t analog_low;      // analog value & 0xFF
};

class IRSensor {
public:
  IRSensor(uint8_t digital_pin, uint8_t analog_pin);

  void init();
  void update();  // Read sensor and flag state change

  // Handle an incoming IRControlPacket
  void handleControl(const IRControlPacket &cmd);

  // Getters
  IRMode getMode() const { return mode_; }
  bool isObstacle() const { return obstacle_; }
  int getAnalogValue() const { return analog_value_; }

  // Check and clear state change flag
  bool checkAndClearStateChanged() {
    bool changed = state_changed_;
    state_changed_ = false;
    return changed;
  }

private:
  uint8_t digital_pin_;
  uint8_t analog_pin_;
  IRMode mode_;
  bool obstacle_;
  int analog_value_;
  bool state_changed_;
  
  // Digital debounce
  bool raw_digital_;           // Last raw digital read
  unsigned long debounce_time_; // When the raw value last changed
  static constexpr unsigned long kDebounceMs = 50;

  // Mode switching helpers
  void activateDigitalMode();
  void activateAnalogMode();
};

#endif // IR_SENSOR_H
