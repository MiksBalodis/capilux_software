#pragma once
#include <Arduino.h>

bool initSdLogger();
bool appendSensorCsvLine(const String &line);
void printCsvHeader(Stream &out);
String buildCsvHeader();
