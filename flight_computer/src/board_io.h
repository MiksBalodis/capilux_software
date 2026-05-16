#pragma once
#include <Arduino.h>

void initBoardPins();
void setAllLeds(uint8_t pwmValue);
void setCameraPower(bool enabled);
bool readSOE();
bool readLO();
void writeShiftRegister(uint8_t data);
void initShiftRegister();
