#pragma once
// RunCam Device protocol: parse a GET_SETTINGS (root) reply for storage info.
// Entries are [id][name\0][value\0] back to back. Pure C, host-testable.
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

constexpr uint8_t RC_ID_SDCARD_CAPACITY = 3;  // value "free/total", MB
constexpr uint8_t RC_ID_REMAINING_TIME = 4;   // value seconds of recording left

// Returns true if REMAINING_TIME was found. freeMb/totalMb are -1 if absent.
inline bool parseCamStorage(const uint8_t *buf, int n, int32_t &remainSec, int32_t &freeMb, int32_t &totalMb) {
  remainSec = freeMb = totalMb = -1;
  int i = 0;
  while (i < n) {
    uint8_t id = buf[i++];
    const char *name = (const char *)buf + i;
    int nl = (int)strnlen(name, n - i);
    if (i + nl >= n) break;
    i += nl + 1;
    const char *val = (const char *)buf + i;
    int vl = (int)strnlen(val, n - i);
    if (i + vl >= n) break;  // value must be NUL-terminated inside the buffer
    i += vl + 1;
    if (id == RC_ID_REMAINING_TIME) remainSec = atol(val);
    if (id == RC_ID_SDCARD_CAPACITY) {
      const char *slash = strchr(val, '/');
      if (slash) { freeMb = atol(val); totalMb = atol(slash + 1); }
    }
  }
  return remainSec >= 0;
}
