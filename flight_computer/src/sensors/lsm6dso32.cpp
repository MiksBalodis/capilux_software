#include "lsm6dso32.h"
#include "i2c_utils.h"

static constexpr uint8_t REG_WHO_AM_I = 0x0F;
static constexpr uint8_t REG_CTRL1_XL = 0x10;
static constexpr uint8_t REG_CTRL2_G  = 0x11;
static constexpr uint8_t REG_CTRL3_C  = 0x12;
static constexpr uint8_t REG_OUT_TEMP_L = 0x20;
static constexpr uint8_t EXPECTED_WHO_AM_I = 0x6C;

Lsm6dso32::Lsm6dso32(TwoWire &bus, uint8_t address, const char *name)
  : _bus(bus), _address(address), _name(name) {}

bool Lsm6dso32::begin() {
  uint8_t who = 0;
  if (!i2cReadRegister8(_bus, _address, REG_WHO_AM_I, who)) {
    _available = false;
    return false;
  }

  if (who != EXPECTED_WHO_AM_I) {
    _available = false;
    return false;
  }

  // CTRL3_C: BDU=1 so high/low bytes are consistent.
  if (!i2cWriteRegister8(_bus, _address, REG_CTRL3_C, 0x44)) {
    _available = false;
    return false;
  }

  // Accelerometer: ODR 104 Hz, FS ±32g.
  // For LSM6DSO32: FS_XL bits select ±32g. Verify exact bitfield in datasheet.
  if (!i2cWriteRegister8(_bus, _address, REG_CTRL1_XL, 0x4C)) {
    _available = false;
    return false;
  }

  // Gyroscope: ODR 104 Hz, FS 2000 dps.
  if (!i2cWriteRegister8(_bus, _address, REG_CTRL2_G, 0x4C)) {
    _available = false;
    return false;
  }

  delay(50);
  _available = true;
  return true;
}

ImuReading Lsm6dso32::read() {
  ImuReading r;

  if (!_available && !begin()) {
    return r;
  }

  uint8_t buf[14] = {0};
  if (!i2cReadRegisters(_bus, _address, REG_OUT_TEMP_L, buf, 14)) {
    _available = false;
    return r;
  }

  int16_t rawTemp = (int16_t)((uint16_t)buf[1] << 8 | buf[0]);

  int16_t rawGx = (int16_t)((uint16_t)buf[3] << 8 | buf[2]);
  int16_t rawGy = (int16_t)((uint16_t)buf[5] << 8 | buf[4]);
  int16_t rawGz = (int16_t)((uint16_t)buf[7] << 8 | buf[6]);

  int16_t rawAx = (int16_t)((uint16_t)buf[9] << 8 | buf[8]);
  int16_t rawAy = (int16_t)((uint16_t)buf[11] << 8 | buf[10]);
  int16_t rawAz = (int16_t)((uint16_t)buf[13] << 8 | buf[12]);

  // Datasheet scale factors:
  // ±32 g: approximately 0.976 mg/LSB.
  // ±2000 dps: approximately 70 mdps/LSB.
  constexpr float ACCEL_G_PER_LSB = 0.000976f;
  constexpr float GYRO_DPS_PER_LSB = 0.070f;

  r.temperature_C = 25.0f + rawTemp / 256.0f;
  r.gx_dps = rawGx * GYRO_DPS_PER_LSB;
  r.gy_dps = rawGy * GYRO_DPS_PER_LSB;
  r.gz_dps = rawGz * GYRO_DPS_PER_LSB;

  r.ax_g = rawAx * ACCEL_G_PER_LSB;
  r.ay_g = rawAy * ACCEL_G_PER_LSB;
  r.az_g = rawAz * ACCEL_G_PER_LSB;

  r.ok = true;
  return r;
}
