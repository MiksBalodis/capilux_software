#pragma once
// Host simulator: just enough of the arduino-pico API for src/main.cpp. Implemented in sim/hw.cpp.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <string>

#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define MSBFIRST 1
#define XIP_BASE 0x10000000u

uint32_t millis();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void pinMode(int p, int m);
void digitalWrite(int p, int v);
int digitalRead(int p);
void analogWrite(int p, int v);
void analogWriteFreq(int f);
void analogWriteRange(int r);
int analogRead(int p);
void analogReadResolution(int b);
void shiftOut(int data, int clk, int order, uint8_t v);
inline void noInterrupts() {}
inline void interrupts() {}

class String {
  std::string s;
 public:
  unsigned length() const { return (unsigned)s.size(); }
  String &operator+=(const char *x) { s += x; return *this; }
  String &operator+=(char c) { s += c; return *this; }
  char operator[](unsigned i) const { return s[i]; }
  void remove(unsigned i, unsigned n) { s.erase(i, n); }
};

class Stream {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual ~Stream() {}
};

struct SimPort;  // harness side
class SerialSim : public Stream {
 public:
  SimPort *p;
  explicit SerialSim(SimPort *port) : p(port) {}
  void begin(unsigned long) {}
  void setTX(int) {}
  void setRX(int) {}
  void setFIFOSize(size_t);
  int available() override;
  int read() override;
  int availableForWrite();
  size_t write(uint8_t c);
  size_t write(const uint8_t *b, size_t n);
  size_t write(char c) { return write((uint8_t)c); }
  void println(const char *s);
  void flush() {}
  explicit operator bool();
};
extern SerialSim Serial, Serial1, Serial2;

struct RP2040Sim {
  void wdt_begin(uint32_t ms);
  void wdt_reset();
  void reboot();
};
extern RP2040Sim rp2040;
