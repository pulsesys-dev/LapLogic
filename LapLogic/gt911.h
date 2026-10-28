// Minimal GT911 capacitive touch driver (single touch point)
#pragma once
#include <Arduino.h>

class GT911 {
public:
  bool begin(int sda, int scl, int intPin, int rstPin);
  // Returns true while a finger is down; x/y are raw panel coordinates.
  bool read(uint16_t &x, uint16_t &y);
  bool ok() const { return _ok; }

private:
  bool readReg(uint16_t reg, uint8_t *buf, size_t len);
  bool writeReg(uint16_t reg, uint8_t val);
  uint8_t  _addr = 0x5D;
  bool     _ok = false;
  bool     _down = false;
  uint16_t _x = 0, _y = 0;
};
