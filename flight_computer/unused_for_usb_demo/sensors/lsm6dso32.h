#pragma once
#include <Arduino.h>
#include <Wire.h>

struct ImuReading {
  bool ok = false;
  float ax_g = NAN;
  float ay_g = NAN;
  float az_g = NAN;
  float gx_dps = NAN;
  float gy_dps = NAN;
  float gz_dps = NAN;
  float temperature_C = NAN;
};

class Lsm6dso32 {
public:
  Lsm6dso32(TwoWire &bus, uint8_t address, const char *name);

  bool begin();
  ImuReading read();
  const char *name() const { return _name; }
  bool available() const { return _available; }

private:
  TwoWire &_bus;
  uint8_t _address;
  const char *_name;
  bool _available = false;
};
