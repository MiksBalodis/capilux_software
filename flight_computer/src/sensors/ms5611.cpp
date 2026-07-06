#include "ms5611.h"

static constexpr uint8_t CMD_RESET = 0x1E;
static constexpr uint8_t CMD_ADC_READ = 0x00;
static constexpr uint8_t CMD_CONVERT_D1_OSR4096 = 0x48; // pressure
static constexpr uint8_t CMD_CONVERT_D2_OSR4096 = 0x58; // temperature
static constexpr uint8_t CMD_PROM_READ_BASE = 0xA0;

Ms5611::Ms5611(TwoWire &bus, uint8_t address, const char *name)
  : _bus(bus), _address(address), _name(name) {}

bool Ms5611::sendCommand(uint8_t cmd) {
  _bus.beginTransmission(_address);
  _bus.write(cmd);
  return _bus.endTransmission() == 0;
}

bool Ms5611::begin() {
  if (!sendCommand(CMD_RESET)) {
    _available = false;
    return false;
  }

  delay(5);

  _available = readProm();
  return _available;
}

bool Ms5611::readProm() {
  for (uint8_t i = 0; i < 7; ++i) {
    _bus.beginTransmission(_address);
    _bus.write(CMD_PROM_READ_BASE + i * 2);
    if (_bus.endTransmission(false) != 0) {
      return false;
    }

    if (_bus.requestFrom((int)_address, 2) != 2) {
      return false;
    }

    C[i] = ((uint16_t)_bus.read() << 8) | _bus.read();
  }

  // C[0] contains factory data/CRC; C[1..6] must not be all zero.
  return C[1] != 0 && C[2] != 0 && C[3] != 0 && C[4] != 0 && C[5] != 0 && C[6] != 0;
}

bool Ms5611::readAdc(uint8_t conversionCommand, uint32_t &value) {
  if (!sendCommand(conversionCommand)) {
    return false;
  }

  delay(10); // OSR4096 conversion max is around 9 ms

  _bus.beginTransmission(_address);
  _bus.write(CMD_ADC_READ);
  if (_bus.endTransmission(false) != 0) {
    return false;
  }

  if (_bus.requestFrom((int)_address, 3) != 3) {
    return false;
  }

  value = ((uint32_t)_bus.read() << 16) | ((uint32_t)_bus.read() << 8) | _bus.read();
  return value != 0;
}

Ms5611Reading Ms5611::read() {
  Ms5611Reading r;

  if (!_available && !begin()) {
    return r;
  }

  uint32_t D1 = 0;
  uint32_t D2 = 0;

  if (!readAdc(CMD_CONVERT_D1_OSR4096, D1)) {
    _available = false;
    return r;
  }

  if (!readAdc(CMD_CONVERT_D2_OSR4096, D2)) {
    _available = false;
    return r;
  }

  int64_t dT = (int64_t)D2 - ((int64_t)C[5] << 8);
  int64_t TEMP = 2000 + ((dT * C[6]) >> 23);

  int64_t OFF  = ((int64_t)C[2] << 16) + ((dT * C[4]) >> 7);
  int64_t SENS = ((int64_t)C[1] << 15) + ((dT * C[3]) >> 8);

  // First-order compensated pressure in 0.01 mbar.
  int64_t P = ((((int64_t)D1 * SENS) >> 21) - OFF) >> 15;

  r.temperature_C = TEMP / 100.0f;
  r.pressure_hPa = P / 100.0f;
  r.ok = true;
  return r;
}
