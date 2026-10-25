#pragma once
// Uplink command parser (SED table 4.29). Pure C so it runs in host tests.
#include <stdlib.h>
#include <string.h>

enum Cmd {
  C_BAD, C_PING, C_TEST_ON, C_TEST_OFF, C_STATUS, C_HK, C_OBS, C_MEM, C_SAVE,
  C_CLEAR, C_CLEAR_OK, C_CAM_ON, C_CAM_OFF, C_LED_ALL, C_LED, C_LED_OFF,
  C_SAFE_ON, C_SAFE_OFF, C_REBOOT, C_TV, C_REARM, C_SELF_TEST
};
struct Parsed { Cmd id; int a, b; };

// reads " <int>" in [lo, hi]; anything else fails
inline bool parseNum(const char *&s, int lo, int hi, int &out) {
  if (*s != ' ' || s[1] < '0' || s[1] > '9') return false;
  char *e;
  long v = strtol(s + 1, &e, 10);
  if (v < lo || v > hi) return false;
  out = (int)v;
  s = e;
  return true;
}

// Exact match only, so a corrupted line can never trigger anything.
inline Parsed parseCmd(const char *line) {
  static const struct { const char *w; Cmd id; } T[] = {
    {"PING", C_PING}, {"ENTER_TEST_MODE", C_TEST_ON}, {"EXIT_TEST_MODE", C_TEST_OFF},
    {"GET_STATUS", C_STATUS}, {"GET_HK", C_HK}, {"GET_OBS_STATUS", C_OBS},
    {"GET_MEMORY_STATUS", C_MEM}, {"SAVE_MEMORY", C_SAVE},
    {"CLEAR_MEMORY CONFIRM", C_CLEAR_OK}, {"CLEAR_MEMORY", C_CLEAR},
    {"CAM_ON", C_CAM_ON}, {"CAM_OFF", C_CAM_OFF}, {"LED_ALL", C_LED_ALL},
    {"LED_OFF", C_LED_OFF}, {"LED", C_LED}, {"ENTER_SAFE_MODE", C_SAFE_ON},
    {"EXIT_SAFE_MODE", C_SAFE_OFF}, {"REBOOT", C_REBOOT}, {"TV", C_TV}, {"REARM", C_REARM}, {"SELF_TEST", C_SELF_TEST},
  };
  Parsed p = {C_BAD, 0, 0};
  for (auto &t : T) {
    size_t n = strlen(t.w);
    if (strncmp(line, t.w, n) != 0 || (line[n] != 0 && line[n] != ' ')) continue;
    const char *s = line + n;
    bool ok = true;
    if (t.id == C_LED_ALL) ok = parseNum(s, 0, 255, p.a);
    else if (t.id == C_LED) ok = parseNum(s, 1, 4, p.a) && parseNum(s, 0, 255, p.b);
    else if (t.id == C_TV) ok = parseNum(s, 0, 8, p.a);  // 8 = automatic cycling
    if (ok && *s == 0) p.id = t.id;
    return p;
  }
  return p;
}
