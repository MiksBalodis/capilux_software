#pragma once
// 64-byte flight record for the onboard flash backup log (10 Hz from SOE).
// Layout keeps every float 4-byte aligned (Cortex-M0+ has no unaligned access).
// Decoder: tools/decode_flash.py, keep both in sync.
#include <stddef.h>
#include <stdint.h>

struct FlashRec {
  uint32_t tSoeMs;    // ms since SOE (not since boot), so a resume after a power cut can continue the clock
  uint16_t boot;      // SD boot folder number, 0xFFFF = no SD
  uint8_t state;      // State enum
  uint8_t flags;      // bit0 SOE, bit1 LO, bit2 camera power, bit3 SD logging OK
  float chP[4];       // hPa, NaN = invalid
  float barP;         // hPa, NaN = invalid
  float acc[3];       // g, NaN = invalid
  float gyr[3];       // dps, NaN = invalid
  int16_t chT[4];     // 0.01 degC, INT16_MIN = invalid
  int16_t barT;       // 0.01 degC, INT16_MIN = invalid
  uint16_t crc;       // CRC16-CCITT over the first 62 bytes
};
static_assert(sizeof(FlashRec) == 64, "4 records per 256 B flash page");

inline uint16_t crc16(const uint8_t *d, size_t n) {  // CCITT-FALSE: poly 0x1021, init 0xFFFF
  uint16_t c = 0xFFFF;
  while (n--) { c ^= (uint16_t)*d++ << 8; for (int i = 0; i < 8; i++) c = c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1; }
  return c;
}
