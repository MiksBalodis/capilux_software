#include "telemetry_format.h"

static void addFloat(String &s, float value, uint8_t decimals) {
  if (isnan(value)) {
    s += "nan";
  } else {
    s += String(value, decimals);
  }
}

String makeCsvLine(
  unsigned long timeMs,
  bool soe,
  bool lo,
  const Lps27Reading lps[4],
  const Ms5611Reading &ms,
  const ImuReading &imu
) {
  String s;
  s.reserve(256);

  s += String(timeMs);
  s += ',';
  s += soe ? "1" : "0";
  s += ',';
  s += lo ? "1" : "0";

  for (int i = 0; i < 4; ++i) {
    s += ',';
    s += lps[i].ok ? "1" : "0";
    s += ',';
    addFloat(s, lps[i].pressure_hPa, 2);
    s += ',';
    addFloat(s, lps[i].temperature_C, 2);
  }

  s += ',';
  s += ms.ok ? "1" : "0";
  s += ',';
  addFloat(s, ms.pressure_hPa, 2);
  s += ',';
  addFloat(s, ms.temperature_C, 2);

  s += ',';
  s += imu.ok ? "1" : "0";
  s += ',';
  addFloat(s, imu.ax_g, 4);
  s += ',';
  addFloat(s, imu.ay_g, 4);
  s += ',';
  addFloat(s, imu.az_g, 4);
  s += ',';
  addFloat(s, imu.gx_dps, 2);
  s += ',';
  addFloat(s, imu.gy_dps, 2);
  s += ',';
  addFloat(s, imu.gz_dps, 2);
  s += ',';
  addFloat(s, imu.temperature_C, 2);

  return s;
}
