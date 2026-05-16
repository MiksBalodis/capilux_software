#include <Arduino.h>
#include <Wire.h>

#include "pins.h"
#include "config.h"
#include "board_io.h"
#include "i2c_utils.h"
#include "sd_logger.h"
#include "telemetry_format.h"

#include "sensors/lps27hhwt.h"
#include "sensors/ms5611.h"
#include "sensors/lsm6dso32.h"

// Wire  = I2C1 on GP4/GP5: chamber 1/2 sensors
// Wire1 = I2C2 on GP2/GP3: chamber 3/4 sensors
Lps27hhwt chamberSensors[4] = {
  Lps27hhwt(Wire,  ADDR_LPS27_LOW,  "CH1_LPS27"),
  Lps27hhwt(Wire,  ADDR_LPS27_HIGH, "CH2_LPS27"),
  Lps27hhwt(Wire1, ADDR_LPS27_LOW,  "CH3_LPS27"),
  Lps27hhwt(Wire1, ADDR_LPS27_HIGH, "CH4_LPS27"),
};

Ms5611 ms5611(Wire, ADDR_MS5611, "MS5611");
Lsm6dso32 imu(Wire, ADDR_LSM6DSO32, "LSM6DSO32");

static unsigned long lastSensorReadMs = 0;

static void printDeviceStatus() {
  Serial.println();
  Serial.println(F("=== I2C device scan ==="));
  scanI2CBus(Wire, "Wire / I2C1 / GP4-GP5", Serial);
  scanI2CBus(Wire1, "Wire1 / I2C2 / GP2-GP3", Serial);
  Serial.println();
}

static void initI2C() {
  Wire.setSDA(PIN_I2C1_SDA);
  Wire.setSCL(PIN_I2C1_SCL);
  Wire.begin();
  Wire.setClock(400000);

  Wire1.setSDA(PIN_I2C2_SDA);
  Wire1.setSCL(PIN_I2C2_SCL);
  Wire1.begin();
  Wire1.setClock(400000);
}

static void initSensors() {
  Serial.println(F("Initializing sensors..."));

  for (int i = 0; i < 4; ++i) {
    bool ok = chamberSensors[i].begin();
    Serial.print(F("  "));
    Serial.print(chamberSensors[i].name());
    Serial.print(F(" at 0x"));
    Serial.print(chamberSensors[i].address(), HEX);
    Serial.print(F(": "));
    Serial.println(ok ? F("OK") : F("NOT FOUND"));
  }

  Serial.print(F("  MS5611: "));
  Serial.println(ms5611.begin() ? F("OK") : F("NOT FOUND"));

  Serial.print(F("  LSM6DSO32: "));
  Serial.println(imu.begin() ? F("OK") : F("NOT FOUND"));
}

static void readAndPrintSensors() {
  Lps27Reading lps[4];
  for (int i = 0; i < 4; ++i) {
    lps[i] = chamberSensors[i].read();
  }

  Ms5611Reading ms = ms5611.read();
  ImuReading imuReading = imu.read();

  bool soe = readSOE();
  bool lo = readLO();

  String csv = makeCsvLine(millis(), soe, lo, lps, ms, imuReading);

  Serial.println(csv);

  if (ENABLE_SD_LOGGING) {
    appendSensorCsvLine(csv);
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1500);

  Serial.println();
  Serial.println(F("CAPILUX sensor bring-up"));
  Serial.println(F("Arduino framework / PlatformIO / RP2040"));
  Serial.println();

  initBoardPins();
  setCameraPower(false);
  setAllLeds(0);

  initI2C();
  printDeviceStatus();
  initSensors();

  Serial.print(F("Initializing SD logger: "));
  bool sdOk = ENABLE_SD_LOGGING ? initSdLogger() : false;
  if (ENABLE_SD_LOGGING) {
    Serial.println(sdOk ? F("OK") : F("FAILED"));
  } else {
    Serial.println(F("disabled"));
  }

  Serial.println();
  Serial.println(F("CSV telemetry header:"));
  printCsvHeader(Serial);
  Serial.println(F("Starting sensor loop..."));
}

void loop() {
  const unsigned long now = millis();

  if (now - lastSensorReadMs >= SENSOR_PERIOD_MS) {
    lastSensorReadMs = now;
    readAndPrintSensors();
  }
}
