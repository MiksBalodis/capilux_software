#include "i2c_utils.h"

bool i2cDevicePresent(TwoWire &bus, uint8_t address) {
  bus.beginTransmission(address);
  return bus.endTransmission() == 0;
}

bool i2cReadRegister8(TwoWire &bus, uint8_t address, uint8_t reg, uint8_t &value) {
  bus.beginTransmission(address);
  bus.write(reg);
  if (bus.endTransmission(false) != 0) {
    return false;
  }

  if (bus.requestFrom((int)address, 1) != 1) {
    return false;
  }

  value = bus.read();
  return true;
}

bool i2cWriteRegister8(TwoWire &bus, uint8_t address, uint8_t reg, uint8_t value) {
  bus.beginTransmission(address);
  bus.write(reg);
  bus.write(value);
  return bus.endTransmission() == 0;
}

bool i2cReadRegisters(TwoWire &bus, uint8_t address, uint8_t startReg, uint8_t *buffer, size_t length) {
  bus.beginTransmission(address);
  bus.write(startReg);
  if (bus.endTransmission(false) != 0) {
    return false;
  }

  size_t received = bus.requestFrom((int)address, (int)length);
  if (received != length) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    buffer[i] = bus.read();
  }

  return true;
}

void scanI2CBus(TwoWire &bus, const char *name, Stream &out) {
  out.print("I2C scan ");
  out.print(name);
  out.println(":");

  bool foundAny = false;
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    if (i2cDevicePresent(bus, addr)) {
      foundAny = true;
      out.print("  found 0x");
      if (addr < 0x10) out.print('0');
      out.println(addr, HEX);
    }
  }

  if (!foundAny) {
    out.println("  no devices found");
  }
}
