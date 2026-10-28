#include "gt911.h"
#include <Wire.h>

static const uint16_t REG_STATUS = 0x814E;
static const uint16_t REG_POINT1 = 0x8150;

bool GT911::begin(int sda, int scl, int intPin, int rstPin) {
  // Reset with INT held low selects I2C address 0x5D
  pinMode(rstPin, OUTPUT);
  pinMode(intPin, OUTPUT);
  digitalWrite(intPin, LOW);
  digitalWrite(rstPin, LOW);
  delay(11);
  digitalWrite(rstPin, HIGH);
  delay(6);
  pinMode(intPin, INPUT);
  delay(60);

  Wire.begin(sda, scl, 400000);

  const uint8_t candidates[2] = {0x5D, 0x14};
  for (uint8_t a : candidates) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      _addr = a;
      _ok = true;
      return true;
    }
  }
  _ok = false;
  return false;
}

bool GT911::readReg(uint16_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(_addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  size_t got = Wire.requestFrom((int)_addr, (int)len);
  if (got != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool GT911::writeReg(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(_addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool GT911::read(uint16_t &x, uint16_t &y) {
  if (!_ok) return false;
  uint8_t st = 0;
  if (!readReg(REG_STATUS, &st, 1)) return false;

  if (st & 0x80) {                     // fresh data in buffer
    uint8_t n = st & 0x0F;
    if (n > 0 && n <= 5) {
      uint8_t p[4];
      if (readReg(REG_POINT1, p, 4)) {
        _x = p[0] | (p[1] << 8);
        _y = p[2] | (p[3] << 8);
        _down = true;
      }
    } else {
      _down = false;
    }
    writeReg(REG_STATUS, 0);           // acknowledge
  }
  x = _x;
  y = _y;
  return _down;
}
