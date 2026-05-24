#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

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
bool hkStreamEnabled = false;

bool cameraPower = false;

uint8_t ledPwm[4] = {0, 0, 0, 0};

// SD/logger state
bool sdMounted = false;
bool loggerOk = false;
bool memorySaved = false;
uint32_t logLineCounter = 0;
uint32_t lastSaveMs = 0;
File logFile;

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
  Serial.print(",\"message\":\"CAPILUX powered on\",\"fw\":\"usb_sd_memory_demo_1\"}");
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
  /*
    Sensors are not connected yet, so pressure/temperature/voltage data
    are intentionally not faked.

    SD/logger status is real.
  */

  Serial.print("{\"type\":\"HK_REPORT\",\"t_ms\":");
  Serial.print(millis());

  Serial.print(",\"hk_ok\":");
  Serial.print(sdMounted || loggerOk ? 1 : 0);

  Serial.print(",\"voltage_ok\":0");
  Serial.print(",\"board_temp_ok\":0");
  Serial.print(",\"pressure_ok\":0");

  Serial.print(",\"sd_mounted\":");
  Serial.print(sdMounted ? 1 : 0);

  Serial.print(",\"logger_ok\":");
  Serial.print(loggerOk ? 1 : 0);

  Serial.print(",\"memory_saved\":");
  Serial.print(memorySaved ? 1 : 0);

  Serial.print(",\"log_bytes\":");
  Serial.print(getLogFileSize());

  Serial.print(",\"sensor_mode\":\"not_connected\"");

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
    logFile.println("log_line,t_ms,state,soe,lo,camera_power,led1,led2,led3,led4,commands_received,bad_commands");
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
  logFile.println(badCommands);

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
