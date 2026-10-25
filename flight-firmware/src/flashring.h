#pragma once
// Flash ring buffer logic, hardware-free so it runs in host tests.
// The platform provides flashAt / flashHwErase / flashHwProgram.
#include <stdint.h>
#include "config.h"

constexpr uint32_t FLASH_END = FLASH_LOG_OFS + FLASH_LOG_SIZE, SECTOR = 4096, PAGE = 256;

const uint8_t *flashAt(uint32_t ofs);
void flashHwErase(uint32_t ofs);                      // one 4 KB sector
void flashHwProgram(uint32_t ofs, const uint8_t *d);  // one 256 B page

inline uint32_t nextSector(uint32_t ofs) {
  ofs = ofs / SECTOR * SECTOR + SECTOR;
  return ofs >= FLASH_END ? FLASH_LOG_OFS : ofs;
}

inline bool blank(uint32_t ofs, uint32_t n) {
  const uint8_t *p = flashAt(ofs);
  for (uint32_t i = 0; i < n; i++) if (p[i] != 0xFF) return false;
  return true;
}

inline void eraseSector(uint32_t ofs) { if (!blank(ofs, SECTOR)) flashHwErase(ofs); }

// End of data = a used sector followed by a blank one; resume at its first blank page.
// No such pair (all blank, or a new board full of junk): start at the beginning.
inline uint32_t ringFindEnd() {
  for (uint32_t s = FLASH_LOG_OFS; s < FLASH_END; s += SECTOR) {
    if (blank(s, SECTOR) || !blank(nextSector(s), SECTOR)) continue;
    uint32_t w = s;
    while (w < s + SECTOR && !blank(w, PAGE)) w += PAGE;
    return w == s + SECTOR ? nextSector(s) : w;
  }
  return FLASH_LOG_OFS;
}

// Writes one page and returns the new write position. Never refuses: on entering a sector
// it erases that sector and the next, so the end of data stays findable after a reset.
inline uint32_t ringWrite(uint32_t wr, const uint8_t *page) {
  if (!blank(wr, PAGE)) wr = nextSector(wr);  // something in the way: skip to a fresh sector
  if (wr % SECTOR == 0) { eraseSector(wr); eraseSector(nextSector(wr)); }
  flashHwProgram(wr, page);
  wr += PAGE;
  return wr >= FLASH_END ? FLASH_LOG_OFS : wr;
}

// Offset of the nearest programmed page before position wr, or 0 if the whole ring is blank.
// Used at boot to read back the last flight record (power-loss resume / flight-done latch).
inline uint32_t ringLastPage(uint32_t wr) {
  uint32_t p = wr;
  for (uint32_t i = 0; i < FLASH_LOG_SIZE / PAGE; i++) {
    p = (p == FLASH_LOG_OFS ? FLASH_END : p) - PAGE;
    if (!blank(p, PAGE)) return p;
  }
  return 0;
}
