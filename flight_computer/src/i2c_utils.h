#pragma once
#include <Arduino.h>
#include <Wire.h>

bool i2cDevicePresent(TwoWire &bus, uint8_t address);
bool i2cReadRegister8(TwoWire &bus, uint8_t address, uint8_t reg, uint8_t &value);
bool i2cWriteRegister8(TwoWire &bus, uint8_t address, uint8_t reg, uint8_t value);
bool i2cReadRegisters(TwoWire &bus, uint8_t address, uint8_t startReg, uint8_t *buffer, size_t length);
void scanI2CBus(TwoWire &bus, const char *name, Stream &out);
