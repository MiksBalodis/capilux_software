#pragma once
#include <Arduino.h>
#include <Wire.h>

struct Ms5611Reading {
  bool ok = false;
  float pressure_hPa = NAN;
  float temperature_C = NAN;
};

class Ms5611 {
public:
  Ms5611(TwoWire &bus, uint8_t address, const char *name);

  bool begin();
  Ms5611Reading read();
  const char *name() const { return _name; }
  bool available() const { return _available; }

private:
  TwoWire &_bus;
  uint8_t _address;
  const char *_name;
  bool _available = false;
  uint16_t C[7] = {0};

  bool sendCommand(uint8_t cmd);
  bool readProm();
  bool readAdc(uint8_t conversionCommand, uint32_t &value);
};
