#include "lps27hhwt.h"
#include "i2c_utils.h"

// LPS27HHW register map
static constexpr uint8_t REG_WHO_AM_I   = 0x0F;
static constexpr uint8_t REG_CTRL_REG2  = 0x11;
static constexpr uint8_t REG_STATUS     = 0x27;
static constexpr uint8_t REG_PRESS_OUT_XL = 0x28;
static constexpr uint8_t REG_TEMP_OUT_L   = 0x2B;

static constexpr uint8_t EXPECTED_WHO_AM_I = 0xB3;

Lps27hhwt::Lps27hhwt(TwoWire &bus, uint8_t address, const char *name)
  : _bus(bus), _address(address), _name(name) {}

bool Lps27hhwt::begin() {
  uint8_t who = 0;
  if (!i2cReadRegister8(_bus, _address, REG_WHO_AM_I, who)) {
    _available = false;
    return false;
  }

  _available = (who == EXPECTED_WHO_AM_I);
  return _available;
}

bool Lps27hhwt::triggerOneShot() {
  // CTRL_REG2 bit 0 = ONE_SHOT.
  uint8_t ctrl2 = 0;
  if (!i2cReadRegister8(_bus, _address, REG_CTRL_REG2, ctrl2)) {
    return false;
  }
  ctrl2 |= 0x01;
  return i2cWriteRegister8(_bus, _address, REG_CTRL_REG2, ctrl2);
}

Lps27Reading Lps27hhwt::read() {
  Lps27Reading r;

  if (!_available && !begin()) {
    return r;
  }

  if (!triggerOneShot()) {
    _available = false;
    return r;
  }

  // Wait for pressure and temperature data-ready bits.
  // STATUS bit 0: pressure available, bit 1: temperature available.
  bool ready = false;
  for (int i = 0; i < 20; ++i) {
    uint8_t status = 0;
    if (i2cReadRegister8(_bus, _address, REG_STATUS, status)) {
      if ((status & 0x03) == 0x03) {
        ready = true;
        break;
      }
    }
    delay(5);
  }

  if (!ready) {
    return r;
  }

  uint8_t pbuf[3] = {0};
  if (!i2cReadRegisters(_bus, _address, REG_PRESS_OUT_XL, pbuf, 3)) {
    return r;
  }

  int32_t rawPressure = (int32_t)((uint32_t)pbuf[2] << 16 | (uint32_t)pbuf[1] << 8 | pbuf[0]);
  if (rawPressure & 0x00800000) {
    rawPressure |= 0xFF000000; // sign extend 24-bit
  }

  uint8_t tbuf[2] = {0};
  if (!i2cReadRegisters(_bus, _address, REG_TEMP_OUT_L, tbuf, 2)) {
    return r;
  }

  int16_t rawTemp = (int16_t)((uint16_t)tbuf[1] << 8 | tbuf[0]);

  r.pressure_hPa = rawPressure / 4096.0f;

  // LPS27/LPS22-family convention: 100 LSB/degC, 0 LSB = 0 degC.
  // Verify this against your exact LPS27HHWT datasheet revision during calibration.
  r.temperature_C = rawTemp / 100.0f;

  r.ok = true;
  return r;
}
