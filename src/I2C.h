#pragma once

#include <Wire.h>

class I2C {
public:
  I2C(int sda, int scl) { Wire.begin(sda, scl); }
  void writeRegister(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr >> 1); // device expects 7-bit
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
  }
};
