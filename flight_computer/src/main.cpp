#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>

#include "i2c_utils.h"
#include "sensors/lps27hhwt.h"
#include "sensors/ms5611.h"
#include "sensors/lsm6dso32.h"

#if defined(ARDUINO_ARCH_RP2040)
  #include "hardware/watchdog.h"
#endif

// ============================================================
// CAPILUX command/telemetry firmware
// Current bench transport: USB CDC Serial
// Future transport: Serial1 via RS-422
// ============================================================

// ---------- Pin mapping ----------

constexpr int PIN_LED_EN1 = 7;
constexpr int PIN_LED_EN2 = 11;
constexpr int PIN_LED_EN3 = 12;
constexpr int PIN_LED_EN4 = 13;

constexpr int PIN_CAM_EN = 10;

constexpr int PIN_SOE_ISO = 14;
constexpr int PIN_LO_ISO  = 15;

// SD card SPI pins
constexpr int PIN_SD_MISO = 16;
constexpr int PIN_SD_CS   = 17;
constexpr int PIN_SD_CLK  = 18;
constexpr int PIN_SD_MOSI = 19;

// Shift register pins
constexpr int PIN_SRCLK = 20;
constexpr int PIN_RCLK  = 21;
constexpr int PIN_OE    = 22;
constexpr int PIN_SER   = 26;

// I2C buses (SED pinout)
constexpr int PIN_I2C2_SDA = 2;  // chamber 3/4 LPS27HHWT
constexpr int PIN_I2C2_SCL = 3;
constexpr int PIN_I2C1_SDA = 4;  // chamber 1/2 LPS27HHWT + onboard MS5611/LSM6DSO32
constexpr int PIN_I2C1_SCL = 5;

// I2C addresses
constexpr uint8_t ADDR_LPS27_LOW  = 0x5C;
constexpr uint8_t ADDR_LPS27_HIGH = 0x5D;
constexpr uint8_t ADDR_MS5611     = 0x77;
constexpr uint8_t ADDR_LSM6DSO32  = 0x6B;

// ---------- Communication ----------

constexpr uint32_t LINK_BAUD = 115200;
constexpr uint32_t STATUS_PERIOD_MS = 1000;
constexpr uint32_t HK_PERIOD_MS = 1000;
constexpr uint32_t LOG_PERIOD_MS = 1000;
constexpr uint32_t MAX_COMMAND_LENGTH = 96;

// ---------- SD paths ----------

const char *DATA_DIR = "/CAPILUX";
const char *LOG_FILE_PATH = "/CAPILUX/REC.CSV";
const char *SAVE_MARKER_PATH = "/CAPILUX/SAVED.TXT";

// ---------- State ----------

enum class SystemState {
  BOOT,
  STANDBY,
  TEST_MODE,
  SAFE_MODE,
  STREAMING
};

SystemState systemState = SystemState::BOOT;

String commandBuffer;

uint32_t bootTimeMs = 0;
uint32_t lastStatusMs = 0;
uint32_t lastHkMs = 0;
uint32_t lastLogMs = 0;

uint32_t commandsReceived = 0;
uint32_t badCommands = 0;

bool statusStreamEnabled = true;
bool hkStreamEnabled = true;

bool cameraPower = false;

uint8_t ledPwm[4] = {0, 0, 0, 0};

// SD/logger state
bool sdMounted = false;
bool loggerOk = false;
bool memorySaved = false;
uint32_t logLineCounter = 0;
uint32_t lastSaveMs = 0;
File logFile;

// ---------- Sensors ----------
// Wire  = RP2040 I2C0 on GP4/GP5 = SED bus "I2C1" (chambers 1/2 + onboard)
// Wire1 = RP2040 I2C1 on GP2/GP3 = SED bus "I2C2" (chambers 3/4)
Lps27hhwt lpsCh1(Wire,  ADDR_LPS27_LOW,  "CH1");
Lps27hhwt lpsCh2(Wire,  ADDR_LPS27_HIGH, "CH2");
Lps27hhwt lpsCh3(Wire1, ADDR_LPS27_LOW,  "CH3");
Lps27hhwt lpsCh4(Wire1, ADDR_LPS27_HIGH, "CH4");
Ms5611    baro(Wire,  ADDR_MS5611,    "BARO");
Lsm6dso32 imu(Wire,   ADDR_LSM6DSO32, "IMU");

constexpr uint32_t SENSOR_SAMPLE_PERIOD_MS = 1000;
uint32_t lastSensorMs = 0;

// Latest cached readings, refreshed once per sample period
Lps27Reading rCh1, rCh2, rCh3, rCh4;
Ms5611Reading rBaro;
ImuReading rImu;

int sensorsOkCount() {
  return (rCh1.ok ? 1 : 0) + (rCh2.ok ? 1 : 0) + (rCh3.ok ? 1 : 0) +
         (rCh4.ok ? 1 : 0) + (rBaro.ok ? 1 : 0) + (rImu.ok ? 1 : 0);
}

void initSensors() {
  Wire.setSDA(PIN_I2C1_SDA);
  Wire.setSCL(PIN_I2C1_SCL);
  Wire.setClock(100000);
  Wire.begin();

  Wire1.setSDA(PIN_I2C2_SDA);
  Wire1.setSCL(PIN_I2C2_SCL);
  Wire1.setClock(100000);
  Wire1.begin();

  lpsCh1.begin();
  lpsCh2.begin();
  lpsCh3.begin();
  lpsCh4.begin();
  baro.begin();
  imu.begin();
}

void sampleSensors() {
  // Drivers re-try begin() internally if a sensor was missing,
  // so disconnected sensors recover automatically when reattached.
  rCh1 = lpsCh1.read();
  rCh2 = lpsCh2.read();
  rCh3 = lpsCh3.read();
  rCh4 = lpsCh4.read();
  rBaro = baro.read();
  rImu = imu.read();
}

// Print a float JSON value or null when invalid
void printJsonFloat(const char *key, float v, bool ok, int digits = 2) {
  Serial.print(",\"");
  Serial.print(key);
  Serial.print("\":");
  if (ok && !isnan(v)) Serial.print(v, digits); else Serial.print("null");
}

// ---------- Basic helpers ----------

const char* stateName(SystemState s) {
  switch (s) {
    case SystemState::BOOT: return "BOOT";
    case SystemState::STANDBY: return "STANDBY";
    case SystemState::TEST_MODE: return "TEST_MODE";
    case SystemState::SAFE_MODE: return "SAFE_MODE";
    case SystemState::STREAMING: return "STREAMING";
    default: return "UNKNOWN";
  }
}

bool readSOE() {
  return digitalRead(PIN_SOE_ISO) == HIGH;
}

bool readLO() {
  return digitalRead(PIN_LO_ISO) == HIGH;
}

void setLed(uint8_t channel, uint8_t value) {
  if (channel < 1 || channel > 4) return;

  ledPwm[channel - 1] = value;

  const int pins[4] = {
    PIN_LED_EN1,
    PIN_LED_EN2,
    PIN_LED_EN3,
    PIN_LED_EN4
  };

  analogWrite(pins[channel - 1], value);
}

void setAllLeds(uint8_t value) {
  for (uint8_t i = 1; i <= 4; i++) {
    setLed(i, value);
  }
}

void setCameraPower(bool enabled) {
  cameraPower = enabled;
  digitalWrite(PIN_CAM_EN, enabled ? HIGH : LOW);
}

// ---------- Shift register ----------

void shiftRegisterWrite(uint8_t data) {
  digitalWrite(PIN_RCLK, LOW);

  for (int i = 7; i >= 0; i--) {
    digitalWrite(PIN_SRCLK, LOW);
    digitalWrite(PIN_SER, ((data >> i) & 0x01) ? HIGH : LOW);
    digitalWrite(PIN_SRCLK, HIGH);
  }

  digitalWrite(PIN_RCLK, HIGH);
}

void initShiftRegister() {
  pinMode(PIN_SRCLK, OUTPUT);
  pinMode(PIN_RCLK, OUTPUT);
  pinMode(PIN_OE, OUTPUT);
  pinMode(PIN_SER, OUTPUT);

  digitalWrite(PIN_SRCLK, LOW);
  digitalWrite(PIN_RCLK, LOW);
  digitalWrite(PIN_SER, LOW);

  // 74HC595 OE is active-low.
  digitalWrite(PIN_OE, LOW);

  shiftRegisterWrite(0x00);
}

// ---------- JSON telemetry ----------

void sendBoot() {
  Serial.print("{\"type\":\"POWER_ON\",\"t_ms\":");
  Serial.print(millis());
  Serial.print(",\"message\":\"CAPILUX powered on\",\"fw\":\"usb_sensors_1\"}");
  Serial.println();
}

void sendAck(const String& cmd) {
  Serial.print("{\"type\":\"ACK\",\"t_ms\":");
  Serial.print(millis());
  Serial.print(",\"cmd\":\"");
  Serial.print(cmd);
  Serial.print("\"}");
  Serial.println();
}

void sendError(const String& message, const String& detail = "") {
  Serial.print("{\"type\":\"ERROR\",\"t_ms\":");
  Serial.print(millis());
  Serial.print(",\"message\":\"");
  Serial.print(message);
  Serial.print("\"");

  if (detail.length() > 0) {
    Serial.print(",\"detail\":\"");
    Serial.print(detail);
    Serial.print("\"");
  }

  Serial.print("}");
  Serial.println();
}

uint32_t getLogFileSize() {
  if (!sdMounted) return 0;

  if (logFile) {
    return logFile.size();
  }

  if (!SD.exists(LOG_FILE_PATH)) return 0;

  File f = SD.open(LOG_FILE_PATH, FILE_READ);
  if (!f) return 0;

  uint32_t size = f.size();
  f.close();
  return size;
}

void sendMemoryReport() {
  Serial.print("{\"type\":\"MEMORY_REPORT\",\"t_ms\":");
  Serial.print(millis());

  Serial.print(",\"sd_mounted\":");
  Serial.print(sdMounted ? 1 : 0);

  Serial.print(",\"logger_ok\":");
  Serial.print(loggerOk ? 1 : 0);

  Serial.print(",\"memory_saved\":");
  Serial.print(memorySaved ? 1 : 0);

  Serial.print(",\"log_file\":\"");
  Serial.print(LOG_FILE_PATH);
  Serial.print("\"");

  Serial.print(",\"log_bytes\":");
  Serial.print(getLogFileSize());

  Serial.print(",\"log_lines\":");
  Serial.print(logLineCounter);

  Serial.print(",\"last_save_ms\":");
  Serial.print(lastSaveMs);

  Serial.print("}");
  Serial.println();
}

void sendStatus() {
  Serial.print("{\"type\":\"STATUS_REPORT\",\"t_ms\":");
  Serial.print(millis());

  Serial.print(",\"state\":\"");
  Serial.print(stateName(systemState));
  Serial.print("\"");

  Serial.print(",\"uptime_ms\":");
  Serial.print(millis() - bootTimeMs);

  Serial.print(",\"soe\":");
  Serial.print(readSOE() ? 1 : 0);

  Serial.print(",\"lo\":");
  Serial.print(readLO() ? 1 : 0);

  Serial.print(",\"camera_power\":");
  Serial.print(cameraPower ? 1 : 0);

  Serial.print(",\"led1\":");
  Serial.print(ledPwm[0]);
  Serial.print(",\"led2\":");
  Serial.print(ledPwm[1]);
  Serial.print(",\"led3\":");
  Serial.print(ledPwm[2]);
  Serial.print(",\"led4\":");
  Serial.print(ledPwm[3]);

  Serial.print(",\"commands_received\":");
  Serial.print(commandsReceived);

  Serial.print(",\"bad_commands\":");
  Serial.print(badCommands);

  Serial.print("}");
  Serial.println();
}

void sendHousekeeping() {
  Serial.print("{\"type\":\"HK_REPORT\",\"t_ms\":");
  Serial.print(millis());

  int okCount = sensorsOkCount();

  Serial.print(",\"hk_ok\":");
  Serial.print(((sdMounted || loggerOk) && okCount == 6) ? 1 : 0);

  // voltage sensing not implemented in hardware yet
  Serial.print(",\"voltage_ok\":0");

  Serial.print(",\"board_temp_ok\":");
  Serial.print((rBaro.ok && rBaro.temperature_C > 0 && rBaro.temperature_C < 60) ? 1 : 0);

  Serial.print(",\"pressure_ok\":");
  Serial.print((rCh1.ok && rCh2.ok && rCh3.ok && rCh4.ok) ? 1 : 0);

  Serial.print(",\"sensors_ok\":");
  Serial.print(okCount);

  printJsonFloat("ch1_p_hpa", rCh1.pressure_hPa, rCh1.ok);
  printJsonFloat("ch1_t_c",   rCh1.temperature_C, rCh1.ok);
  printJsonFloat("ch2_p_hpa", rCh2.pressure_hPa, rCh2.ok);
  printJsonFloat("ch2_t_c",   rCh2.temperature_C, rCh2.ok);
  printJsonFloat("ch3_p_hpa", rCh3.pressure_hPa, rCh3.ok);
  printJsonFloat("ch3_t_c",   rCh3.temperature_C, rCh3.ok);
  printJsonFloat("ch4_p_hpa", rCh4.pressure_hPa, rCh4.ok);
  printJsonFloat("ch4_t_c",   rCh4.temperature_C, rCh4.ok);
  printJsonFloat("baro_p_hpa", rBaro.pressure_hPa, rBaro.ok);
  printJsonFloat("baro_t_c",   rBaro.temperature_C, rBaro.ok);
  printJsonFloat("ax_g", rImu.ax_g, rImu.ok, 3);
  printJsonFloat("ay_g", rImu.ay_g, rImu.ok, 3);
  printJsonFloat("az_g", rImu.az_g, rImu.ok, 3);
  printJsonFloat("gx_dps", rImu.gx_dps, rImu.ok, 1);
  printJsonFloat("gy_dps", rImu.gy_dps, rImu.ok, 1);
  printJsonFloat("gz_dps", rImu.gz_dps, rImu.ok, 1);

  Serial.print(",\"sd_mounted\":");
  Serial.print(sdMounted ? 1 : 0);

  Serial.print(",\"logger_ok\":");
  Serial.print(loggerOk ? 1 : 0);

  Serial.print(",\"memory_saved\":");
  Serial.print(memorySaved ? 1 : 0);

  Serial.print(",\"log_bytes\":");
  Serial.print(getLogFileSize());

  Serial.print(",\"sensor_mode\":\"");
  if (okCount == 6) Serial.print("live");
  else if (okCount > 0) Serial.print("degraded");
  else Serial.print("not_connected");
  Serial.print("\"");

  Serial.print("}");
  Serial.println();
}

void sendObservationStatus() {
  Serial.print("{\"type\":\"OBS_REPORT\",\"t_ms\":");
  Serial.print(millis());

  Serial.print(",\"camera_power\":");
  Serial.print(cameraPower ? 1 : 0);

  Serial.print(",\"camera_status\":\"");
  Serial.print(cameraPower ? "POWERED" : "OFF");
  Serial.print("\"");

  Serial.print(",\"led1\":");
  Serial.print(ledPwm[0]);

  Serial.print(",\"led2\":");
  Serial.print(ledPwm[1]);

  Serial.print(",\"led3\":");
  Serial.print(ledPwm[2]);

  Serial.print(",\"led4\":");
  Serial.print(ledPwm[3]);

  Serial.print("}");
  Serial.println();
}

void sendHelp() {
  Serial.println("{\"type\":\"HELP\",\"commands\":[\"PING\",\"GET_STATUS\",\"GET_HK\",\"GET_OBS_STATUS\",\"GET_MEMORY_STATUS\",\"START_STATUS_STREAM\",\"STOP_STATUS_STREAM\",\"START_HK_STREAM\",\"STOP_HK_STREAM\",\"ENTER_TEST_MODE\",\"EXIT_TEST_MODE\",\"ENTER_SAFE_MODE\",\"EXIT_SAFE_MODE\",\"LED 1 255\",\"LED_ALL 120\",\"LED_OFF\",\"CAM_ON\",\"CAM_OFF\",\"SAVE_MEMORY\",\"CLEAR_MEMORY\",\"REBOOT\"]}");
}

// ---------- SD card and logger ----------

bool openLogFile() {
  if (!sdMounted) {
    loggerOk = false;
    return false;
  }

  if (!SD.exists(DATA_DIR)) {
    if (!SD.mkdir(DATA_DIR)) {
      loggerOk = false;
      return false;
    }
  }

  bool newFile = !SD.exists(LOG_FILE_PATH);

  logFile = SD.open(LOG_FILE_PATH, FILE_WRITE);
  if (!logFile) {
    loggerOk = false;
    return false;
  }

  if (newFile || logFile.size() == 0) {
    logFile.println("log_line,t_ms,state,soe,lo,camera_power,led1,led2,led3,led4,commands_received,bad_commands,ch1_p_hpa,ch1_t_c,ch2_p_hpa,ch2_t_c,ch3_p_hpa,ch3_t_c,ch4_p_hpa,ch4_t_c,baro_p_hpa,baro_t_c,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps");
    logFile.flush();
  }

  loggerOk = true;
  return true;
}

bool initSdAndLogger() {
  SPI.setRX(PIN_SD_MISO);
  SPI.setTX(PIN_SD_MOSI);
  SPI.setSCK(PIN_SD_CLK);
  SPI.setCS(PIN_SD_CS);
  SPI.begin();

  sdMounted = SD.begin(PIN_SD_CS);

  if (!sdMounted) {
    loggerOk = false;
    return false;
  }

  memorySaved = SD.exists(SAVE_MARKER_PATH);

  return openLogFile();
}

void closeLogFile() {
  if (logFile) {
    logFile.flush();
    logFile.close();
  }
  loggerOk = false;
}

void appendLogLine() {
  if (!sdMounted || !loggerOk || !logFile) return;

  logLineCounter++;

  logFile.print(logLineCounter);
  logFile.print(",");
  logFile.print(millis());
  logFile.print(",");
  logFile.print(stateName(systemState));
  logFile.print(",");
  logFile.print(readSOE() ? 1 : 0);
  logFile.print(",");
  logFile.print(readLO() ? 1 : 0);
  logFile.print(",");
  logFile.print(cameraPower ? 1 : 0);
  logFile.print(",");
  logFile.print(ledPwm[0]);
  logFile.print(",");
  logFile.print(ledPwm[1]);
  logFile.print(",");
  logFile.print(ledPwm[2]);
  logFile.print(",");
  logFile.print(ledPwm[3]);
  logFile.print(",");
  logFile.print(commandsReceived);
  logFile.print(",");
  logFile.print(badCommands);

  auto csvF = [](float v, bool ok, int digits) {
    logFile.print(",");
    if (ok && !isnan(v)) logFile.print(v, digits);
  };
  csvF(rCh1.pressure_hPa, rCh1.ok, 2);  csvF(rCh1.temperature_C, rCh1.ok, 2);
  csvF(rCh2.pressure_hPa, rCh2.ok, 2);  csvF(rCh2.temperature_C, rCh2.ok, 2);
  csvF(rCh3.pressure_hPa, rCh3.ok, 2);  csvF(rCh3.temperature_C, rCh3.ok, 2);
  csvF(rCh4.pressure_hPa, rCh4.ok, 2);  csvF(rCh4.temperature_C, rCh4.ok, 2);
  csvF(rBaro.pressure_hPa, rBaro.ok, 2); csvF(rBaro.temperature_C, rBaro.ok, 2);
  csvF(rImu.ax_g, rImu.ok, 3); csvF(rImu.ay_g, rImu.ok, 3); csvF(rImu.az_g, rImu.ok, 3);
  csvF(rImu.gx_dps, rImu.ok, 1); csvF(rImu.gy_dps, rImu.ok, 1); csvF(rImu.gz_dps, rImu.ok, 1);
  logFile.println();

  // For bench testing, flush every line so it is easy to verify.
  logFile.flush();
}

bool saveMemory() {
  if (!sdMounted) {
    return false;
  }

  if (logFile) {
    logFile.flush();
    logFile.close();
  }

  File marker = SD.open(SAVE_MARKER_PATH, FILE_WRITE);
  if (!marker) {
    loggerOk = false;
    return false;
  }

  marker.print("CAPILUX memory saved at t_ms=");
  marker.println(millis());
  marker.print("log_file=");
  marker.println(LOG_FILE_PATH);
  marker.print("log_bytes=");
  marker.println(getLogFileSize());
  marker.close();

  memorySaved = true;
  lastSaveMs = millis();

  return openLogFile();
}

bool clearMemory() {
  if (!sdMounted) {
    return false;
  }

  if (systemState == SystemState::STREAMING) {
    return false;
  }

  closeLogFile();

  if (SD.exists(LOG_FILE_PATH)) {
    SD.remove(LOG_FILE_PATH);
  }

  if (SD.exists(SAVE_MARKER_PATH)) {
    SD.remove(SAVE_MARKER_PATH);
  }

  memorySaved = false;
  logLineCounter = 0;
  lastSaveMs = 0;

  return openLogFile();
}

// ---------- Command parsing ----------

String getToken(String s, uint8_t index) {
  s.trim();

  uint8_t currentIndex = 0;
  int start = 0;

  for (int i = 0; i <= s.length(); i++) {
    if (i == s.length() || s[i] == ' ') {
      if (currentIndex == index) {
        return s.substring(start, i);
      }

      currentIndex++;

      while (i + 1 < s.length() && s[i + 1] == ' ') {
        i++;
      }

      start = i + 1;
    }
  }

  return "";
}

void requestReboot() {
  Serial.println("{\"type\":\"ACK\",\"cmd\":\"REBOOT\",\"message\":\"rebooting\"}");
  Serial.flush();

  if (logFile) {
    logFile.flush();
    logFile.close();
  }

  delay(100);

#if defined(ARDUINO_ARCH_RP2040)
  watchdog_reboot(0, 0, 0);
  while (true) {}
#else
  void (*resetFunc)(void) = 0;
  resetFunc();
#endif
}

void handleCommand(String cmdRaw) {
  cmdRaw.trim();

  if (cmdRaw.length() == 0) return;

  commandsReceived++;

  String cmd = cmdRaw;
  cmd.toUpperCase();

  String base = getToken(cmd, 0);

  if (base == "PING") {
    sendAck("PING");
  }
  else if (base == "HELP") {
    sendHelp();
  }
  else if (base == "GET_STATUS") {
    sendStatus();
  }
  else if (base == "GET_HK") {
    sendHousekeeping();
  }
  else if (base == "GET_OBS_STATUS") {
    sendObservationStatus();
  }
  else if (base == "GET_MEMORY_STATUS") {
    sendMemoryReport();
  }
  else if (base == "START_STATUS_STREAM") {
    statusStreamEnabled = true;
    sendAck("START_STATUS_STREAM");
  }
  else if (base == "STOP_STATUS_STREAM") {
    statusStreamEnabled = false;
    sendAck("STOP_STATUS_STREAM");
  }
  else if (base == "START_HK_STREAM") {
    hkStreamEnabled = true;
    systemState = SystemState::STREAMING;
    sendAck("START_HK_STREAM");
  }
  else if (base == "STOP_HK_STREAM") {
    hkStreamEnabled = false;
    systemState = SystemState::STANDBY;
    sendAck("STOP_HK_STREAM");
  }
  else if (base == "ENTER_TEST_MODE") {
    systemState = SystemState::TEST_MODE;
    sendAck("ENTER_TEST_MODE");
    sendStatus();
  }
  else if (base == "EXIT_TEST_MODE") {
    systemState = SystemState::STANDBY;
    sendAck("EXIT_TEST_MODE");
    sendStatus();
  }
  else if (base == "ENTER_SAFE_MODE") {
    systemState = SystemState::SAFE_MODE;
    hkStreamEnabled = false;
    setCameraPower(false);
    setAllLeds(0);
    sendAck("ENTER_SAFE_MODE");
    sendStatus();
    sendObservationStatus();
  }
  else if (base == "EXIT_SAFE_MODE") {
    systemState = SystemState::STANDBY;
    sendAck("EXIT_SAFE_MODE");
    sendStatus();
  }
  else if (base == "CAM_ON") {
    setCameraPower(true);
    sendAck("CAM_ON");
    sendObservationStatus();
  }
  else if (base == "CAM_OFF") {
    setCameraPower(false);
    sendAck("CAM_OFF");
    sendObservationStatus();
  }
  else if (base == "LED") {
    int channel = getToken(cmd, 1).toInt();
    int value = getToken(cmd, 2).toInt();

    if (channel < 1 || channel > 4 || value < 0 || value > 255) {
      badCommands++;
      sendError("bad_led_command", cmdRaw);
      return;
    }

    setLed((uint8_t)channel, (uint8_t)value);
    sendAck("LED");
    sendObservationStatus();
  }
  else if (base == "LED_ALL") {
    int value = getToken(cmd, 1).toInt();

    if (value < 0 || value > 255) {
      badCommands++;
      sendError("bad_led_all_command", cmdRaw);
      return;
    }

    setAllLeds((uint8_t)value);
    sendAck("LED_ALL");
    sendObservationStatus();
  }
  else if (base == "LED_OFF") {
    setAllLeds(0);
    sendAck("LED_OFF");
    sendObservationStatus();
  }
  else if (base == "SAVE_MEMORY") {
    bool ok = saveMemory();

    if (ok) {
      sendAck("SAVE_MEMORY");
    } else {
      badCommands++;
      sendError("save_memory_failed", "Check SD card and logger state");
    }

    sendMemoryReport();
  }
  else if (base == "CLEAR_MEMORY") {
    bool ok = clearMemory();

    if (ok) {
      sendAck("CLEAR_MEMORY");
    } else {
      badCommands++;
      sendError("clear_memory_failed", "Clear is blocked during STREAMING or SD is unavailable");
    }

    sendMemoryReport();
  }
  else if (base == "REBOOT") {
    requestReboot();
  }
  else {
    badCommands++;
    sendError("unknown_command", cmdRaw);
  }
}

void readCommands() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\n') {
      handleCommand(commandBuffer);
      commandBuffer = "";
    }
    else if (c != '\r') {
      if (commandBuffer.length() < MAX_COMMAND_LENGTH) {
        commandBuffer += c;
      } else {
        commandBuffer = "";
        badCommands++;
        sendError("command_too_long");
      }
    }
  }
}

// ---------- Setup and loop ----------

void setup() {
  bootTimeMs = millis();

  Serial.begin(LINK_BAUD);
  delay(1500);

  pinMode(PIN_LED_EN1, OUTPUT);
  pinMode(PIN_LED_EN2, OUTPUT);
  pinMode(PIN_LED_EN3, OUTPUT);
  pinMode(PIN_LED_EN4, OUTPUT);

  pinMode(PIN_CAM_EN, OUTPUT);

  pinMode(PIN_SOE_ISO, INPUT_PULLDOWN);
  pinMode(PIN_LO_ISO, INPUT_PULLDOWN);

  setAllLeds(0);
  setCameraPower(false);

  initShiftRegister();

  systemState = SystemState::STANDBY;

  initSdAndLogger();
  initSensors();
  sampleSensors();

  sendBoot();
  sendHelp();
  sendStatus();
  sendHousekeeping();
  sendMemoryReport();
  sendObservationStatus();
}

void loop() {
  readCommands();

  uint32_t now = millis();

  if (now - lastSensorMs >= SENSOR_SAMPLE_PERIOD_MS) {
    lastSensorMs = now;
    sampleSensors();
  }

  if (statusStreamEnabled && now - lastStatusMs >= STATUS_PERIOD_MS) {
    lastStatusMs = now;
    sendStatus();
  }

  if (hkStreamEnabled && now - lastHkMs >= HK_PERIOD_MS) {
    lastHkMs = now;
    sendHousekeeping();
  }

  if (now - lastLogMs >= LOG_PERIOD_MS) {
    lastLogMs = now;
    appendLogLine();
  }
}
