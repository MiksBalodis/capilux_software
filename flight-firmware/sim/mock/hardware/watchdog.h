#pragma once
#include <stdint.h>
struct WatchdogHw { volatile uint32_t scratch[8]; };
extern WatchdogHw *watchdog_hw;
bool watchdog_caused_reboot();
