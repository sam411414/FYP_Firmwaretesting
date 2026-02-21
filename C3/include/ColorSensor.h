#ifndef COLOR_SENSOR_H
#define COLOR_SENSOR_H

#include <Arduino.h>
#include <Adafruit_TCS34725.h>

// Color sensor output mode
enum ColorMode : uint8_t {
  kColorRGB    = 0,  // R/G/B only
  kColorLux    = 1,  // Lux only
  kColorRGBLux = 2   // Both
};

// Color sensor commands (received from S3)
enum ColorCommand : uint8_t {
  kColorCmdRGB    = 0,  // Switch to RGB mode
  kColorCmdLux    = 1,  // Switch to Lux mode
  kColorCmdRGBLux = 2,  // Switch to RGB+Lux mode
  kColorCmdLedOn  = 3,  // Turn onboard LED on
  kColorCmdLedOff = 4   // Turn onboard LED off
};

// Control packet received over ESP-NOW (color-specific)
struct ColorControlPacket {
  uint8_t type;      // Must be kPacketColorControl (7)
  uint8_t command;   // ColorCommand value
};

// Status packet sent from C3 to S3 (color-specific)
struct ColorStatusPacket {
  uint8_t type;       // Must be kPacketColorStatus (6)
  uint8_t mode;       // ColorMode: 0=RGB, 1=Lux, 2=RGB+Lux
  uint8_t r;          // IR-compensated red, normalized 0-255
  uint8_t g;          // IR-compensated green, normalized 0-255
  uint8_t b;          // IR-compensated blue, normalized 0-255
  uint8_t lux_high;   // Lux >> 8
  uint8_t lux_low;    // Lux & 0xFF
};

class ColorSensor {
public:
  // sda_pin / scl_pin: I2C pins. led_pin: onboard LED control (LOW = off).
  ColorSensor(uint8_t sda_pin, uint8_t scl_pin, uint8_t led_pin);

  // Initialize I2C, TCS34725, and LED pin. Returns false if sensor not found.
  bool init();

  // Read sensor with autorange. Non-blocking: only samples when the
  // integration window has elapsed. Sets state-changed flag on significant
  // changes to R/G/B or lux.
  void update();

  // Handle an incoming ColorControlPacket (mode switch or LED control)
  void handleControl(const ColorControlPacket &cmd);

  // Getters — IR-compensated, normalized 0-255
  uint8_t   getRed()       const { return r_; }
  uint8_t   getGreen()     const { return g_; }
  uint8_t   getBlue()      const { return b_; }
  uint16_t  getLux()       const { return lux_; }
  ColorMode getMode()      const { return mode_; }
  bool      isSaturated()  const { return saturated_; }
  bool      isReady()      const { return ready_; }

  // Returns true once per state change, then resets the flag
  bool checkAndClearStateChanged() {
    bool changed = state_changed_;
    state_changed_ = false;
    return changed;
  }

private:
  uint8_t sda_pin_;
  uint8_t scl_pin_;
  uint8_t led_pin_;
  Adafruit_TCS34725 tcs_;
  bool ready_;
  bool state_changed_;
  bool saturated_;

  ColorMode mode_;
  uint8_t  r_, g_, b_;
  uint16_t lux_;

  unsigned long last_read_time_;

  // ── Autorange table (dim → bright) ────────────────────────────────
  struct AgcEntry {
    tcs34725Gain_t gain;
    uint8_t        integration_time;  // TCS34725_INTEGRATIONTIME_* constant
    uint16_t       min_count;         // 0 = start of list
    uint16_t       max_count;         // 0 = end of list
  };
  static const AgcEntry kAgcTable[];
  static constexpr int  kAgcTableSize = 5;
  int agc_index_;

  void applyGainTime();
  uint16_t getIntegrationMs() const;
  uint16_t getGainMultiplier() const;

  // DN40 application note coefficients
  static constexpr float kRCoef  = 0.136f;
  static constexpr float kGCoef  = 1.000f;
  static constexpr float kBCoef  = -0.444f;
  static constexpr float kGA     = 1.0f;
  static constexpr float kDF     = 310.0f;

  // Change thresholds
  static constexpr int kChangeThreshold = 8;   // RGB channel (0-255)
  static constexpr int kLuxThreshold    = 10;
};

#endif // COLOR_SENSOR_H
