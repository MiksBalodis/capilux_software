#include "sd_logger.h"
#include <SPI.h>
#include <SD.h>
#include "pins.h"

static constexpr const char *LOG_FILE = "/sensor_log.csv";
static bool sdReady = false;

String buildCsvHeader() {
  return F("time_ms,soe,lo,"
           "lps_ch1_ok,lps_ch1_hpa,lps_ch1_c,"
           "lps_ch2_ok,lps_ch2_hpa,lps_ch2_c,"
           "lps_ch3_ok,lps_ch3_hpa,lps_ch3_c,"
           "lps_ch4_ok,lps_ch4_hpa,lps_ch4_c,"
           "ms_ok,ms_hpa,ms_c,"
           "imu_ok,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,imu_c");
}

void printCsvHeader(Stream &out) {
  out.println(buildCsvHeader());
}

bool initSdLogger() {
  SPI.setRX(PIN_SD_MISO);
  SPI.setTX(PIN_SD_MOSI);
  SPI.setSCK(PIN_SD_CLK);
  SPI.begin();

  sdReady = SD.begin(PIN_SD_CS);
  if (!sdReady) {
    return false;
  }

  if (!SD.exists(LOG_FILE)) {
    File f = SD.open(LOG_FILE, FILE_WRITE);
    if (!f) {
      sdReady = false;
      return false;
    }
    f.println(buildCsvHeader());
    f.close();
  }

  return true;
}

bool appendSensorCsvLine(const String &line) {
  if (!sdReady) {
    return false;
  }

  File f = SD.open(LOG_FILE, FILE_WRITE);
  if (!f) {
    sdReady = false;
    return false;
  }

  f.println(line);
  f.close();
  return true;
}
