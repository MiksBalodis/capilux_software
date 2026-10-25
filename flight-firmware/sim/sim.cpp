// Host simulator for the flight firmware.
// Runs the real, unmodified src/main.cpp (built as fw.so against sim/mock) on a fake clock with
// simulated RXSM signals, uplink, 8 RunCam cameras, SD card, onboard flash, watchdog and power cuts.
// Every boot loads a fresh copy of fw.so, so all firmware globals start from zero like after a reset;
// flash, SD card and the watchdog scratch registers live here and survive as they would on hardware.
// Build + run: sim/run.sh
#include <Arduino.h>
#include <SdFat.h>
#include <Wire.h>
#include <hardware/flash.h>
#include <hardware/watchdog.h>
#include <dlfcn.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <unistd.h>
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <vector>
#include "../src/config.h"

// ======================= simulated hardware =======================
static uint64_t simUs, bootUs;  // global clock / clock at the last reset
static jmp_buf resetJmp;
static bool inFw;
enum Reset { R_WDT = 1, R_REBOOT = 2 };

static bool wdtOn, wdtCaused;
static uint32_t wdtMs;
static uint64_t wdtLastUs;
static WatchdogHw wdHw;
WatchdogHw *watchdog_hw = &wdHw;
bool watchdog_caused_reboot() { return wdtCaused; }

static void checkWdt() {
  if (inFw && wdtOn && simUs - wdtLastUs > (uint64_t)wdtMs * 1000) longjmp(resetJmp, R_WDT);
}
uint32_t millis() { simUs += 10; checkWdt(); return (uint32_t)((simUs - bootUs) / 1000); }
void delay(uint32_t ms) { simUs += ms * 1000ull; checkWdt(); }
void delayMicroseconds(uint32_t us) { simUs += us; checkWdt(); }
void RP2040Sim::wdt_begin(uint32_t ms) { wdtOn = true; wdtMs = ms; wdtLastUs = simUs; }
void RP2040Sim::wdt_reset() { wdtLastUs = simUs; }
void RP2040Sim::reboot() { longjmp(resetJmp, R_REBOOT); }
RP2040Sim rp2040;

static int pinMode_[32], pinOut[32], pinIn[32];
static uint64_t camEnOnUs;
static std::vector<std::pair<double, int>> camEnLog;  // (T s, level)
static double T();
void pinMode(int p, int m) { pinMode_[p] = m; }
void digitalWrite(int p, int v) {
  if (p == PIN_CAM_EN && pinOut[p] != !!v) {
    camEnLog.push_back({T(), !!v});
    if (v) camEnOnUs = simUs;
  }
  pinOut[p] = !!v;
}
int digitalRead(int p) { return pinMode_[p] == OUTPUT ? pinOut[p] : pinIn[p]; }
static int ledPwm[32];
static std::vector<std::pair<double, int>> led0Log;  // chamber 1 LED: (T s, pwm)
void analogWrite(int p, int v) {
  if (p == PIN_CH_LED[0] && ledPwm[p] != v) led0Log.push_back({T(), v});
  ledPwm[p] = v;
}
void analogWriteFreq(int) {}
void analogWriteRange(int) {}
int analogRead(int) { return 2068; }  // VSYS 5.0 V
void analogReadResolution(int) {}
static uint8_t mux595 = 0xFF;
void shiftOut(int, int, int, uint8_t v) { mux595 = v; }

// ---- serial ports ----
struct SimPort { std::deque<uint8_t> rx; std::string tx; int id; };
static SimPort pUsb{{}, {}, 0}, pUp{{}, {}, 1}, pCam{{}, {}, 2};
SerialSim Serial(&pUsb), Serial1(&pUp), Serial2(&pCam);
void SerialSim::setFIFOSize(size_t) {}
int SerialSim::available() { return (int)p->rx.size(); }
int SerialSim::read() { if (p->rx.empty()) return -1; int c = p->rx.front(); p->rx.pop_front(); return c; }
int SerialSim::availableForWrite() { return 64; }
SerialSim::operator bool() { return false; }  // no USB host
void SerialSim::println(const char *) {}
static void camRx(const uint8_t *b, size_t n);
static void downlink(const std::string &line);
size_t SerialSim::write(uint8_t c) {
  if (p->id == 1) { if (c == '\n') { downlink(p->tx); p->tx.clear(); } else p->tx += (char)c; }
  if (p->id == 2) camRx(&c, 1);
  return 1;
}
size_t SerialSim::write(const uint8_t *b, size_t n) { for (size_t i = 0; i < n; i++) write(b[i]); return n; }

// ---- cameras (RunCam Device Protocol subset) ----
struct Cam { bool rec; double recSec; int ignoreStarts; bool dead; } cam[8];
static uint64_t lastCamTickUs;
static std::string camBuf;
static uint8_t crc8(const uint8_t *d, int n) {
  uint8_t c = 0;
  while (n--) { c ^= *d++; for (int i = 0; i < 8; i++) c = (c & 0x80) ? (c << 1) ^ 0xD5 : c << 1; }
  return c;
}
static void camTick() {  // recording time advances while powered and recording
  double dt = (simUs - lastCamTickUs) / 1e6;
  lastCamTickUs = simUs;
  for (auto &c : cam) {
    if (!pinOut[PIN_CAM_EN]) c.rec = false;
    if (c.rec) c.recSec += dt;
  }
}
static bool camBooted() { return pinOut[PIN_CAM_EN] && simUs - camEnOnUs >= 3000000; }
static void camRx(const uint8_t *b, size_t n) {
  camTick();
  camBuf.append((const char *)b, n);
  if ((mux595 & 0xC0) != 0) { camBuf.clear(); return; }  // muxes disabled
  int ch = mux595 & 7;
  Cam &c = cam[ch];
  const uint8_t *q = (const uint8_t *)camBuf.data();
  if (camBuf.size() >= 4 && q[0] == 0xCC && q[1] == 0x01 && crc8(q, 3) == q[3]) {
    if (camBooted() && !c.dead) {
      if (q[2] == 0x03) { if (c.ignoreStarts > 0) c.ignoreStarts--; else c.rec = true; }
      if (q[2] == 0x04) c.rec = false;
    }
    camBuf.clear();
  } else if (camBuf.size() >= 5 && q[0] == 0xCC && q[1] == 0x10) {
    if (camBooted() && !c.dead && crc8(q, 4) == q[4]) {
      char val[16];
      snprintf(val, sizeof val, "%d", (int)(3600 - c.recSec));
      std::string pl;
      pl += (char)4; pl += "REMAINING_TIME"; pl += '\0'; pl += val; pl += '\0';
      std::string fr;
      fr += (char)0xCC; fr += (char)0; fr += (char)pl.size(); fr += pl;
      fr += (char)crc8((const uint8_t *)fr.data(), (int)fr.size());
      for (char x : fr) pCam.rx.push_back((uint8_t)x);
    }
    camBuf.clear();
  } else if (camBuf.size() >= 5) camBuf.clear();
}

// ---- I2C: no devices fitted; a stuck bus can be simulated by holding SDA low ----
// ---- I2C: only the LSM6DSO32 (bus 0, 0x6B) answers; everything else NACKs. A stuck bus can be
// simulated by holding SDA low. The IMU feeds its FIFO at 104 Hz from accelG(T) (z axis). ----
static double dipAt = 1e9, dipLen;  // injected short drop to ~0 g (tests)
static double accelG(double t) {  // rough REXUS profile, enough to exercise the microgravity detection
  if (t >= dipAt && t < dipAt + dipLen) return 0.01;
  if (t < 0) return 1.0;
  if (t < 26) return 6.0;    // motor burn
  if (t < 67) return 0.3;    // coast, spinning (IMU off axis)
  if (t < 240) return 0.01;  // after yo-yo despin: microgravity
  return 3.0;                // re-entry
}
static uint8_t i2cAddr, i2cReg, i2cVal, i2cN;
static std::deque<uint8_t> i2cRx;
static std::deque<std::array<uint8_t, 7>> imuFifo;
static uint64_t imuGenUs;
static uint32_t imuSamples;
static bool imuHere(TwoWire *w) { return w->bus == 0 && i2cAddr == ADDR_IMU; }
static void imuGen() {
  while (simUs - imuGenUs >= 1000000 / 104) {
    imuGenUs += 1000000 / 104;
    if (imuFifo.size() > 3000) continue;  // overrun: drop
    int16_t z = (int16_t)(accelG(T()) / 0.000976);
    imuFifo.push_back({0x01 << 3, 0, 0, 0, 0, 0, 0});                                // gyro 0
    imuFifo.push_back({0x02 << 3, 0, 0, 0, 0, (uint8_t)z, (uint8_t)(z >> 8)});      // accel
    if (++imuSamples % 8 == 0) imuFifo.push_back({0x04 << 3, (uint8_t)imuSamples, (uint8_t)(imuSamples >> 8), 0, 0, 0, 0});
  }
}
TwoWire Wire(0), Wire1(1);
void TwoWire::beginTransmission(uint8_t a) { i2cAddr = a; i2cN = 0; }
size_t TwoWire::write(uint8_t v) { if (i2cN++ == 0) i2cReg = v; else i2cVal = v; return 1; }
uint8_t TwoWire::endTransmission(bool) {
  delayMicroseconds(200);
  if (!imuHere(this)) return 2;  // NACK
  if (i2cN == 2 && i2cReg == 0x0A && i2cVal == 0x00) { imuFifo.clear(); imuGenUs = simUs; }  // FIFO bypass
  return 0;
}
size_t TwoWire::requestFrom(uint8_t a, size_t n) {
  i2cAddr = a;
  if (!imuHere(this)) return 0;
  i2cRx.clear();
  if (i2cReg == 0x0F) i2cRx.push_back(0x6C);
  else if (i2cReg == 0x3A) { imuGen(); size_t w = imuFifo.size(); i2cRx.push_back(w & 0xFF); i2cRx.push_back((w >> 8) & 0x03); }
  else if (i2cReg == 0x78) { if (imuFifo.empty()) return 0; for (uint8_t b : imuFifo.front()) i2cRx.push_back(b); imuFifo.pop_front(); }
  else while (i2cRx.size() < n) i2cRx.push_back(0);
  while (i2cRx.size() < n) i2cRx.push_back(0);
  return n;
}
int TwoWire::read() { if (i2cRx.empty()) return -1; int v = i2cRx.front(); i2cRx.pop_front(); return v; }

// ---- onboard flash at XIP_BASE ----
static uint8_t *flash;
static std::vector<double> eraseLog;  // T of every sector erase
void flash_range_erase(uint32_t ofs, size_t n) { memset(flash + ofs, 0xFF, n); eraseLog.push_back(T()); delay(45); }
void flash_range_program(uint32_t ofs, const uint8_t *d, size_t n) { for (size_t i = 0; i < n; i++) flash[ofs + i] &= d[i]; }

// ---- SD card ----
struct FileData { std::string data; uint32_t size; };
static std::map<std::string, FileData> files;
static std::set<std::string> dirs;
static bool sdPresent = true, sdHang;
static int sdWriteFails;  // next n writes fail
bool SdFat32::begin(SdSpiConfig) {
  if (sdHang) for (;;) delay(100);  // a card that never answers: only the watchdog gets us out
  delay(20);
  return sdPresent;
}
void SdFat32::end() {}
bool SdFat32::exists(const char *p) { return dirs.count(p) || files.count(p); }
bool SdFat32::mkdir(const char *p) { if (!sdPresent) return false; dirs.insert(p); return true; }
bool File32::createContiguous(const char *p, uint32_t n) {
  if (!sdPresent) return false;
  path = p; files[path] = {std::string(), n}; size = n; pos = 0; open_ = true; append = false;
  delay(50);
  return true;
}
bool File32::open(const char *p, int flags) {
  if (!sdPresent) return false;
  path = p;
  if (!files.count(path)) { if (!(flags & O_CREAT)) return false; files[path] = {std::string(), 0}; }
  append = flags & O_APPEND; pos = 0; open_ = true;
  return true;
}
bool File32::seekSet(uint32_t at) { if (!open_ || !sdPresent) return false; pos = at; return true; }
size_t File32::write(const void *b, size_t n) {
  if (!open_ || !sdPresent) return 0;
  if (sdWriteFails > 0) { sdWriteFails--; return 0; }
  FileData &f = files[path];
  if (append) pos = f.data.size();
  if (f.data.size() < pos + n) f.data.resize(pos + n);
  memcpy(&f.data[pos], b, n);
  pos += n;
  return n;
}
bool File32::sync() { return open_ && sdPresent; }
bool File32::close() { bool ok = open_; open_ = false; return ok; }

// ======================= harness =======================
// Sim time 0 = power-on at T-600 s (SED: power on T-600 s)
static double T() { return simUs / 1e6 - 600.0; }
static uint64_t atT(double t) { return (uint64_t)((t + 600.0) * 1e6); }

struct Pkt { double t; std::string s; };
static std::vector<Pkt> pkts;
static void downlink(const std::string &line) { pkts.push_back({T(), line}); }

static std::string field(const std::string &s, const char *k) {
  std::string key = std::string("\"") + k + "\":";
  size_t i = s.find(key);
  if (i == std::string::npos) return "";
  i += key.size();
  if (s[i] == '"') { size_t e = s.find('"', i + 1); return s.substr(i + 1, e - i - 1); }
  size_t e = s.find_first_of(",}", i);
  return s.substr(i, e - i);
}
// time of the first packet of type t (with field k == v) at or after `from`, or NAN
static double first(const char *t, const char *k, const char *v, double from = -1e9) {
  for (auto &p : pkts)
    if (p.t >= from && field(p.s, "t") == t && (!k || field(p.s, k) == v)) return p.t;
  return NAN;
}
static double evt(const char *e, double from = -1e9) { return first("EVT", "e", e, from); }
static int count(const char *t, const char *k, const char *v, double from = -1e9) {
  int n = 0;
  for (auto &p : pkts) n += p.t >= from && field(p.s, "t") == t && (!k || field(p.s, k) == v);
  return n;
}
static std::string lastSt() {
  for (auto i = pkts.rbegin(); i != pkts.rend(); ++i) if (field(i->s, "t") == "STA") return field(i->s, "st");
  return "";
}
static double camOffAfter(double from) {
  for (auto &c : camEnLog) if (c.first >= from && !c.second) return c.first;
  return NAN;
}

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (cond) printf("  ok    "); else { fails++; printf("  FAIL  "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static bool near(double a, double b, double tol) { return fabs(a - b) <= tol; }

// ---- firmware instances ----
static void (*fwSetup)(), (*fwLoop)();
static int instance;
static bool powered;
static const char *fwFile = "fw.so";
static void loadFw() {
  char p[64];
  snprintf(p, sizeof p, "/tmp/capilux_sim_fw_%d_%d.so", getpid(), instance++);
  char cmd[160];
  snprintf(cmd, sizeof cmd, "cp %s %s", fwFile, p);
  if (system(cmd)) { perror("cp"); exit(2); }
  void *h = dlopen(p, RTLD_NOW | RTLD_LOCAL);
  if (!h) { fprintf(stderr, "%s\n", dlerror()); exit(2); }
  unlink(p);
  fwSetup = (void (*)())dlsym(h, "_Z5setupv");
  fwLoop = (void (*)())dlsym(h, "_Z4loopv");
}

static void boot();
static void handleReset(int why) {
  inFw = false;
  wdtCaused = true;  // watchdog timeout and rp2040.reboot() both come back as a watchdog reboot
  (void)why;
  boot();
}
static void boot() {
  for (;;) {
    bootUs = simUs;
    wdtOn = false;
    pUp.rx.clear(); pCam.rx.clear(); pUp.tx.clear();
    mux595 = 0xFF;
    for (int i = 0; i < 32; i++) { pinMode_[i] = INPUT; if (pinOut[i] && i == PIN_CAM_EN) digitalWrite(i, 0); pinOut[i] = 0; }
    loadFw();
    int r = setjmp(resetJmp);
    if (r) { inFw = false; wdtCaused = true; continue; }
    inFw = true;
    fwSetup();
    inFw = false;
    wdtCaused = false;
    return;
  }
}
static void powerOn() {
  powered = true;
  memset((void *)wdHw.scratch, 0, sizeof wdHw.scratch);
  wdtCaused = false;
  boot();
}
static void powerOff() {
  powered = false;
  if (pinOut[PIN_CAM_EN]) digitalWrite(PIN_CAM_EN, 0);
  for (auto &c : cam) c.rec = false;
}

static void setSoe(bool on) { pinIn[PIN_SOE] = !on; }
static void setLo(bool on) { pinIn[PIN_LO] = !on; }
static void send(const char *line) { for (const char *c = line; *c; c++) pUp.rx.push_back(*c); pUp.rx.push_back('\n'); }

static std::multimap<uint64_t, std::function<void()>> sched;
static void at(double t, std::function<void()> f) { sched.insert({atT(t), f}); }

static void runUntil(double t) {
  uint64_t end = atT(t);
  while (simUs < end) {
    while (!sched.empty() && sched.begin()->first <= simUs) {
      auto f = sched.begin()->second;
      sched.erase(sched.begin());
      f();
    }
    if (!powered) { simUs += 1000; continue; }
    int r = setjmp(resetJmp);
    if (r) { handleReset(r); continue; }
    inFw = true;
    fwLoop();
    inFw = false;
    simUs += 1000;  // ~1 ms per loop pass
    camTick();
  }
}

// a firmware hang: nothing runs, the watchdog fires after its timeout
static void hang() {
  simUs += (uint64_t)wdtMs * 1000 + 1000;
  handleReset(R_WDT);
}
static void powerCut(double sec) { powerOff(); simUs += (uint64_t)(sec * 1e6); powerOn(); }

static void resetWorld(bool wipeFlash = true) {
  pkts.clear(); camEnLog.clear(); sched.clear(); led0Log.clear(); eraseLog.clear();
  imuFifo.clear(); imuGenUs = 0; imuSamples = 0;
  simUs = 0; lastCamTickUs = 0;
  for (auto &c : cam) c = Cam{};
  if (wipeFlash) { memset(flash, 0xFF, 2 << 20); files.clear(); dirs.clear(); }
  sdPresent = true; sdHang = false; sdWriteFails = 0;
  for (int i = 0; i < 32; i++) pinIn[i] = 1;  // pull-ups: SOE/LO released, I2C idle high
  powered = false;
}

// Exact stop times from config.h
static const double T_STOP_CMD = T_SOE_S + T_EXP_END_MS / 1000.0;           // record-stop command
static const double T_ALL_OFF = T_STOP_CMD + CAM_STOP_MS / 1000.0;          // cameras + LEDs off
static const double SOE = T_SOE_S, PWR_OFF = T_POWER_OFF_S;

// ======================= scenarios =======================
static void nominal() {
  printf("\n[1] Nominal countdown and flight\n");
  resetWorld();
  powerOn();
  at(-360, [] { send("ENTER_TEST_MODE"); });
  at(-355, [] { send("CAM_ON"); send("LED_ALL 100"); });
  at(-300, [] { send("EXIT_TEST_MODE"); });
  at(SOE, [] { setSoe(true); });
  at(0, [] { setLo(true); });
  at(PWR_OFF, [] { powerOff(); });
  runUntil(PWR_OFF + 1);
  CHECK(near(evt("SOE"), SOE, 0.1), "SOE event at T%+.1f", evt("SOE"));
  CHECK(near(evt("LO"), 0, 0.1), "LO event at T%+.1f", evt("LO"));
  CHECK(count("EVT", "e", "REC_OK_7") == 1 && evt("REC_OK_7") < SOE + 20, "all 8 cameras confirmed recording (cam 7 at T%+.1f)", evt("REC_OK_7"));
  int okAll = 1;
  for (int i = 0; i < 8; i++) okAll &= cam[i].recSec > 0;
  CHECK(okAll, "every camera recorded");
  CHECK(near(evt("EXP_END"), T_STOP_CMD, 0.2), "record-stop at T%+.1f (expected T%+.1f)", evt("EXP_END"), T_STOP_CMD);
  CHECK(near(camOffAfter(0), T_ALL_OFF, 0.2), "camera power off at T%+.1f, %.1f s before RXSM power-off", camOffAfter(0), PWR_OFF - camOffAfter(0));
  CHECK(lastSt() == "SHUTDOWN", "state SHUTDOWN until power-off");
  CHECK(count("EVT", "e", "I2C0_RECOVER") + count("EVT", "e", "I2C1_RECOVER") == 0, "missing I2C sensors do not trigger bus recovery");
  CHECK(count("ERR", nullptr, nullptr) == 0, "no ERR packets");
  bool hkFile = false;
  for (auto &f : files) hkFile |= f.first.find("/HK.CSV") != std::string::npos && f.second.data.size() > 10000;
  CHECK(hkFile, "HK log written to SD");

  printf("\n[1b] Power back on after the flight, signals released (next day / bench)\n");
  double t0 = T();
  setSoe(false); setLo(false);
  powerOn();
  runUntil(t0 + 5);
  CHECK(lastSt() == "STANDBY", "boots to STANDBY (not latched) -> %s", lastSt().c_str());

  printf("\n[1c] Power back on with SOE still asserted (reset after the flight)\n");
  powerOff(); setSoe(true);
  t0 = T();
  powerOn();
  runUntil(t0 + 5);
  CHECK(lastSt() == "SHUTDOWN" && count("EVT", "e", "FLIGHT_DONE_LATCHED", t0) == 1, "stays SHUTDOWN, flight not repeated -> %s", lastSt().c_str());
  CHECK(std::isnan(camOffAfter(t0)) && !pinOut[PIN_CAM_EN], "cameras stay off");
  send("REARM");
  runUntil(t0 + 7);
  CHECK(count("EVT", "e", "SOE", t0) == 1 && pinOut[PIN_CAM_EN], "REARM with SOE asserted starts a new run at once");
  double s2 = evt("SOE", t0);
  runUntil(s2 + T_EXP_END_MS / 1000.0 + 5);
  CHECK(near(evt("EXP_END", t0), s2 + T_EXP_END_MS / 1000.0, 0.2), "new run timed from REARM (%.1f s)", evt("EXP_END", t0) - s2);
}

static void hold() {
  printf("\n[2] Countdown hold after SOE (SOE released at T-30, recycled 10 min later)\n");
  resetWorld();
  powerOn();
  at(SOE, [] { setSoe(true); });
  at(-30, [] { setSoe(false); });
  const double D = 600;  // new count: everything shifted by 10 min
  at(SOE + D, [] { setSoe(true); });
  at(D, [] { setLo(true); });
  runUntil(PWR_OFF + D);
  CHECK(near(evt("SOE_RELEASED"), -29, 0.2), "SOE release detected at T%+.1f", evt("SOE_RELEASED"));
  CHECK(near(camOffAfter(-31), -26, 0.2), "cameras stopped and off at T%+.1f", camOffAfter(-31));
  CHECK(first("STA", "st", "STANDBY", -29) < -25, "back to STANDBY, re-armed");
  CHECK(near(evt("SOE", 0), SOE + D, 0.1), "second SOE starts a fresh flight");
  CHECK(near(evt("EXP_END", 0), T_STOP_CMD + D, 0.2), "end timed from the second SOE: T%+.1f (new count)", evt("EXP_END", 0) - D);
}

static void wdtInFlight() {
  printf("\n[3] Firmware hang at T+100 -> watchdog reset\n");
  resetWorld();
  powerOn();
  at(SOE, [] { setSoe(true); });
  at(0, [] { setLo(true); });
  runUntil(100);
  hang();
  runUntil(PWR_OFF);
  double e = evt("EXP_END");
  CHECK(count("EVT", "e", "RESUME_WDT") == 1, "resumed after the watchdog reset");
  CHECK(e <= T_STOP_CMD + 0.2 && e >= T_STOP_CMD - 5, "record-stop at T%+.1f (nominal T%+.1f, earlier is OK)", e, T_STOP_CMD);
  double off = NAN, on = NAN;
  for (auto &c : camEnLog) {
    if (c.first > 99 && !c.second && std::isnan(off)) off = c.first;
    else if (!std::isnan(off) && c.second && std::isnan(on)) on = c.first;
  }
  CHECK(on - off < 0.01, "CAM_EN back on %.0f ms after the reset (the hold-up cap on Q2 must bridge this)", (on - off) * 1000);
  CHECK(cam[0].rec == false && cam[0].recSec > 300, "cameras recorded again after the reset (%.0f s total)", cam[0].recSec);
}

static void powerLoss() {
  printf("\n[4] Experiment power lost for 1 s at T+100 (SOE/LO still asserted)\n");
  resetWorld();
  powerOn();
  at(SOE, [] { setSoe(true); });
  at(0, [] { setLo(true); });
  runUntil(100);
  powerCut(1.0);
  runUntil(PWR_OFF);
  double e = evt("EXP_END", 100);
  CHECK(count("EVT", "e", "RESUME_POWER") == 1, "resumed from the flash log");
  CHECK(e <= T_STOP_CMD + 0.2 && e >= T_STOP_CMD - 5, "record-stop at T%+.1f (nominal T%+.1f)", e, T_STOP_CMD);
  CHECK(lastSt() == "SHUTDOWN", "ends in SHUTDOWN");

  printf("\n[4b] Same, but the SD card hangs on the resume\n");
  resetWorld();
  powerOn();
  at(SOE, [] { setSoe(true); });
  at(0, [] { setLo(true); });
  runUntil(100);
  sdHang = true;
  powerCut(1.0);
  runUntil(PWR_OFF);
  e = evt("EXP_END", 100);
  CHECK(count("EVT", "e", "RESUME_WDT") == 1, "second resume skipped the card");
  CHECK(e <= T_STOP_CMD + 0.2 && e >= T_STOP_CMD - 15, "record-stop at T%+.1f", e);
  CHECK(cam[3].recSec > 250, "cameras kept recording (%.0f s)", cam[3].recSec);
}

static void testModeForgotten() {
  printf("\n[5] Test mode never exited (entered T-6:00)\n");
  resetWorld();
  powerOn();
  at(-360, [] { send("ENTER_TEST_MODE"); });
  at(SOE, [] { setSoe(true); });
  at(0, [] { setLo(true); });
  runUntil(PWR_OFF);
  CHECK(near(evt("TEST_TIMEOUT"), -60, 0.2), "test mode timed out at T%+.1f", evt("TEST_TIMEOUT"));
  CHECK(near(evt("SOE"), SOE, 0.1), "flight started at SOE");
  CHECK(near(evt("EXP_END"), T_STOP_CMD, 0.2), "normal end");

  printf("\n[5b] Test mode entered late (T-1:00), SOE comes while still in test mode\n");
  resetWorld();
  powerOn();
  at(-60, [] { send("ENTER_TEST_MODE"); });
  at(SOE, [] { setSoe(true); });
  at(0, [] { setLo(true); });
  at(PWR_OFF, [] { powerOff(); });
  runUntil(PWR_OFF);
  double s = first("EVT", "e", "SOE_LATE");
  CHECK(near(s, -60 + TEST_TIMEOUT_MS / 1000.0, 0.2), "flight starts when test mode times out (T%+.1f)", s);
  CHECK(near(evt("EXP_END"), T_STOP_CMD, 0.2), "but is still timed from the real SOE: stop at T%+.1f", evt("EXP_END"));
}

static void commands() {
  printf("\n[6] Commands and protections\n");
  resetWorld();
  powerOn();
  runUntil(-500);
  send("CLEAR_MEMORY");
  runUntil(-499);
  CHECK(count("CFR", nullptr, nullptr) == 1, "CLEAR_MEMORY asks for confirmation");
  send("CLEAR_MEMORY CONFIRM");
  runUntil(-497);
  CHECK(count("ACK", "cmd", "CLEAR_MEMORY") == 1, "CLEAR_MEMORY CONFIRM accepted immediately");
  pUp.rx.push_back('P'); pUp.rx.push_back(0x80); pUp.rx.push_back('N'); pUp.rx.push_back('G'); pUp.rx.push_back('\n');
  runUntil(-496);
  CHECK(count("ERR", "err", "BAD_CMD") == 1, "corrupted line rejected as BAD_CMD");
  send("SAVE_MEMORY");
  runUntil(-495);
  CHECK(count("ACK", "cmd", "SAVE_MEMORY") == 1, "SAVE_MEMORY works on the ground");
  setSoe(true);
  runUntil(-490);
  send("SAVE_MEMORY"); send("CLEAR_MEMORY"); send("REBOOT"); send("CAM_OFF");
  runUntil(-489);
  CHECK(count("ERR", "err", "INHIBIT") == 3, "SAVE / CLEAR / REBOOT inhibited in flight");
  CHECK(count("ERR", "err", "STATE") == 1 && pinOut[PIN_CAM_EN], "CAM_OFF refused in flight, cameras stay on");
  setSoe(false);
  runUntil(-480);
  CHECK(lastSt() == "STANDBY", "SOE released before LO -> STANDBY");
  send("REBOOT");
  runUntil(-475);
  CHECK(count("ACK", "cmd", "REBOOT") == 1 && count("PON", nullptr, nullptr) == 2, "REBOOT acknowledged before the reset");
}

static int tvNow() { return pinOut[PIN_TV[0]] | pinOut[PIN_TV[1]] << 1 | pinOut[PIN_TV[2]] << 2; }
static void tv() {
  printf("\n[8] TV channel in flight\n");
  resetWorld();
  powerOn();
  at(SOE, [] { setSoe(true); });
  at(0, [] { setLo(true); });
  runUntil(SOE + 1);
  std::set<int> seen;
  bool stepsOk = true;
  for (int k = 0; k < 8; k++) {
    runUntil(SOE + 1 + k * TV_DWELL_MS / 1000.0 + 5);
    stepsOk &= tvNow() == k;
    seen.insert(tvNow());
  }
  CHECK(TV_DEFAULT_CH != TV_AUTO || (stepsOk && seen.size() == 8), "no command: TV steps through all 8 cameras, %u s each", (unsigned)(TV_DWELL_MS / 1000));
  send("TV 3");
  runUntil(100);
  double t3 = T();
  bool held = true;
  for (int k = 0; k < 6; k++) { runUntil(t3 + k * 10 + 5); held &= tvNow() == 3; }
  CHECK(held && count("ACK", "cmd", "TV") == 1, "TV 3 in flight: holds camera 4 (channel 3)");
  send("TV 8");
  runUntil(t3 + 70);
  int a = tvNow();
  runUntil(t3 + 70 + TV_DWELL_MS / 1000.0);
  CHECK(tvNow() != a, "TV 8: back to cycling");
  int stillOk = 1;
  for (int i = 0; i < 8; i++) stillOk &= cam[i].rec;
  CHECK(stillOk, "switching the TV does not affect recording");
  send("TV 9");
  runUntil(t3 + 90);
  CHECK(count("ERR", "err", "BAD_CMD") == 1, "TV 9 rejected");
  runUntil(PWR_OFF);
  send("TV 2");
  runUntil(PWR_OFF + 1);
  CHECK(count("ERR", "err", "STATE") == 1, "TV refused after shutdown (cameras off)");
}

static void faults() {
  printf("\n[7] Camera and bus faults\n");
  resetWorld();
  cam[2].ignoreStarts = 1;  // misses the first REC_START
  cam[5].dead = true;       // never answers
  powerOn();
  at(SOE, [] { setSoe(true); });
  at(0, [] { setLo(true); });
  at(50, [] { pinIn[PIN_SDA1] = 0; });  // something holds SDA low
  at(52, [] { pinIn[PIN_SDA1] = 1; });
  runUntil(PWR_OFF);
  CHECK(cam[2].recSec > 250, "camera that missed REC_START recorded after a retry (%.0f s)", cam[2].recSec);
  CHECK(count("EVT", "e", "REC_OK_5") == 0, "dead camera is not reported as recording");
  std::string lastCam;
  for (auto &p : pkts) if (field(p.s, "t") == "CAM" && p.t < 200) lastCam = p.s;
  CHECK(field(lastCam, "rec") == std::to_string(0xFF & ~(1 << 5)), "CAM packet rec mask = %s", field(lastCam, "rec").c_str());
  CHECK(count("EVT", "e", "I2C0_RECOVER", 50) >= 1 && evt("I2C0_RECOVER", 50) < 53, "stuck I2C bus recovered (T%+.1f)", evt("I2C0_RECOVER", 50));
  CHECK(near(evt("EXP_END"), T_STOP_CMD, 0.2), "timeline unaffected");

  printf("\n[7b] SD card write errors in flight\n");
  resetWorld();
  powerOn();
  at(SOE, [] { setSoe(true); });
  at(100, [] { sdWriteFails = 100; });
  runUntil(PWR_OFF);
  CHECK(count("ERR", nullptr, nullptr) >= 1, "SD failure reported");
  CHECK(near(evt("EXP_END"), T_STOP_CMD, 0.2), "timeline unaffected");
}

// Flight extras: LED sync flash, microgravity events, flight time in STA, SELF_TEST.
static void extras() {
  printf("\n[9] Flight extras\n");
  resetWorld();
  powerOn();
  at(-500, [] { send("SELF_TEST"); });
  at(-360, [] { send("ENTER_TEST_MODE"); });
  at(-355, [] { send("CAM_ON"); });
  at(-340, [] { send("SELF_TEST"); });
  at(-300, [] { send("EXIT_TEST_MODE"); });
  at(SOE, [] { setSoe(true); });
  at(0, [] { setLo(true); });
  runUntil(PWR_OFF);

  double dark1 = NAN, light1 = NAN, dark2 = NAN;
  for (auto &e : led0Log) {
    if (e.first > SOE + 1 && e.second == 0 && std::isnan(dark1)) dark1 = e.first;
    else if (!std::isnan(dark1) && std::isnan(light1) && e.second > 0) light1 = e.first;
    else if (!std::isnan(light1) && std::isnan(dark2) && e.second == 0) dark2 = e.first;
  }
  double s1 = SOE + LED_SYNC_AT_MS[0] / 1000.0;
  CHECK(near(dark1, s1, 0.05) && near(light1 - dark1, LED_SYNC_OFF_MS / 1000.0, 0.02) && near(evt("SYNC_1"), s1, 0.05),
        "LED sync: dark at T%+.2f for %.0f ms, SYNC_1 event", dark1, (light1 - dark1) * 1000);
  CHECK(near(dark2, T_ALL_OFF, 0.05) && count("EVT", "e", "SYNC_2") == 0, "only one sync flash, LEDs then on until shutdown");

  double u0 = 67 + UG_ENTER_MS / 1000.0, u1 = 240 + UG_EXIT_MS / 1000.0;
  CHECK(near(evt("UG_START"), u0, 0.3) && near(evt("UG_END"), u1, 0.3), "microgravity T%+.1f to T%+.1f (sim profile 67-240 s + hold times)", evt("UG_START"), evt("UG_END"));
  CHECK(count("EVT", "e", "UG_START") == 1, "no false start during burn or spin");

  std::string sta100, staPre;
  size_t maxSta = 0;
  for (auto &p : pkts) if (field(p.s, "t") == "STA") {
    if (p.t < -400 && staPre.empty()) staPre = p.s;
    if (p.t >= 100 && sta100.empty()) sta100 = p.s;
    maxSta = std::max(maxSta, p.s.size() + 1);
  }
  CHECK(field(sta100, "tf") == "145" && field(staPre, "tf") == "-1", "STA tf: %s s at T+100, %s before SOE; STA up to %zu B",
        field(sta100, "tf").c_str(), field(staPre, "tf").c_str(), maxSta);

  std::string sts1, sts2;
  for (auto &p : pkts) if (field(p.s, "t") == "STS") (sts1.empty() ? sts1 : sts2) = p.s;
  CHECK(field(sts1, "ok") == "0" && field(sts1, "s") == "32" && field(sts1, "m") == "15" && field(sts1, "c") == "-1",
        "SELF_TEST on standby: NO-GO (sim has only the IMU), storage OK, cameras not checked");
  CHECK(field(sts2, "c") == "255", "SELF_TEST in test mode with cameras on: all 8 cameras answer");
  CHECK(near(evt("EXP_END"), T_STOP_CMD, 0.2), "timeline unaffected");

  printf("\n[9b] Microgravity: no false starts\n");
  resetWorld();
  powerOn();
  at(SOE, [] { setSoe(true); });
  dipAt = -20; dipLen = 3;  // 3 s near 0 g on the pad (handling, a drop) before LO
  runUntil(-10);
  CHECK(count("EVT", "e", "UG_START") == 0, "3 s at 0 g before LO: ignored");
  setLo(true);
  dipAt = 40; dipLen = UG_ENTER_MS / 2000.0;  // in flight, shorter than the hold time
  runUntil(60);
  CHECK(count("EVT", "e", "UG_START") == 0, "%.1f s dip in flight: ignored", dipLen);
  dipAt = 1e9;
}

int main() {
  flash = (uint8_t *)mmap((void *)(uintptr_t)XIP_BASE, 2 << 20, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (flash != (uint8_t *)(uintptr_t)XIP_BASE) { perror("mmap XIP_BASE"); return 2; }
  printf("Timeline: SOE T%+d s, power-off T%+d s -> record-stop T%+.1f, all off T%+.1f\n", T_SOE_S, T_POWER_OFF_S, T_STOP_CMD, T_ALL_OFF);
  nominal();
  hold();
  wdtInFlight();
  powerLoss();
  testModeForgotten();
  commands();
  faults();
  tv();
  extras();
  printf("\n%d/%d checks passed\n", checks - fails, checks);
  return fails != 0;
}
