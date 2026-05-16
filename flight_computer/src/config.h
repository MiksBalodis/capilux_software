#pragma once

#include <Arduino.h>
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif

constexpr unsigned long SENSOR_PERIOD_MS = 1000;

// Set to 1 if you want to write /sensor_log.csv to SD card.
// Keep enabled for bring-up. Disable if SD wiring is not ready yet.
constexpr bool ENABLE_SD_LOGGING = true;

// I2C addresses
constexpr uint8_t ADDR_LPS27_LOW  = 0x5C;
constexpr uint8_t ADDR_LPS27_HIGH = 0x5D;
constexpr uint8_t ADDR_MS5611     = 0x77;
constexpr uint8_t ADDR_LSM6DSO32  = 0x6A;

// If your IMU uses the alternative address, change to 0x6B.
