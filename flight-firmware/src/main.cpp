// CAPILUX mainboard firmware (SED v3.0 section 4.9, netlist v5)
// v0.3: fixes for issues.md (see the "Status" section there). Values that are not final yet,
// and every behaviour the team still has to decide, are in config.h.
#include <Arduino.h>
#include <SPI.h>
#include <SdFat.h>
#include <Wire.h>
#include <hardware/flash.h>
#include <hardware/watchdog.h>
#include <stdarg.h>
#include <algorithm>
#include "cmd.h"
#include "config.h"
#include "flashrec.h"
#include "flashring.h"
#include "camparse.h"

enum State : uint8_t { BOOT, STANDBY, TEST, ACTIVE, SHUTDOWN, SAFE };
const char *const STATE_NAME[] = {"BOOT", "STANDBY", "TEST", "ACTIVE", "SHUTDOWN", "SAFE"};
// Watchdog scratch 0..2 survive a watchdog reset (pico-sdk only uses 4..7), not a power cut.
constexpr uint32_t MAGIC_ACTIVE = 0xCA710001, MAGIC_DONE = 0xCA710002, MAGIC_REC = 0xCA710003;  // REC: cameras recording before SOE
constexpr uint8_t CAM_REC_START = 0x03, CAM_REC_STOP = 0x04;  // RunCam Device Protocol, camera control

State state = BOOT;
uint32_t tSoe, tLo, tSoeLastOn, tSoeRise, tCamOn, tStop, tClearArmed, tTestOn;
bool recPending, stopping, clearArmed, loSeen, soeWasOn;
uint32_t cmdRx, cmdBad;
bool camOn;
uint8_t led[4];
int tvCh, tvSel;      // tvCh = channel shown now, tvSel = 0-7 fixed or TV_AUTO
uint32_t tTvAuto;     // when automatic cycling (re)started

struct Debounced { int pin; bool on, raw; uint32_t t; };
Debounced soe{PIN_SOE}, lo{PIN_LO};

struct Chamber { float p, t; bool ok, init; uint8_t stale; } ch[4];
float baroP, baroT, acc[3], gyr[3];
bool baroOk, imuOk;

String txq;

bool preflight() { return state == STANDBY || state == TEST || state == SAFE; }

// Time since t. Signed, so a t set after the caller took its own millis() snapshot reads as 0
// instead of wrapping to ~49 days (issue 15).
uint32_t since(uint32_t t) {
  int32_t d = (int32_t)(millis() - t);
  return d > 0 ? (uint32_t)d : 0;
}

// ---------- small helpers ----------
void emit(const char *s) {
  if (txq.length() < TXQ_MAX) { txq += s; txq += '\n'; }  // link down: drop instead of growing forever
  // USB is ground-only; never block on a host that has the port open but is not reading (#21)
  if (Serial && Serial.availableForWrite() >= (int)strlen(s) + 2) Serial.println(s);
}

void pumpTx() {  // non-blocking: never stall the loop on the 38.4k link
  size_t n = 0;
  while (n < txq.length() && Serial1.availableForWrite() > 0) Serial1.write(txq[n++]);
  if (n) txq.remove(0, n);
}

void drainTx(uint32_t ms) {  // blocking, only before a reboot (#16)
  uint32_t t0 = millis();
  while (txq.length() && millis() - t0 < ms) pumpTx();
  Serial1.flush();
}

// "null" in JSON, "" in CSV for invalid values (SED 4.9.7)
const char *fv(char *b, bool ok, float v, const char *nul = "null") {
  if (ok) snprintf(b, 12, "%.2f", v); else strcpy(b, nul);
  return b;
}

void updateInput(Debounced &d) {
  bool raw = digitalRead(d.pin) == LOW;  // open collector: low = asserted
  if (raw != d.raw) { d.raw = raw; d.t = millis(); }
  else if (millis() - d.t >= DEBOUNCE_MS) d.on = raw;
}

// ---------- 595 + camera UART ----------
void write595(uint8_t v) {
  shiftOut(PIN_SER, PIN_SRCLK, MSBFIRST, v);
  digitalWrite(PIN_RCLK, HIGH);
  digitalWrite(PIN_RCLK, LOW);
}

uint8_t crc8(const uint8_t *d, int n) {  // DVB-S2, as used by the RunCam protocol
  uint8_t c = 0;
  while (n--) { c ^= *d++; for (int i = 0; i < 8; i++) c = (c & 0x80) ? (c << 1) ^ 0xD5 : c << 1; }
  return c;
}

void camSend(uint8_t n, uint8_t action) {
  uint8_t p[4] = {0xCC, 0x01, action, 0};
  p[3] = crc8(p, 3);
  write595(n | n << 3);  // same channel on RX and TX mux, both enabled
  Serial2.write(p, 4);
  Serial2.flush();
  write595(MUX_OFF);
}
void camSendAll(uint8_t action) { for (uint8_t n = 0; n < 8; n++) if (CAM_MASK >> n & 1) camSend(n, action); }

// Storage query: GET_SETTINGS (root) to one camera, parse REMAINING_TIME.
// ~20 ms with a reply, at most ~100 ms for a silent camera; one camera per second.
int32_t camRemain[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // seconds of recording left, -1 = unknown
int32_t camPrev[8] = {-1, -1, -1, -1, -1, -1, -1, -1};    // previous reading, for the recording check
uint8_t camPoll, camRecOk, camTries;  // camRecOk bit n = camera n confirmed recording
uint32_t tCamPoll, tCamRetry;

bool camRead(uint8_t *b, size_t n, uint32_t ms) {
  uint32_t t0 = millis();
  size_t got = 0;
  while (got < n && millis() - t0 < ms) {
    if (Serial2.available()) b[got++] = Serial2.read();
    else pumpTx();  // keep the downlink moving while we wait (#10)
  }
  return got == n;
}

int32_t camQueryRemaining(uint8_t n) {
  uint8_t buf[512], frame[3 + 256];
  int total = 0;
  uint8_t chunk = 0, more = 0;
  write595(n | n << 3);
  while (Serial2.available()) Serial2.read();
  do {
    uint8_t req[5] = {0xCC, 0x10, 0x00, chunk, 0};  // GET_SETTINGS, root, chunk index
    req[4] = crc8(req, 4);
    Serial2.write(req, 5);
    if (!camRead(frame, 3, 100) || frame[0] != 0xCC || !camRead(frame + 3, frame[2] + 1, 100) ||
        crc8(frame, 3 + frame[2]) != frame[3 + frame[2]] || total + frame[2] > (int)sizeof buf) {
      total = 0;
      break;
    }
    memcpy(buf + total, frame + 3, frame[2]);
    total += frame[2];
    more = frame[1];
  } while (more && ++chunk < 4);
  write595(MUX_OFF);
  int32_t sec, freeMb, totalMb;
  return total && parseCamStorage(buf, total, sec, freeMb, totalMb) ? sec : -1;
}

void setCam(bool on) {
  camOn = on;
  digitalWrite(PIN_CAM_EN, on);
  if (on) tCamOn = millis();
  else {
    camRecOk = 0;
    for (int i = 0; i < 8; i++) camRemain[i] = camPrev[i] = -1;  // stale once powered off
  }
}
void setLed(int i, uint8_t v) { led[i] = v; analogWrite(PIN_CH_LED[i], v); }
void setTv(int n) { tvCh = n; for (int b = 0; b < 3; b++) digitalWrite(PIN_TV[b], n >> b & 1); }
void selectTv(int sel) { tvSel = sel; tTvAuto = millis(); }

// Every loop pass while the cameras are on: hold the chosen camera, or step through the fitted ones.
void tvStep() {
  if (!camOn) return;
  if (tvSel != TV_AUTO) { if (tvCh != tvSel) setTv(tvSel); return; }
  uint8_t fitted[8], n = 0;
  for (uint8_t i = 0; i < 8; i++) if (CAM_MASK >> i & 1) fitted[n++] = i;
  int ch = n ? fitted[since(tTvAuto) / TV_DWELL_MS % n] : 0;
  if (ch != tvCh) setTv(ch);
}

void allOff() { setCam(false); for (int i = 0; i < 4; i++) setLed(i, 0); }
void ledsAll(uint8_t v) { for (int i = 0; i < 4; i++) setLed(i, v); }

// ---------- camera recording session ----------
// Independent of the state: started by the test at T-6:00 (or by SOE as a fallback) and kept
// running through the countdown and the flight. REC_START goes out once the cameras have booted,
// and is repeated to any camera whose remaining recording time does not drop (#6).
bool camStopping;      // REC_STOP sent, power is cut CAM_STOP_MS later (DVR closes its file)
uint32_t tCamStop;
void camRecStart();
void camStart() {
  camStopping = false;
  if (camOn) return;   // already running: keep the recording and its confirmations
  setCam(true);
  recPending = true;
  camTries = 0;
}
void camStop() {       // graceful: stop recording, cut power later in camService()
  if (!camOn || camStopping) return;
  camSendAll(CAM_REC_STOP);
  camStopping = true;
  recPending = false;
  tCamStop = millis();
}
bool camRecording() { return camOn && !recPending && !camStopping; }

// ---------- SD logging ----------
// Files are created contiguous at full size on the ground, so flight writes only touch data
// sectors (never the FAT or directory). The 4 KB chunk after the write position is always
// zeroed, so the real end of data is the first NUL byte even on a power cut.
// Each file fails on its own: a dead IMU file no longer stops HK (#5).
SdFat32 sd;
bool sdBegun;
struct Log { File32 f; char base[16]; const char *tag; uint32_t size, pos; int part; bool ok; };
Log hk{File32(), "", "HK"}, imu{File32(), "", "IMU"};
bool saved;
uint16_t bootNo = 0xFFFF;
uint32_t logLines, tSdRetry;
const uint8_t ZERO[4096] = {};

bool sdAllOk() { return hk.ok && imu.ok; }

bool zeroChunk(Log &l, uint32_t k) {
  uint32_t at = k * 4096;
  if (at >= l.size) return true;
  uint32_t n = std::min<uint32_t>(4096, l.size - at);
  return l.f.seekSet(at) && l.f.write(ZERO, n) == n;
}

bool logOpen(Log &l) {
  char p[32];
  snprintf(p, sizeof p, l.part ? "%s%d.CSV" : "%s.CSV", l.base, l.part);
  if (!l.f.createContiguous(p, l.size)) return false;
  l.pos = 0;
  return zeroChunk(l, 0) && zeroChunk(l, 1);
}

// Writes are idempotent: the position only advances once the line AND the zeroed chunk ahead
// are both on the card, so a retry rewrites the same bytes in the same place.
bool logWrite(Log &l, const char *s, size_t n) {
  if (l.pos + n > l.size) {
    if (!preflight() && !LOG_ROLLOVER_IN_FLIGHT) return false;  // full in flight: stop, never touch the FAT
    l.f.close();
    l.part++;
    rp2040.wdt_begin(WDT_SLOW_MS);  // allocation scans the FAT
    bool ok = logOpen(l);
    rp2040.wdt_begin(WDT_MS);
    if (!ok) return false;
  }
  if (!l.f.seekSet(l.pos) || l.f.write(s, n) != n) return false;
  uint32_t np = l.pos + n;
  if (np / 4096 != l.pos / 4096 && !zeroChunk(l, np / 4096 + 1)) return false;
  l.pos = np;
  return true;
}

void logFail(Log &l) {  // report once per file; everything else carries on
  if (!l.ok) return;
  l.ok = false;
  char b[64];
  snprintf(b, sizeof b, "{\"t\":\"ERR\",\"ms\":%lu,\"err\":\"SD_FAIL_%s\"}", millis(), l.tag);
  emit(b);
}

bool logf(Log &l, const char *fmt, ...) {
  if (!l.ok) return false;
  char b[256];
  va_list a;
  va_start(a, fmt);
  int n = vsnprintf(b, sizeof b, fmt, a);
  va_end(a);
  if (n <= 0) return false;
  n = std::min(n, (int)sizeof b - 1);
  for (uint8_t i = 0; i < SD_WRITE_TRIES; i++) if (logWrite(l, b, n)) return true;  // GC stall, contact chatter
  logFail(l);
  return false;
}

void logSync() {
  for (Log *l : {&hk, &imu})
    if (l->ok && !l->f.sync() && !l->f.sync()) logFail(*l);
}

bool openLogs() {
  // folders are never deleted, so the first missing number is found by binary search
  char d[12];
  int lo = 0, hi = 10000;
  while (lo < hi) {
    int m = (lo + hi) / 2;
    snprintf(d, sizeof d, "/%04d", m);
    if (sd.exists(d)) lo = m + 1; else hi = m;
  }
  snprintf(d, sizeof d, "/%04d", lo);
  if (lo >= 10000 || !sd.mkdir(d)) return false;
  bootNo = lo;
  snprintf(hk.base, sizeof hk.base, "%s/HK", d);
  snprintf(imu.base, sizeof imu.base, "%s/IMU", d);
  hk.size = HK_FILE_BYTES;
  imu.size = IMU_FILE_BYTES;
  hk.part = imu.part = 0;
  if (!logOpen(hk) || !logOpen(imu)) return false;
  hk.ok = imu.ok = true;
  return logf(hk, "line,ms,state,soe,lo,cam,led1,led2,led3,led4,cmd_rx,cmd_bad,"
                  "ch1_p,ch1_t,ch2_p,ch2_t,ch3_p,ch3_t,ch4_p,ch4_t,bar_p,bar_t,ax,ay,az,gx,gy,gz\n") &&
         logf(imu, "imu_ts,n,ax,ay,az,gx,gy,gz\n");
}

bool sdStart() {
  hk.f.close();
  imu.f.close();
  hk.ok = imu.ok = false;
  if (sdBegun) sd.end();
  sdBegun = sd.begin(SdSpiConfig(PIN_SD_CS, DEDICATED_SPI, SD_SCK_MHZ(20)));
  return sdBegun && openLogs();
}

void logEvent(const char *e) { logf(hk, "#EVT,%lu,%s\n", millis(), e); }  // read with comment='#'

char pk[128];
void event(const char *e) {  // SD log + downlink
  logEvent(e);
  snprintf(pk, sizeof pk, "{\"t\":\"EVT\",\"ms\":%lu,\"e\":\"%s\"}", millis(), e);
  emit(pk);
}

void sdRetry() {  // pre-flight only: a failed card is re-initialised into a new folder (#5)
  if (sdAllOk() || !preflight() || since(tSdRetry) < SD_RETRY_MS) return;
  tSdRetry = millis();
  rp2040.wdt_begin(WDT_SLOW_MS);
  bool ok = sdStart();
  rp2040.wdt_begin(WDT_MS);
  if (ok) event("SD_REINIT");
}

bool saveMem() {  // flush + receipt file (SED 4.9.7); pre-flight only, so a growing file is fine here
  logSync();
  char p[24];
  snprintf(p, sizeof p, "/%04u/SAVE.TXT", bootNo);
  File32 m;
  if (!m.open(p, O_WRONLY | O_CREAT | O_APPEND)) return false;
  char b[48];
  int n = snprintf(b, sizeof b, "%lu,%lu,%lu\n", millis(), (unsigned long)hk.pos, (unsigned long)imu.pos);
  bool ok = m.write(b, n) == (size_t)n;
  return m.close() && ok;
}

// ---------- onboard flash backup log ----------
// Ring buffer of raw records. It never blocks: when the writer enters a sector it erases
// the next one if needed, so there is always room and old data is overwritten oldest-first.
// The erased sector after the write position marks the end of data after a reset.
// Erases take ~45 ms (max 400 ms) every ~6 s in flight; the IMU FIFO covers the pause.
extern "C" char __flash_binary_end;
uint32_t flashWr, eraseAt;  // eraseAt = 0: no CLEAR erase running
bool flashOk;
FlashRec page[4];
uint8_t pageN;

const uint8_t *flashAt(uint32_t ofs) { return (const uint8_t *)(XIP_BASE + ofs); }
void flashHwErase(uint32_t ofs) { noInterrupts(); flash_range_erase(ofs, SECTOR); interrupts(); }
void flashHwProgram(uint32_t ofs, const uint8_t *d) { noInterrupts(); flash_range_program(ofs, d, PAGE); interrupts(); }

void flashInit() {
  flashOk = (uint32_t)&__flash_binary_end - XIP_BASE <= FLASH_LOG_OFS;  // never touch firmware
  flashWr = ringFindEnd();
}

uint32_t flashFreeKb() {  // informational only, nothing depends on it
  uint32_t n = 0;
  for (uint32_t s = FLASH_LOG_OFS; s < FLASH_END; s += SECTOR) n += blank(s, SECTOR);
  return n * SECTOR / 1024;
}

void flashFlushPage() {
  if (!pageN) return;
  memset(&page[pageN], 0xFF, (4 - pageN) * sizeof(FlashRec));  // decoder skips 0xFF records
  pageN = 0;
  if (flashOk) flashWr = ringWrite(flashWr, (const uint8_t *)page);
}

void flashLog() {
  FlashRec &r = page[pageN++];
  r.tSoeMs = millis() - tSoe;
  r.boot = bootNo;
  r.state = state;
  r.flags = soe.on | lo.on << 1 | camOn << 2 | sdAllOk() << 3;
  for (int i = 0; i < 4; i++) {
    r.chP[i] = ch[i].ok ? ch[i].p : NAN;
    r.chT[i] = ch[i].ok ? (int16_t)lroundf(ch[i].t * 100) : INT16_MIN;
  }
  r.barP = baroOk ? baroP : NAN;
  r.barT = baroOk ? (int16_t)lroundf(baroT * 100) : INT16_MIN;
  for (int i = 0; i < 3; i++) { r.acc[i] = imuOk ? acc[i] : NAN; r.gyr[i] = imuOk ? gyr[i] : NAN; }
  r.crc = crc16((const uint8_t *)&r, 62);
  if (pageN == 4) flashFlushPage();
}

// Writes one record with the current state and flushes it now. The last record in the ring
// tells the next boot whether a flight was running (ACTIVE), is done (SHUTDOWN) or was re-armed.
void flashMark() { flashLog(); flashFlushPage(); }

// Last valid record in the ring (CRC checked), searching back a few pages past torn writes.
bool flashLast(FlashRec &out) {
  if (!flashOk) return false;
  uint32_t p = ringLastPage(flashWr);
  for (int k = 0; p && k < 8; k++, p = ringLastPage(p)) {
    for (int i = 3; i >= 0; i--) {
      FlashRec r;
      memcpy(&r, flashAt(p + i * sizeof(FlashRec)), sizeof r);
      if (r.state <= SAFE && crc16((const uint8_t *)&r, 62) == r.crc) { out = r; return true; }
    }
  }
  return false;
}

bool backupFlash() {  // whole ring to SD, rotated so the oldest data comes first
  char p[32];
  for (int i = 0;; i++) {
    snprintf(p, sizeof p, "/%04u/FLASH%d.BIN", bootNo, i);
    if (!sd.exists(p)) break;
    if (i == 99) return false;
  }
  File32 f;
  rp2040.wdt_begin(WDT_SLOW_MS);
  bool ok = f.createContiguous(p, FLASH_LOG_SIZE);
  rp2040.wdt_begin(WDT_MS);
  if (!ok) return false;
  uint32_t s = nextSector(flashWr);  // blank sector right after the newest data
  for (uint32_t i = 0; i < FLASH_LOG_SIZE / SECTOR; i++, s = nextSector(s)) {
    rp2040.wdt_reset();
    pumpTx();
    if (f.write(flashAt(s), SECTOR) != SECTOR) { f.close(); return false; }
  }
  return f.close();
}

void eraseStep() {  // CLEAR: one sector per loop pass so watchdog and telemetry keep running
  if (!eraseAt) return;
  if (!preflight()) { eraseAt = 0; return; }  // SOE wins; the ring erases ahead by itself
  eraseSector(eraseAt);
  eraseAt += SECTOR;
  if (eraseAt >= FLASH_END) { eraseAt = 0; logEvent("FLASH_ERASED"); }
}

// ---------- I2C sensors ----------
uint8_t i2cFails[2];
uint32_t tI2cRecover[2];

// Frees a bus held by a slave that was reset mid-transfer: up to 9 SCL pulses while SDA is low,
// then a STOP. Open-drain emulated with INPUT_PULLUP / OUTPUT LOW (#11).
void i2cClear(int sda, int scl) {
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  delayMicroseconds(10);
  for (int i = 0; i < 9 && digitalRead(sda) == LOW; i++) {
    pinMode(scl, OUTPUT); digitalWrite(scl, LOW); delayMicroseconds(10);
    pinMode(scl, INPUT_PULLUP); delayMicroseconds(10);
  }
  pinMode(sda, OUTPUT); digitalWrite(sda, LOW); delayMicroseconds(10);  // STOP: SDA rises while SCL is high
  pinMode(scl, INPUT_PULLUP); delayMicroseconds(10);
  pinMode(sda, INPUT_PULLUP); delayMicroseconds(10);
}

void i2cBegin(int bus) {
  TwoWire &w = bus ? Wire1 : Wire;
  int sda = bus ? PIN_SDA2 : PIN_SDA1, scl = bus ? PIN_SCL2 : PIN_SCL1;
  i2cClear(sda, scl);
  w.setSDA(sda); w.setSCL(scl); w.setClock(100000); w.setTimeout(10); w.begin();
}

void i2cRecover(int bus) {
  if (since(tI2cRecover[bus]) < 1000) return;  // at most once per second per bus
  tI2cRecover[bus] = millis();
  i2cFails[bus] = 0;
  (bus ? Wire1 : Wire).end();
  i2cBegin(bus);
  for (int i = 0; i < 4; i++) if (CH_BUS[i] == bus) { ch[i].ok = false; ch[i].init = false; }
  if (bus == 0) { imuOk = false; baroOk = false; }  // baroPhase is reset by baroStep on the next error
  event(bus ? "I2C1_RECOVER" : "I2C0_RECOVER");
}

// Recovery only when the bus is really stuck (SDA or SCL held low while idle). A device that is
// simply missing (e.g. an unplugged chamber board) NACKs on a healthy bus; recovering then would
// re-init the IMU and barometer every 2 s and punch holes in the IMU data.
void i2cNote(TwoWire &w, bool ok) {
  int bus = &w == &Wire1;
  if (ok) { i2cFails[bus] = 0; return; }
  if (++i2cFails[bus] < I2C_RECOVER_AFTER) return;
  int sda = bus ? PIN_SDA2 : PIN_SDA1, scl = bus ? PIN_SCL2 : PIN_SCL1;
  if (digitalRead(sda) == LOW || digitalRead(scl) == LOW) i2cRecover(bus);  // the pad reads back in I2C mode too
  else i2cFails[bus] = 0;
}

bool rd(TwoWire &w, uint8_t a, uint8_t reg, uint8_t *b, size_t n) {
  w.beginTransmission(a);
  w.write(reg);
  bool ok = w.endTransmission(false) == 0 && w.requestFrom(a, n) == n;
  if (ok) for (size_t i = 0; i < n; i++) b[i] = w.read();
  i2cNote(w, ok);
  return ok;
}
bool wr(TwoWire &w, uint8_t a, uint8_t reg, uint8_t v) {
  w.beginTransmission(a); w.write(reg); w.write(v);
  bool ok = w.endTransmission() == 0;
  i2cNote(w, ok);
  return ok;
}

// LPS27HHW. STATUS is read with the data: a chip that reset back to power-down still answers
// but never sets P_DA/T_DA again, so a run of reads without new data forces a re-init (#13).
void readChamber(int i) {
  TwoWire &w = CH_BUS[i] ? Wire1 : Wire;
  Chamber &c = ch[i];
  uint8_t a = CH_ADDR[i], b[6], id;
  if (!c.init) {
    if (!(rd(w, a, 0x0F, &id, 1) && id == 0xB3 && wr(w, a, 0x10, 0x22))) return;  // 10 Hz, BDU
    c.init = true;
    c.stale = 0;
  }
  if (!rd(w, a, 0x27, b, 6)) { c.ok = c.init = false; return; }  // STATUS, PRESS_XL..H, TEMP_L..H
  if (!(b[0] & 0x03)) {
    if (++c.stale >= CH_STALE_READS) c.ok = c.init = false;
    return;
  }
  c.stale = 0;
  int32_t p = (int32_t)((uint32_t)b[3] << 24 | (uint32_t)b[2] << 16 | (uint32_t)b[1] << 8) >> 8;
  c.p = p / 4096.0f;
  c.t = (int16_t)(b[5] << 8 | b[4]) / 100.0f;
  c.ok = true;
}

// LSM6DSO32 with FIFO: 104 Hz, +-32 g (0.976 mg/LSB), +-2000 dps (70 mdps/LSB).
// The chip timestamps every 8th sample (25 us ticks); each IMU line carries the last
// timestamp and the sample count since it. #SYNC lines map IMU time to millis().
uint32_t imuTs, tImuData;
uint16_t imuN;
uint8_t imuHave;

bool imuInit() {
  uint8_t id;
  int8_t fine;
  bool ok = rd(Wire, ADDR_IMU, 0x0F, &id, 1) && id == 0x6C &&
            wr(Wire, ADDR_IMU, 0x12, 0x44) &&  // CTRL3_C: BDU, auto-increment
            wr(Wire, ADDR_IMU, 0x10, 0x44) &&  // CTRL1_XL: 104 Hz, +-32 g
            wr(Wire, ADDR_IMU, 0x11, 0x4C) &&  // CTRL2_G: 104 Hz, 2000 dps
            wr(Wire, ADDR_IMU, 0x19, 0x20) &&  // CTRL10_C: timestamp counter on
            wr(Wire, ADDR_IMU, 0x09, 0x44) &&  // FIFO_CTRL3: batch accel + gyro at 104 Hz
            wr(Wire, ADDR_IMU, 0x0A, 0x00) &&  // FIFO_CTRL4: bypass (clears FIFO)
            wr(Wire, ADDR_IMU, 0x0A, 0x86) &&  // FIFO_CTRL4: timestamp every 8th sample, continuous
            rd(Wire, ADDR_IMU, 0x63, (uint8_t *)&fine, 1);  // INTERNAL_FREQ_FINE: 0.15 %/LSB clock error
  if (ok) {
    logf(imu, "#FREQ_FINE,%lu,%d\n", millis(), fine);
    tImuData = millis();
    imuHave = 0;
  }
  return ok;
}

void imuDrain() {
  if (!imuOk && !(imuOk = imuInit())) return;
  uint8_t s[2], w[7];
  if (!rd(Wire, ADDR_IMU, 0x3A, s, 2)) { imuOk = false; imuHave = 0; return; }  // FIFO_STATUS1/2
  if (s[1] & 0x40) { logf(imu, "#OVR,%lu\n", millis()); imuHave = 0; }  // re-pair after an overrun (#20)
  uint16_t n = (s[1] & 0x03) << 8 | s[0];
  if (!n) {  // a reset LSM6DSO32 comes back with the FIFO in bypass: count stays 0 forever (#13)
    if (since(tImuData) >= IMU_STALE_MS) { imuOk = false; logf(imu, "#STALE,%lu\n", millis()); }
    return;
  }
  tImuData = millis();
  for (; n; n--) {
    if (!rd(Wire, ADDR_IMU, 0x78, w, 7)) { imuOk = false; imuHave = 0; return; }  // tag + 6 data bytes
    int16_t v[3] = {(int16_t)(w[2] << 8 | w[1]), (int16_t)(w[4] << 8 | w[3]), (int16_t)(w[6] << 8 | w[5])};
    switch (w[0] >> 3) {
      case 0x01: for (int i = 0; i < 3; i++) gyr[i] = v[i] * 0.070f; imuHave |= 1; break;
      case 0x02: for (int i = 0; i < 3; i++) acc[i] = v[i] * 0.000976f; imuHave |= 2; break;
      case 0x04: imuTs = w[1] | w[2] << 8 | w[3] << 16 | (uint32_t)w[4] << 24; imuN = 0; break;
    }
    if (imuHave == 3) {
      imuHave = 0;
      logf(imu, "%lu,%u,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f\n", imuTs, imuN++, acc[0], acc[1], acc[2], gyr[0], gyr[1], gyr[2]);
    }
  }
}

void imuSync() {  // 1 Hz pair of (millis, IMU timestamp) for post-flight time alignment
  uint8_t t[4];
  if (imuOk && rd(Wire, ADDR_IMU, 0x40, t, 4))
    logf(imu, "#SYNC,%lu,%lu\n", millis(), t[0] | t[1] << 8 | t[2] << 16 | (uint32_t)t[3] << 24);
}

// MS5611, non-blocking: start D1, read it 10 ms later and start D2, read D2 10 ms later.
// msD1, not D1: the Arduino core already defines D1 as a pin name.
uint16_t C[7];
uint32_t msD1, baroT0;
int baroPhase;  // 0 = needs init, 1 = start D1, 2 = D1 running, 3 = D2 running

bool msCmd(uint8_t c) {
  Wire.beginTransmission(ADDR_BARO); Wire.write(c);
  bool ok = Wire.endTransmission() == 0;
  i2cNote(Wire, ok);
  return ok;
}
bool msRead(uint8_t c, uint8_t n, uint32_t &v) {
  if (!msCmd(c)) return false;
  bool ok = Wire.requestFrom(ADDR_BARO, (size_t)n) == n;
  i2cNote(Wire, ok);
  if (!ok) return false;
  v = 0;
  while (n--) v = v << 8 | Wire.read();
  return true;
}

void baroCompute(uint32_t D2) {  // datasheet with 2nd order compensation (cold soak)
  int32_t dT = D2 - ((int32_t)C[5] << 8);
  int32_t T = 2000 + ((int64_t)dT * C[6] >> 23);
  int64_t off = ((int64_t)C[2] << 16) + ((int64_t)C[4] * dT >> 7);
  int64_t sens = ((int64_t)C[1] << 15) + ((int64_t)C[3] * dT >> 8);
  if (T < 2000) {
    int64_t x = T - 2000, off2 = 5 * x * x / 2, sens2 = 5 * x * x / 4;
    if (T < -1500) { int64_t y = T + 1500; off2 += 7 * y * y; sens2 += 11 * y * y / 2; }
    T -= (int64_t)dT * dT >> 31; off -= off2; sens -= sens2;
  }
  baroP = (((((int64_t)msD1 * sens) >> 21) - off) >> 15) / 100.0f;  // (D1*SENS/2^21 - OFF) / 2^15
  baroT = T / 100.0f;
}

void baroStep() {
  uint32_t now = millis(), v;
  if (baroPhase == 0) {
    if (!msCmd(0x1E)) return;
    delay(3);  // reset reload time
    for (int i = 1; i <= 6; i++) { if (!msRead(0xA0 + 2 * i, 2, v)) return; C[i] = v; }
    baroPhase = 1;
  }
  if (baroPhase == 1) { if (msCmd(0x48)) { baroT0 = now; baroPhase = 2; } else { baroOk = false; baroPhase = 0; } return; }
  if (now - baroT0 < 10) return;
  if (baroPhase == 2) {
    if (!msRead(0x00, 3, msD1) || !msCmd(0x58)) { baroOk = false; baroPhase = 0; return; }
    baroT0 = now; baroPhase = 3;
  } else {
    if (!msRead(0x00, 3, v) || !msD1 || !v) { baroOk = false; baroPhase = 0; return; }
    baroCompute(v); baroOk = true; baroPhase = 1;
  }
}

void logHk() {
  char s[18][12];
  int k = 0;
  for (int i = 0; i < 4; i++) { fv(s[k++], ch[i].ok, ch[i].p, ""); fv(s[k++], ch[i].ok, ch[i].t, ""); }
  fv(s[k++], baroOk, baroP, ""); fv(s[k++], baroOk, baroT, "");
  for (int i = 0; i < 3; i++) fv(s[k++], imuOk, acc[i], "");
  for (int i = 0; i < 3; i++) fv(s[k++], imuOk, gyr[i], "");
  logf(hk, "%lu,%lu,%s,%d,%d,%d,%d,%d,%d,%d,%lu,%lu,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",
       ++logLines, millis(), STATE_NAME[state], soe.on, lo.on, camOn, led[0], led[1], led[2], led[3], cmdRx, cmdBad,
       s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13], s[14], s[15], s[16], s[17]);
  imuSync();
  logSync();  // only data sectors change, so a power cut mid-sync costs at most this second
}

// ---------- telemetry (SED tables 4.27/4.28) ----------
void sendSta() {
  int n = snprintf(pk, sizeof pk, "{\"t\":\"STA\",\"ms\":%lu,\"st\":\"%s\",\"soe\":%d,\"lo\":%d,\"cam\":%d",
                   millis(), STATE_NAME[state], soe.on, lo.on, camOn);
  // tf = whole seconds since SOE, -1 when not flying
  n += snprintf(pk + n, sizeof pk - n, ",\"tf\":%ld", state == ACTIVE ? (long)((millis() - tSoe) / 1000) : -1L);
  snprintf(pk + n, sizeof pk - n, "}");
  emit(pk);
}
void sendAct() {
  snprintf(pk, sizeof pk, "{\"t\":\"ACT\",\"ms\":%lu,\"led\":[%d,%d,%d,%d],\"rx\":%lu,\"bad\":%lu}",
           millis(), led[0], led[1], led[2], led[3], cmdRx, cmdBad);
  emit(pk);
}
float vsys() { return analogRead(PIN_VSYS_ADC) * 3.0f * 3.3f / 4095.0f; }
void sendHk() {
  int ns = baroOk + imuOk;
  for (auto &c : ch) ns += c.ok;
  float v = vsys();
  snprintf(pk, sizeof pk, "{\"t\":\"HK\",\"ms\":%lu,\"ok\":%d,\"v\":%.2f,\"ns\":%d,\"sd\":%d}",
           millis(), ns == 6 && sdAllOk() && flashOk && v > 4.5f && v < 5.5f, v, ns, sdAllOk());
  emit(pk);
}
void sendCh(int pair) {  // CH1 = chambers 1/2, CH2 = chambers 3/4
  char a[12], b[12], c[12], d[12];
  Chamber &x = ch[2 * pair], &y = ch[2 * pair + 1];
  snprintf(pk, sizeof pk, "{\"t\":\"CH%d\",\"ms\":%lu,\"p\":[%s,%s],\"T\":[%s,%s]}", pair + 1, millis(),
           fv(a, x.ok, x.p), fv(b, y.ok, y.p), fv(c, x.ok, x.t), fv(d, y.ok, y.t));
  emit(pk);
}
void sendBar() {
  char a[12], b[12];
  snprintf(pk, sizeof pk, "{\"t\":\"BAR\",\"ms\":%lu,\"p\":%s,\"T\":%s}", millis(), fv(a, baroOk, baroP), fv(b, baroOk, baroT));
  emit(pk);
}
// IMU split in two so each packet stays under the 64-byte RXSM recommendation (worst case 60 B)
void sendImu() {
  const char *tag[2] = {"ACC", "GYR"}, *key[2] = {"a", "g"};
  const char *fmt = imuOk ? "{\"t\":\"%s\",\"ms\":%lu,\"%s\":[%.2f,%.2f,%.2f]}" : "{\"t\":\"%s\",\"ms\":%lu,\"%s\":null}";
  for (int k = 0; k < 2; k++) {
    float *v = k ? gyr : acc;
    snprintf(pk, sizeof pk, fmt, tag[k], millis(), key[k], v[0], v[1], v[2]);
    emit(pk);
  }
}
void sendMem() {  // bt = SD boot folder, n = HK lines, fl = free flash KB, er = flash erase running
  snprintf(pk, sizeof pk, "{\"t\":\"MEM\",\"sd\":%d,\"sav\":%d,\"bt\":%u,\"n\":%lu,\"fl\":%lu,\"er\":%d}",
           sdAllOk(), saved, bootNo, logLines, flashFreeKb(), eraseAt != 0);
  emit(pk);
}
void sendCam() {  // remaining recording time per camera in minutes (-1 = no reply / off), rec = confirmed recording mask
  long m[8];
  for (int i = 0; i < 8; i++) m[i] = camRemain[i] < 0 ? -1 : camRemain[i] / 60;
  snprintf(pk, sizeof pk, "{\"t\":\"CAM\",\"rt\":[%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld],\"rec\":%u}",
           m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], camRecOk);
  emit(pk);
  if (camOn) logf(hk, "#CAM,%lu,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%u\n", millis(), m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], camRecOk);
}
void sendObs() {
  snprintf(pk, sizeof pk, "{\"t\":\"OBS\",\"ms\":%lu,\"cam\":%d,\"tv\":%d,\"ta\":%d,\"led\":[%d,%d,%d,%d]}",
           millis(), camOn, tvCh, tvSel == TV_AUTO, led[0], led[1], led[2], led[3]);  // ta = TV cycling
  emit(pk);
}

// SELF_TEST: one GO / NO-GO packet.
// s: bit0-3 chamber 1-4, bit4 barometer, bit5 IMU (63 = all)
// m: bit0 SD HK file, bit1 SD IMU file, bit2 flash ring, bit3 supply 4.5-5.5 V (15 = all)
// c: cameras that answered their last storage poll, r: cameras confirmed recording
//    (both -1 = cameras off, not checked; the final check at SOE counts that as NO-GO)
bool sendSelfTest(bool final = false) {
  unsigned s = baroOk << 4 | imuOk << 5, m = hk.ok | imu.ok << 1 | flashOk << 2;
  for (int i = 0; i < 4; i++) s |= ch[i].ok << i;
  float v = vsys();
  m |= (v > 4.5f && v < 5.5f) << 3;
  int c = -1, r = -1;
  if (camOn) { c = 0; for (int i = 0; i < 8; i++) c |= (camRemain[i] >= 0) << i; r = camRecOk; }
  bool camsOk = c < 0 ? !final : (c & CAM_MASK) == CAM_MASK && (r & CAM_MASK) == CAM_MASK;
  bool ok = s == 63 && m == 15 && camsOk;
  snprintf(pk, sizeof pk, "{\"t\":\"STS\",\"ms\":%lu,\"ok\":%d,\"s\":%u,\"m\":%u,\"c\":%d,\"r\":%d}", millis(), ok, s, m, c, r);
  emit(pk);
  logf(hk, "#STS,%lu,%d,%u,%u,%d,%d\n", millis(), ok, s, m, c, r);
  return ok;
}

// ---------- state machine ----------
void setState(State s) {
  state = s;
  logEvent(STATE_NAME[s]);
  sendSta();
}

// LED sync: dark frames at fixed times since SOE, logged as SYNC_n events.
constexpr int N_SYNC = sizeof LED_SYNC_AT_MS / sizeof LED_SYNC_AT_MS[0];
int syncIdx;
bool syncDark;
uint32_t tSync;
void syncInit() {  // a resume skips the marks that are already past
  uint32_t el = millis() - tSoe;
  syncIdx = 0;
  while (syncIdx < N_SYNC && LED_SYNC_AT_MS[syncIdx] <= el) syncIdx++;
  syncDark = false;
}
void syncStep(uint32_t el) {
  if (stopping || syncIdx >= N_SYNC) return;
  if (!syncDark && el >= LED_SYNC_AT_MS[syncIdx]) {
    for (int i = 0; i < 4; i++) setLed(i, 0);
    syncDark = true;
    tSync = millis();
    char e[10];
    snprintf(e, sizeof e, "SYNC_%d", syncIdx + 1);
    event(e);  // the logged millis is the moment the LEDs went dark
  } else if (syncDark && since(tSync) >= LED_SYNC_OFF_MS) {
    for (int i = 0; i < 4; i++) setLed(i, LED_FLIGHT);
    syncDark = false;
    syncIdx++;
  }
}

// Microgravity start / end from the acceleration magnitude, with hysteresis and hold times.
bool inUg;
uint32_t tUgCand;
void ugInit() { inUg = false; tUgCand = millis(); }
void ugStep() {
  if (state != ACTIVE || !loSeen || !imuOk) { tUgCand = millis(); return; }
  float g = sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
  bool flip = inUg ? g > UG_EXIT_G : g < UG_ENTER_G;
  if (!flip) { tUgCand = millis(); return; }
  if (since(tUgCand) >= (inUg ? UG_EXIT_MS : UG_ENTER_MS)) {
    inUg = !inUg;
    tUgCand = millis();
    event(inUg ? "UG_START" : "UG_END");
  }
}

void enterActive() {
  camStart();          // normally already recording since the test; this is the fallback (e.g. no uplink)
  ledsAll(LED_FLIGHT);
  stopping = false;
  camTries = 0;        // fresh retries for any camera that is still not confirmed
  tSoeLastOn = millis();  // a resume starts with SOE debounced "off": do not read that as a release
  selectTv(TV_DEFAULT_CH);
  syncInit();
  ugInit();
  setState(ACTIVE);
}

// Start the flight. t0 = when SOE was (or should have been) given.
void startFlight(uint32_t t0, const char *why) {
  tSoe = t0;
  event(why);
  enterActive();
  event(sendSelfTest(true) ? "GO" : "NOGO");  // final checks at SOE: sensors, storage, all cameras recording
}

void camRecStart() {
  for (int i = 0; i < 8; i++) camPrev[i] = -1;  // recording is confirmed by the next two polls
  camRecOk = 0;
  camSendAll(CAM_REC_START);
  tCamRetry = millis();
}

// Called after every storage poll: recording is confirmed when the remaining time drops.
void camCheck(uint8_t n, int32_t r) {
  bool expect = camRecording();
  if (expect && camPrev[n] >= 0 && r >= 0 && r < camPrev[n] && !(camRecOk >> n & 1)) {
    camRecOk |= 1 << n;
    char e[12];
    snprintf(e, sizeof e, "REC_OK_%u", n);
    event(e);
  }
  camPrev[n] = r;
}

// Every loop pass: REC_START after boot, delayed power cut after REC_STOP, closed-loop retries.
void camService() {
  if (camOn && recPending && since(tCamOn) >= CAM_BOOT_MS) { camRecStart(); recPending = false; }
  if (camStopping && since(tCamStop) >= CAM_STOP_MS) { setCam(false); camStopping = false; }
  uint8_t missing = CAM_MASK & ~camRecOk;
  if (CAM_REC_CONFIRM && camRecording() && missing && camTries < CAM_MAX_RETRIES && since(tCamRetry) >= CAM_RETRY_MS) {
    for (uint8_t n = 0; n < 8; n++) if (missing >> n & 1) camSend(n, CAM_REC_START);  // idempotent
    camTries++;
    tCamRetry = millis();
    logf(hk, "#CAMRETRY,%lu,%u,%u\n", millis(), camTries, missing);
  }
}

void runActive() {
  uint32_t el = millis() - tSoe;
  if (!stopping) {
    // SOE released before LO (hold / recycle): back to STANDBY and re-arm. The cameras keep
    // recording, as they do before SOE; the next SOE starts a fresh flight clock.
    if (SOE_DROP_ABORTS && !loSeen && since(tSoeLastOn) >= SOE_DROP_MS) {
      event("SOE_RELEASED");
      if (camOn) ledsAll(LED_FLIGHT);  // in case it happened during the sync flash
      setState(STANDBY);
      flashMark();
      return;
    }
    bool end;
    if (END_FROM_LO) end = loSeen ? since(tLo) >= T_EXP_END_FROM_LO_MS : el >= T_EXP_END_MS + HOLD_ALLOWANCE_MS;
    else end = el >= T_EXP_END_MS;
    if (end) {
      camStop();
      stopping = true;
      tStop = millis();
      event("EXP_END");
    }
  }
  if (stopping && since(tStop) >= CAM_STOP_MS) {
    allOff();
    camStopping = false;
    setState(SHUTDOWN);
    flashMark();  // marks the flight done
    logSync();
    return;
  }
  syncStep(el);
}

void persist() {
  watchdog_hw->scratch[0] = state == ACTIVE ? MAGIC_ACTIVE : state == SHUTDOWN ? MAGIC_DONE
                           : camOn && !camStopping ? MAGIC_REC : 0;
  watchdog_hw->scratch[1] = millis() - tSoe;
}

// End of the test (EXIT_TEST_MODE or timeout): cameras keep recording through the countdown,
// LEDs go to the flight level. Cameras that were switched off in the test stay off (SOE starts them).
void endTest() {
  ledsAll(camOn && !camStopping ? LED_FLIGHT : 0);
  setState(STANDBY);
  if (camOn && !camStopping) event("PREFLIGHT_REC");
}

// ---------- commands ----------
void handle(const char *line) {
  Parsed c = parseCmd(line);
  const char *err = nullptr;
  cmdRx++;
  switch (c.id) {
    case C_BAD: err = "BAD_CMD"; break;
    case C_PING: break;
    case C_TEST_ON:  // the pre-flight test (T-6:00): cameras on and recording, LEDs on, then check
      if (state != STANDBY) { err = "STATE"; break; }
      tTestOn = millis();
      if (!camOn) selectTv(TV_DEFAULT_CH);
      camStart();
      ledsAll(LED_FLIGHT);
      setState(TEST);
      break;
    case C_TV:  // view only (does not touch recording): allowed whenever the cameras are on
      if (state == TEST || camOn) { selectTv(c.a); tvStep(); } else err = "STATE";
      break;
    case C_TEST_OFF: if (state == TEST) endTest(); else err = "STATE"; break;
    case C_STATUS: sendSta(); sendAct(); break;
    case C_HK: sendHk(); sendCh(0); sendCh(1); sendBar(); sendImu(); break;
    case C_OBS: sendObs(); sendCam(); break;
    case C_MEM: sendMem(); break;
    case C_SAVE:  // ground only: it writes a growing file, i.e. touches the FAT (#8)
      if (!preflight()) err = "INHIBIT";
      else if (!sdAllOk()) err = "NO_SD";
      else if (!saveMem()) err = "SAVE_FAIL";
      else saved = true;
      break;
    case C_CLEAR:  // erases the onboard flash log only; SD files are never deleted
      if (!preflight()) { err = "INHIBIT"; break; }
      clearArmed = true; tClearArmed = millis();
      snprintf(pk, sizeof pk, "{\"t\":\"CFR\",\"ms\":%lu,\"cmd\":\"CLEAR_MEMORY\",\"win\":%lu}", millis(), CLEAR_WINDOW_MS / 1000);
      emit(pk);
      return;
    case C_CLEAR_OK:
      if (!preflight()) err = "INHIBIT";
      else if (!clearArmed || since(tClearArmed) > CLEAR_WINDOW_MS) err = "NOT_ARMED";
      else if (!flashOk) err = "NO_FLASH";
      else if (!sdAllOk()) err = "NO_SD";  // no backup possible, so no erase (the ring still works)
      else if (!backupFlash()) err = "BACKUP_FAIL";
      else { logEvent("FLASH_BACKUP"); flashWr = FLASH_LOG_OFS; eraseAt = FLASH_LOG_OFS; }
      clearArmed = false;
      break;
    case C_SAFE_ON: if (state == STANDBY || state == TEST) { camStop(); ledsAll(0); setState(SAFE); } else err = "STATE"; break;
    case C_SAFE_OFF: if (state == SAFE) setState(STANDBY); else err = "STATE"; break;
    case C_REARM:  // leave the flight-done latch; a still-asserted SOE starts a new run at once (#3)
      if (state != SHUTDOWN) { err = "STATE"; break; }
      loSeen = false;
      tSoeRise = millis();  // a still-asserted SOE starts a NEW run, timed from now
      setState(STANDBY);
      flashMark();  // newest record is STANDBY now, so a later boot does not latch again
      break;
    case C_SELF_TEST:  // read-only, any state
      sendSelfTest();
      break;
    case C_REBOOT:
      if (state == ACTIVE) { err = "INHIBIT"; break; }  // a reset cuts camera power through CAM_EN
      emit("{\"t\":\"ACK\",\"cmd\":\"REBOOT\"}");
      drainTx(300);
      logSync();
      watchdog_hw->scratch[0] = state == SHUTDOWN ? MAGIC_DONE : camOn && !camStopping ? MAGIC_REC : 0;  // never re-arms a finished flight; keeps a pre-flight recording going
      rp2040.reboot();
      break;
    default:  // camera and LED control: ground test mode only (O.2)
      if (state != TEST) { err = "STATE"; break; }
      if (c.id == C_CAM_ON) camStart();
      else if (c.id == C_CAM_OFF) camStop();  // graceful: files are closed before power is cut
      else if (c.id == C_LED_ALL) for (int i = 0; i < 4; i++) setLed(i, c.a);
      else if (c.id == C_LED) setLed(c.a - 1, c.b);
      else if (c.id == C_LED_OFF) for (int i = 0; i < 4; i++) setLed(i, 0);
  }
  if (err) {
    cmdBad++;
    snprintf(pk, sizeof pk, "{\"t\":\"ERR\",\"ms\":%lu,\"err\":\"%s\"}", millis(), err);
  } else {
    char w[24] = {0};
    strncpy(w, line, strcspn(line, " ") < 23 ? strcspn(line, " ") : 23);
    snprintf(pk, sizeof pk, "{\"t\":\"ACK\",\"ms\":%lu,\"cmd\":\"%s\"}", millis(), w);
  }
  emit(pk);
}

// A line with any byte outside printable ASCII (bit error, NUL) is rejected whole, so a
// corrupted line can never be matched by the parser (#17).
struct LineIn { Stream &s; char buf[64]; uint8_t n; bool over, bad; };
LineIn uplink{Serial1}, usb{Serial};

void pollLine(LineIn &in) {
  while (in.s.available()) {
    char c = in.s.read();
    if (c == '\r') continue;
    if (c == '\n') {
      in.buf[in.n] = 0;
      if (in.over || in.bad) handle("");  // malformed: counted as BAD_CMD
      else if (in.n) handle(in.buf);
      in.n = 0; in.over = in.bad = false;
    } else if (c < 0x20 || c > 0x7E) in.bad = true;
    else if (in.n < sizeof in.buf - 1) in.buf[in.n++] = c;
    else in.over = true;
  }
}

// ---------- main ----------
// SOE or LO held asserted for 60 ms at power-up (raw pins, before the debouncers run).
void readSignalsAtBoot(bool &soeLow, bool &loLow) {
  soeLow = loLow = true;
  for (uint32_t t0 = millis(); millis() - t0 < 60;) {
    soeLow &= digitalRead(PIN_SOE) == LOW;
    loLow &= digitalRead(PIN_LO) == LOW;
  }
}

void setup() {
  // Watchdog reset in flight: camera power back on before anything else. The pin is low from
  // reset until here (a few ms); a hold-up cap on Q2's gate keeps the cameras running across it.
  uint32_t tBoot = millis();  // the resumed clock counts everything from here, incl. the SD setup
  bool wdtReset = watchdog_caused_reboot();
  uint32_t magic = wdtReset ? watchdog_hw->scratch[0] : 0;
  uint32_t elapsed = watchdog_hw->scratch[1], tries = wdtReset ? watchdog_hw->scratch[2] : 0;
  bool resumeWdt = magic == MAGIC_ACTIVE, resumeRec = magic == MAGIC_REC;
  pinMode(PIN_CAM_EN, OUTPUT); digitalWrite(PIN_CAM_EN, resumeWdt || resumeRec);

  // 595 next: OE has no pull-up, so outputs come up enabled with random bits. Disable both muxes.
  pinMode(PIN_OE, OUTPUT); digitalWrite(PIN_OE, HIGH);
  for (int p : {PIN_SER, PIN_SRCLK, PIN_RCLK}) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }
  write595(MUX_OFF);
  digitalWrite(PIN_OE, LOW);

  analogWriteFreq(20000);  // well above camera frame rate, no banding
  analogWriteRange(255);
  for (int i = 0; i < 4; i++) setLed(i, resumeWdt || resumeRec ? LED_FLIGHT : 0);  // lights back before the slow SD setup
  for (int p : PIN_TV) pinMode(p, OUTPUT);
  setTv(0);
  pinMode(PIN_SOE, INPUT); pinMode(PIN_LO, INPUT);  // external 10k pull-ups
  analogReadResolution(12);

  Serial.begin(115200);
  Serial1.setTX(0); Serial1.setRX(1); Serial1.setFIFOSize(UPLINK_RX_FIFO); Serial1.begin(38400);  // #9
  Serial2.setTX(PIN_CAM_TX); Serial2.setRX(PIN_CAM_RX); Serial2.setFIFOSize(CAM_RX_FIFO); Serial2.begin(115200);
  i2cBegin(0);  // includes bus recovery, in case a slave was left holding SDA (#11)
  i2cBegin(1);
  flashInit();

  // ---- What were we doing before this boot? (#3, #12) ----
  bool soeLow, loLow;
  readSignalsAtBoot(soeLow, loLow);
  bool sig = soeLow || loLow;  // still in the flight sequence (RXSM signals stay asserted until power-off)
  enum { R_NONE, R_WDT, R_POWER, R_DONE } r = resumeWdt ? R_WDT : magic == MAGIC_DONE && sig && LATCH_FLIGHT_DONE ? R_DONE : R_NONE;
  // The hang before the reset is counted at its worst case: 4 s normally, 8 s if the previous resume
  // hung inside the SD setup. This can only move the stop earlier, which is the safe direction.
  uint32_t el = r == R_WDT ? elapsed + (tries ? WDT_SLOW_MS : WDT_MS) : 0;
  FlashRec last;
  if (r == R_NONE && sig && flashLast(last)) {
    // records reach flash in pages of 4 (<= 400 ms old) + the unknown outage
    if (last.state == ACTIVE && RESUME_AFTER_POWER_LOSS) { r = R_POWER; el = last.tSoeMs + 4 * FLASH_REC_MS + RESUME_ASSUMED_OUTAGE_MS; }
    else if (LATCH_FLIGHT_DONE && (last.state == ACTIVE || last.state == SHUTDOWN)) r = R_DONE;
  }
  if (r == R_WDT || r == R_POWER) {
    tSoe = tBoot - el;  // continue the original SOE clock
    if (loLow) { loSeen = true; tLo = tSoe + (uint32_t)(-T_SOE_S * 1000); }  // LO time estimated from SOE
    if (el >= T_EXP_END_MS) { r = R_DONE; allOff(); }  // nothing left to record
    else {
      enterActive();
      persist();
      watchdog_hw->scratch[2] = tries + 1;
      rp2040.wdt_begin(WDT_SLOW_MS);  // in flight the state machine must come back even if the card hangs (#14)
    }
  }

  // SdFat32 = FAT16/FAT32 only: format 64 GB cards as FAT32.
  // On a ground boot this runs without the watchdog (folder search + 94 MB allocation can take a
  // few seconds on a well-used card). On a resume it runs under the 8 s watchdog; if the previous
  // resume never got past this point, the card is skipped and the flash ring carries the flight.
  bool resuming = state == ACTIVE;
  if (!(resuming && tries >= 1))
    for (uint8_t i = 0; i < SD_BOOT_TRIES && !sdStart(); i++) rp2040.wdt_reset();
  watchdog_hw->scratch[2] = 0;
  rp2040.wdt_begin(WDT_MS);
  emit("{\"t\":\"PON\",\"fw\":\"capilux-0.3\"}");
  if (!sdAllOk()) emit("{\"t\":\"ERR\",\"err\":\"SD_FAIL\"}");

  if (r == R_WDT) event("RESUME_WDT");
  else if (r == R_POWER) event("RESUME_POWER");
  else if (r == R_DONE) { setState(SHUTDOWN); event("FLIGHT_DONE_LATCHED"); }
  else {
    setState(STANDBY);
    if (resumeRec) { camStart(); event("RESUME_REC"); }  // reset during the pre-flight recording: keep recording
  }
}

void loop() {
  static uint32_t tImu, tCh, tTm, tLog, tFlash;
  rp2040.wdt_reset();
  updateInput(soe);
  updateInput(lo);
  pollLine(uplink);
  pollLine(usb);
  uint32_t now = millis();
  if (soe.on) tSoeLastOn = now;
  if (soe.on && !soeWasOn) tSoeRise = soe.t;  // when the pin really went low (before the debounce)
  soeWasOn = soe.on;
  if (lo.on && !loSeen) { loSeen = true; tLo = now; event("LO"); }

  if (state == TEST && TEST_TIMEOUT_MS && since(tTestOn) >= TEST_TIMEOUT_MS) {  // #7
    endTest();
    event("TEST_TIMEOUT");
  }
  // SOE starts the flight from STANDBY or SAFE, not from TEST (SED 4.9.6). SOE is checked as a
  // level, so if it is already asserted when test mode ends, the flight starts right then.
  // The flight is always timed from the SOE edge, not from when it was noticed: SOE seen late
  // (test mode still on, TEST_TIMEOUT) must not push the end of the experiment past power-off.
  if (soe.on && (state == STANDBY || state == SAFE)) startFlight(tSoeRise, since(tSoeRise) > 1000 ? "SOE_LATE" : "SOE");
  else if (LO_BACKUP_START && lo.on && (state == STANDBY || state == SAFE || state == TEST))
    startFlight(soe.on ? tSoeRise : now - (uint32_t)(-T_SOE_S * 1000), "LO_BACKUP_START");  // else as if SOE came on time
  if (clearArmed && since(tClearArmed) > CLEAR_WINDOW_MS) clearArmed = false;
  camService();
  if (state == ACTIVE) runActive();
  tvStep();
  persist();
  eraseStep();
  sdRetry();

  baroStep();
  if (since(tImu) >= 100) { tImu = millis(); imuDrain(); ugStep(); }  // FIFO holds ~2 s, so stalls lose nothing
  if (since(tCh) >= TM_PERIOD_MS) { tCh = millis(); for (int i = 0; i < 4; i++) readChamber(i); }
  if (since(tTm) >= TM_PERIOD_MS) {
    tTm = millis();
    sendSta(); sendAct(); sendHk(); sendCh(0); sendCh(1); sendBar(); sendImu();
  }
  if (state == ACTIVE && since(tFlash) >= FLASH_REC_MS) { tFlash = millis(); flashLog(); }
  if (camOn && !camStopping && since(tCamOn) >= CAM_BOOT_MS &&
      since(tCamPoll) >= 1000) {  // cameras must be booted; nothing else uses the camera UART then
    tCamPoll = millis();
    int32_t rem = camQueryRemaining(camPoll);
    camRemain[camPoll] = rem;
    camCheck(camPoll, rem);
    camPoll = (camPoll + 1) % 8;
  }
  if (since(tLog) >= 1000) { tLog = millis(); logHk(); sendCam(); }
  pumpTx();
}
