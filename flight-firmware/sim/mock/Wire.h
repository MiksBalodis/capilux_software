#pragma once
#include <Arduino.h>
// All I2C devices absent (NACK) unless sim/hw.cpp says otherwise.
class TwoWire {
 public:
  int bus;
  explicit TwoWire(int b) : bus(b) {}
  void setSDA(int) {}
  void setSCL(int) {}
  void setClock(uint32_t) {}
  void setTimeout(int) {}
  void begin() {}
  void end() {}
  void beginTransmission(uint8_t a);
  size_t write(uint8_t v);
  uint8_t endTransmission(bool stop = true);
  size_t requestFrom(uint8_t a, size_t n);
  int read();
};
extern TwoWire Wire, Wire1;
