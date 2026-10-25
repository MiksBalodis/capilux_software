#pragma once
#include <stddef.h>
#include <stdint.h>

// ============================================================================
//  FLIGHT TIMELINE  --  EDIT THIS BLOCK WHEN EUROLAUNCH CONFIRMS THE NUMBERS
//  Seconds relative to lift-off (LO = T0). Negative = before lift-off.
// ============================================================================
constexpr int32_t T_SOE_S = -45;        // SOE given at T-45 s (SED 4.9.6 "T-0:45"). The IPR asks for an earlier
                                        // SOE (open ACTION, SED table 6.3): just change this number.
constexpr int32_t T_POWER_OFF_S = 600;  // RXSM cuts experiment power: T+600 s (REXUS UM 7.17 p.56, "usually").
                                        // SED v3.0 tables 6.3 / 6.8 still say T+300 s: if RXSM really cuts
                                        // at 300, set 300 here, or power is cut while recording.
constexpr int32_t STOP_MARGIN_S = 20;   // everything must be off this long before power-off
constexpr uint32_t CAM_BOOT_MS = 5000;  // camera power-on -> record command
constexpr uint32_t CAM_STOP_MS = 3000;  // record-stop command -> camera power cut (DVR closes its file)

// Derived, do not edit: when the record-stop command is sent, measured from SOE.
// Default (T-45 s, T+600 s, 20 s, 3 s): stop command at SOE+622 s = T+577 s, all off at T+580 s.
constexpr uint32_t T_EXP_END_MS = (uint32_t)((T_POWER_OFF_S - T_SOE_S - STOP_MARGIN_S) * 1000 - (int32_t)CAM_STOP_MS);
// The same moment measured from LO (only used when END_FROM_LO = true).
constexpr uint32_t T_EXP_END_FROM_LO_MS = (uint32_t)((T_POWER_OFF_S - STOP_MARGIN_S) * 1000 - (int32_t)CAM_STOP_MS);
static_assert(T_SOE_S <= 0, "SOE after lift-off is not supported");
static_assert(T_POWER_OFF_S - STOP_MARGIN_S - T_SOE_S > 60, "experiment window shorter than 60 s: check the timeline");

// ============================================================================
//  BEHAVIOUR SWITCHES  --  decisions the team still has to confirm
// ============================================================================
// SOE released before LO (countdown hold / recycle): stop the cameras, go back to STANDBY and
// re-arm, so the next SOE starts a fresh flight. Matches the UM name "Start/Stop of Experiment".
// Risky only if SOE turns out to be a short pulse: then set false. (issues.md #2)
constexpr bool SOE_DROP_ABORTS = true;
constexpr uint32_t SOE_DROP_MS = 1000;   // SOE must be gone this long to count as released

// Time the end of the experiment from LO once LO is seen (exact w.r.t. power-off, survives a short
// hold after SOE). SED v3.0 / AM Action 64 say LO is logged only, so this stays false until the SED
// is updated. Before LO the experiment then ends at SOE + T_EXP_END_MS + HOLD_ALLOWANCE_MS.
constexpr bool END_FROM_LO = false;
constexpr uint32_t HOLD_ALLOWANCE_MS = 600000;  // only used when END_FROM_LO = true

// LO seen while not flying (forgotten TEST mode, missed SOE) starts the flight anyway, back-dated to
// the expected SOE time. Also contradicts "LO logged only": recommended true after the SED update. (#7)
constexpr bool LO_BACKUP_START = false;

// TEST mode exits by itself after this time (0 = never). Entered at ~T-6:00, so a forgotten
// EXIT_TEST_MODE cannot block SOE. (#7)
constexpr uint32_t TEST_TIMEOUT_MS = 300000;

// After SHUTDOWN the flight counts as done. A reboot or power cycle while SOE or LO is STILL
// asserted stays in SHUTDOWN (no second run on the same SOE) until the ground sends REARM. (#3)
// A boot with both signals released goes to STANDBY as usual, so a bench run or the launch-site
// flight simulation can never leave the experiment latched for launch day.
constexpr bool LATCH_FLIGHT_DONE = true;

// Power lost mid-flight (not a watchdog reset) and SOE or LO still asserted at power-up: resume
// ACTIVE with the time since SOE read back from the flash log, instead of restarting the clock. (#3)
// The outage itself cannot be measured (no RTC), so the resumed clock runs late by its length;
// this much is added as a guess. Larger = stop earlier (safe side).
constexpr bool RESUME_AFTER_POWER_LOSS = true;
constexpr uint32_t RESUME_ASSUMED_OUTAGE_MS = 2000;

// ============================================================================
//  CAMERAS
// ============================================================================
constexpr uint8_t CAM_MASK = 0xFF;          // bit n = a camera is fitted on mux channel n
constexpr bool CAM_REC_CONFIRM = true;      // closed loop: resend REC_START to cameras whose
                                            // remaining recording time does not drop (#6)
constexpr uint32_t CAM_RETRY_MS = 15000;    // between resend rounds
constexpr uint8_t CAM_MAX_RETRIES = 6;
// TV channel: the ground can pick any camera at any time while the cameras are on (TEST and ACTIVE):
// "TV 0".."TV 7" holds that camera, "TV 8" goes back to automatic cycling. Entering TEST or ACTIVE
// starts in TV_DEFAULT_CH, so the TV shows something useful even with no uplink.
constexpr int TV_AUTO = 8;
constexpr int TV_DEFAULT_CH = TV_AUTO;      // 0-7 = fixed camera, TV_AUTO = cycle through the fitted cameras
constexpr uint32_t TV_DWELL_MS = 10000;     // per camera when cycling (8 cameras: one full round every 80 s)

// ============================================================================
//  SD CARD  (one folder per boot, files preallocated so flight writes never touch the FAT)
// ============================================================================
constexpr uint32_t LOG_HOURS = 4;  // powered time the files must cover: T-2 h check-out + 3 h countdown margin (#4)
constexpr uint32_t HK_FILE_BYTES = LOG_HOURS * 3600u * 400u;    // HK ~250 B/s incl. #CAM/#EVT lines
constexpr uint32_t IMU_FILE_BYTES = LOG_HOURS * 3600u * 6500u;  // IMU ~5.2-5.7 KB/s at 104 Hz
constexpr bool LOG_ROLLOVER_IN_FLIGHT = false;  // a full file in flight just stops (never touch the FAT in flight)
constexpr uint8_t SD_WRITE_TRIES = 3;       // a failed write is retried before that file is given up (#5)
constexpr uint8_t SD_BOOT_TRIES = 3;
constexpr uint32_t SD_RETRY_MS = 30000;     // pre-flight only: re-initialise a failed card this often (#5)

// ============================================================================
//  OTHER TIMING
// ============================================================================
constexpr uint32_t TM_PERIOD_MS = 200;      // 5 Hz telemetry
constexpr uint32_t WDT_MS = 4000;
constexpr uint32_t WDT_SLOW_MS = 8000;      // while SD init / allocation runs (RP2040 limit ~8.3 s)
constexpr uint32_t DEBOUNCE_MS = 50;
constexpr uint32_t CLEAR_WINDOW_MS = 5000;
constexpr uint8_t LED_FLIGHT = 255;
constexpr size_t UPLINK_RX_FIFO = 512;      // Serial1 RX buffer (default 32 B overflowed during slow loop passes, #9)
constexpr size_t CAM_RX_FIFO = 1024;
constexpr size_t TXQ_MAX = 2048;            // downlink queue; dropped beyond this when the link is down
constexpr uint8_t I2C_RECOVER_AFTER = 10;   // consecutive failed transfers on one bus -> bus recovery (#11)
constexpr uint32_t IMU_STALE_MS = 1000;     // no IMU samples this long -> re-init the IMU (#13)
constexpr uint8_t CH_STALE_READS = 10;      // chamber sensor without new data for 10 reads (2 s) -> re-init (#13)

// ============================================================================
//  FLIGHT EXTRAS
// ============================================================================
// LED sync flash: all chamber LEDs off for LED_SYNC_OFF_MS at fixed times after SOE. Gives one
// shared, logged time mark in all 8 videos to line them up with the sensor data (SYNC_n events).
constexpr uint32_t LED_SYNC_AT_MS[] = {15000};  // since SOE, after all cameras are recording (more entries = more flashes)
constexpr uint32_t LED_SYNC_OFF_MS = 200;                             // >= 5 frames at 25 fps

// Microgravity events from the IMU: UG_START / UG_END in the log and downlink. Logging only,
// nothing is controlled by it. Only after LO, with hysteresis and a hold time.
constexpr float UG_ENTER_G = 0.05f;       // |a| below this ...
constexpr uint32_t UG_ENTER_MS = 500;     // ... for 0.5 s -> UG_START
constexpr float UG_EXIT_G = 0.10f;        // |a| above this ...
constexpr uint32_t UG_EXIT_MS = 500;      // ... for 0.5 s -> UG_END

// Onboard flash backup ring: last 512 KB (~13.6 min at 10 Hz, one flight from SOE needs ~10.5) of the Pico's 2 MB flash.
// The firmware image must stay below it (checked at boot).
constexpr uint32_t FLASH_LOG_OFS = 0x180000, FLASH_LOG_SIZE = 0x80000;
constexpr uint32_t FLASH_REC_MS = 100;  // 10 Hz, from SOE to shutdown
static_assert((T_EXP_END_MS + (END_FROM_LO ? HOLD_ALLOWANCE_MS : 0)) / FLASH_REC_MS * 64 < FLASH_LOG_SIZE * 9 / 10,
              "flight (+ hold allowance) does not fit in the flash ring");

// ============================================================================
//  HARDWARE  (mainboard netlist v5, 2026-10-06)
// ============================================================================
constexpr int PIN_SDA1 = 4, PIN_SCL1 = 5;     // I2C0 (Wire): chambers 1/2, MS5611, LSM6DSO32
constexpr int PIN_SDA2 = 2, PIN_SCL2 = 3;     // I2C1 (Wire1): chambers 3/4
constexpr int PIN_CAM_TX = 8, PIN_CAM_RX = 9; // UART1 through the 4051 muxes
constexpr int PIN_CAM_EN = 10;                // high = camera power on (Q3 -> Q2)
constexpr int PIN_CH_LED[4] = {13, 12, 11, 7};  // chamber 1..4 (SED table 4.26 has this reversed).
                                                // Not "PIN_LED": the Arduino core already defines that.
constexpr int PIN_SOE = 14, PIN_LO = 15;      // RXSM open collector + 10k pull-up: LOW = active
constexpr int PIN_SD_CS = 17;                 // SPI0 default pins GP16/18/19
constexpr int PIN_SRCLK = 20, PIN_RCLK = 21, PIN_OE = 22, PIN_SER = 26;
constexpr int PIN_TV[3] = {6, 27, 28};        // A0..A2 to TV mux board
constexpr int PIN_VSYS_ADC = 29;              // VSYS/3 inside the Pico

constexpr uint8_t ADDR_IMU = 0x6B, ADDR_BARO = 0x77;
// LPS27HHW per chamber: bus 0 = Wire, 1 = Wire1. Address depends on the SA0 strap on each chamber PCB.
constexpr uint8_t CH_BUS[4] = {0, 0, 1, 1};
constexpr uint8_t CH_ADDR[4] = {0x5C, 0x5D, 0x5C, 0x5D};

// 74HC595: QA-QC = RX mux select, QD-QF = TX mux select, QG/QH = mux enables (active low)
constexpr uint8_t MUX_OFF = 0xC0;
