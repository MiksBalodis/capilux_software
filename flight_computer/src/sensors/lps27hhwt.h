#pragma once
#include <Arduino.h>
#include <Wire.h>

struct Lps27Reading {
  bool ok = false;
  float pressure_hPa = NAN;
  float temperature_C = NAN;
};

class Lps27hhwt {
public:
  Lps27hhwt(TwoWire &bus, uint8_t address, const char *name);

  bool begin();
  Lps27Reading read();
  const char *name() const { return _name; }
  uint8_t address() const { return _address; }
  bool available() const { return _available; }

private:
  TwoWire &_bus;
  uint8_t _address;
  const char *_name;
  bool _available = false;

  bool triggerOneShot();
};
