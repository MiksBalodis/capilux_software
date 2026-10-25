#pragma once
#include <Arduino.h>
// In-memory card in sim/hw.cpp; contents survive simulated reboots like a real card.
#define DEDICATED_SPI 1
#define SD_SCK_MHZ(x) (x)
#define O_WRONLY 0x01
#define O_CREAT 0x02
#define O_APPEND 0x04
struct SdSpiConfig { SdSpiConfig(int, int, int) {} };
class SdFat32 {
 public:
  bool begin(SdSpiConfig);
  void end();
  bool exists(const char *p);
  bool mkdir(const char *p);
};
class File32 {
 public:
  std::string path;
  uint32_t pos = 0, size = 0;
  bool open_ = false, append = false;
  File32() {}
  bool createContiguous(const char *p, uint32_t n);
  bool open(const char *p, int flags);
  bool seekSet(uint32_t at);
  size_t write(const void *b, size_t n);
  bool sync();
  bool close();
};
