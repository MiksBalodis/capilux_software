#pragma once
#include <Arduino.h>
#include "sensors/lps27hhwt.h"
#include "sensors/ms5611.h"
#include "sensors/lsm6dso32.h"

String makeCsvLine(
  unsigned long timeMs,
  bool soe,
  bool lo,
  const Lps27Reading lps[4],
  const Ms5611Reading &ms,
  const ImuReading &imu
);
