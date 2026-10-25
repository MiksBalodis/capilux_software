# Flight firmware review: bugs and data-loss risks

Review of `flight-firmware/` v0.2 (commit 6aa9bd1), 2026-10-09. One main review plus three
independent blind reviews, cross-checked against SED v3.0, REXUS User Manual v7.17 and netlist v5.
Line numbers are `src/main.cpp` unless stated. "Found by" counts how many of the 4 reviews raised it.

Host tests (`test_cmd`, `test_ring`) pass. All pins match netlist v5 (U3 pads, 595 -> 4051 order,
SOE/LO pull-ups R8/R9, no pull-up on 595 OE).

## Status in v0.3 (2026-10-25)

Firmware compiles for the Pico with no warnings. Host tests pass, and `flight-firmware/sim/run.sh`
runs the real `main.cpp` through 15 flight scenarios (66 checks): nominal, hold/recycle, watchdog
reset, power loss, power loss + hung SD, forgotten / late test mode, command protections, camera
faults, stuck I2C bus, SD write errors, TV switching. Every value that is not final is in `config.h`.

| # | Status | How |
|---|--------|-----|
| 1 | fixed | End derived from `T_SOE_S`, `T_POWER_OFF_S` (600 s per REXUS UM), `STOP_MARGIN_S`: stop T+577, all off T+580 |
| 2 | fixed | SOE released before LO: stop, STANDBY, re-arm (`SOE_DROP_ABORTS`). Timing from LO optional (`END_FROM_LO`) |
| 3 | fixed | Power-loss resume from the flash ring (`tSoeMs`); flight-done latch while SOE/LO still asserted; new `REARM` |
| 4 | fixed | Files preallocated for `LOG_HOURS` = 4 h; no rollover in flight |
| 5 | fixed | Per-file failure, write retries, pre-flight SD re-init every 30 s, 3 boot attempts |
| 6 | fixed | REC_START repeated to cameras whose remaining time does not drop; `rec` mask in CAM packet |
| 7 | fixed | TEST times out after 5 min; flight is timed from the SOE edge even if seen late. `LO_BACKUP_START` optional |
| 8 | fixed | SAVE_MEMORY pre-flight only; `saved` only on success |
| 9 | partly | RX FIFO 512 B. Bytes arriving during a flash erase (IRQs off, up to 400 ms, ~every 6 s in ACTIVE) can still be lost |
| 10 | partly | Downlink pumped while waiting for cameras; queue still drops beyond 2 KB |
| 11 | fixed | Bus clear at boot; recovery after 10 failures only if SDA/SCL is held low (missing sensors don't trigger it) |
| 12 | fixed | Resume adds the worst-case hang (4 s, 8 s after a hung SD resume) |
| 13 | fixed | LPS27 STATUS check + re-init; IMU re-init after 1 s without samples |
| 14 | fixed | ACTIVE resumed before SD init; SD init under 8 s watchdog; skipped on a second resume. CAM_EN gap at reset: measure on HW |
| 15 | fixed | Signed `since()` everywhere |
| 16 | fixed | Queue drained before REBOOT |
| 17 | fixed | Lines with non-printable bytes rejected as BAD_CMD |
| 18 | open | Up to 3 records lost on reset; the power-loss resume compensates the time (+400 ms) |
| 19 | fixed | `FlashRec.tSoeMs` = ms since SOE; `decode_flash.py` updated |
| 20 | fixed | Pairing reset after overrun / read error |
| 21 | fixed | USB write only if the host has buffer space |
| 22 | open | Packet sizes not re-checked against 64 B |
| 23 | note | Flash CSV column is now `t_soe_s` |
| 24 | partly | State machine / resume covered by `sim/`; torn page / half-erased sector still untested |

Also changed: `PIN_LED` -> `PIN_CH_LED`, `D1` -> `msD1`, `sdfat::` names (first Pico build fixes);
new `EVT` downlink packet for events; GUI has a Re-arm button, EVT in the log and REC per camera.
TV: the ground can switch the TV camera in TEST and in flight (`TV 0-7`, `TV 8` = cycle); with no
command it cycles through the fitted cameras, 10 s each. Telemetry stays at 5 Hz (as SED v3.0).
Added: LED sync flash (SYNC_1), microgravity events (UG_START/UG_END), flight time `tf` in STA,
SELF_TEST command (STS packet). See flight-firmware/README.md.

**SED changes needed:** add `TV <0-7>` and `REARM` to table 4.29 (no command was deleted: CMD-01..15
are all still there); describe the SOE-release abort and the flight-done latch; if `END_FROM_LO` /
`LO_BACKUP_START` are turned on, AM Action 64 ("LO logged only") must be revised.

## Open questions for EuroLaunch / team (block items 1-3)

- Do SOE and LO stay asserted until power-off, and are they released on a hold, abort or recycle?
  UM l.1597: waveform info "will be added in the future". UM calls SOE "Start/Stop of experiment".
- Experiment power-off time: SED interface table says T+300 s (SED l.3971, l.4082), UM says usually
  T+600 s (UM l.1554), UM example countdown says T+20 min.
- SED "T−0:45 min" for SOE (l.3127) reads as 45 s from the other timeline entries (power on T−15:00,
  test mode T−6:00), but should be written unambiguously.

## High

1. **Experiment window vs power-off.** `config.h:26` `T_EXP_END_MS = 500000` from SOE at T−45 s
   puts camera stop at ~T+455 s and shutdown at ~T+458 s. If power-off is T+300 s, power is cut
   while recording: DVR files never finalised, last flash page lost. Set from the confirmed timeline
   with margin (watchdog resumes push it later, see 12). Found by 4/4.

2. **Countdown hold / recycle after SOE loses the flight.** SOE is latched once (677); ACTIVE never
   looks at SOE dropping; SHUTDOWN is left only by `REBOOT`; REBOOT is refused in ACTIVE (574).
   A hold longer than ~8 min after SOE: cameras record the pad, experiment shuts down before
   lift-off, the re-given SOE is ignored. A shorter hold ends recording early relative to LO.
   Fix: SOE drop before LO -> stop cameras, back to STANDBY; re-arm allowed until LO seen;
   optionally time the end from LO with SOE+T_EXP_END as fallback. Found by 4/4.

3. **Non-watchdog reset in flight restarts the timeline.** Only watchdog resets resume (622-663).
   A brownout / power-on reset boots STANDBY; if SOE is still asserted, ACTIVE restarts with
   `tSoe = now` (677), so stop lands after RXSM power-off and the open video file is cut again.
   Same for `REBOOT` from SHUTDOWN or a post-flight power-up with SOE asserted: ACTIVE restarts and
   the flash ring overwrites the oldest data, i.e. the flight. SED O.4 asks to resume safely after any
   power interruption. Fix: persist SOE-relative time / "flight done" in flash (the ring's last
   record already has state, ms, boot), and never re-arm after SHUTDOWN without a ground command.
   Found by 4/4.

4. **IMU file rollover in flight kills all SD logging.** `config.h:39`, 163-168. IMU logs from power-on
   at ~5.2-5.7 KB/s, so the 24 MB file fills after ~70-80 min. UM: design for 3 h countdown plus
   holds. Rollover runs `close()` + `createContiguous()` (FAT scan on a 64 GB card: can block past
   the 4 s watchdog; touches FAT in flight). Failure calls `sdFailed()`, which also stops HK.
   Fix: preallocate for >= 4 h (~100 MB) or pre-create the next part at boot. Found by 4/4.

5. **One SD error disables SD logging for the whole boot.** `sdFailed()` (177-197) on any failed
   write/zeroChunk/sync, no retry. A single card GC stall or contact chatter at launch loses the
   104 Hz IMU (needed for SED P.8) and HK; flash has only 10 Hz snapshots. Boot-time `sd.begin` is
   also tried once (649). Fix: retry the write, bounded re-init attempts, retry `sd.begin` in
   STANDBY. Found by 4/4.

6. **Camera recording is open loop.** REC_START / REC_STOP are sent once, blind, 5 s after power-on
   (524-525). A camera that boots slowly or misses the frame records nothing. A watchdog resume sends
   REC_START again (`enterActive` sets `recPending`): harmless for 0x03, but with the 0x01 toggle the
   README plans, it stops recording. Fix: confirm via REMAINING_TIME decreasing, retry; no blind
   resend on resume. Found by 4/4.

7. **TEST mode at SOE blocks the flight.** SOE is ignored in TEST (677) with no timeout or backstop.
   Test mode is used at T−6:00. Consistent with SED, but a forgotten EXIT_TEST_MODE loses the flight.
   Fix: LO (or a timeout) forces exit from TEST. Found by 3/4.

## Medium

8. **SAVE_MEMORY accepted in flight.** `C_SAVE` (555) works in ACTIVE/SHUTDOWN and appends to
   `SAVE.TXT`: FAT and directory writes in flight, against the SD design. `saved` is set even if the
   open fails (235). Fix: gate with `preflight()`. Found by 4/4.

9. **Uplink bytes dropped.** arduino-pico Serial1 RX FIFO defaults to 32 bytes (verified in
   `SerialUART.h`); at 38.4k that is ~8 ms (64 B with HW FIFO, ~17 ms). The loop blocks longer:
   camera query 100-800 ms once per second in TEST/ACTIVE (102-124), flash erase 45-400 ms with
   interrupts off (250) every ~6.4 s in ACTIVE and back to back during CLEAR. Commands are lost (the
   parser rejects the fragments). Fix: `Serial1.setFIFOSize(256)` before `begin()`; camera query as
   a one-chunk-per-pass state machine. Found by 4/4.

10. **Downlink throughput limited to ~32 B per loop pass.** `pumpTx` (47) only fills the UART HW FIFO.
    Telemetry is ~2.4 KB/s; slow passes (camera query, IMU drain, erase, sync) can let `txq` reach
    2048 and drop packets (43). Fix: larger UART TX buffer or pump more often.

11. **No I2C bus recovery.** 314-335. A slave holding SDA low (reset mid-transfer, brownout) makes
    the IMU, barometer and chambers 1/2 (all on `Wire`) invalid for the rest of the flight.
    Fix: 9 SCL pulses + STOP and `Wire.begin()` after N consecutive failures and in setup.
    Found by 4/4.

12. **Watchdog resume loses time.** `persist()` (536) saves elapsed time at the last loop; the up to
    4 s hang before the reset and the boot are not counted, so each resume pushes the stop later
    (worsens 1). Fix: add `WDT_MS` on resume. Found by 4/4.

13. **Sensor resets go unnoticed.** LPS27HHW back in power-down still answers 0x28 reads: stale
    pressure logged as valid (329). LSM6DSO32 after a reset has FIFO in bypass: count stays 0,
    `imuOk` stays true, no IMU lines ever again (360-378). Fix: check STATUS P_DA/T_DA; re-init the
    IMU when no samples for > 1 s. Found by 3/4.

14. **Setup runs without watchdog and before resuming.** SD init + folder search + 26 MB allocation
    (649) run before `wdt_begin` (652) and before ACTIVE is resumed. On a watchdog resume a hung or
    slow card leaves cameras/LEDs on with no state machine. Unverified on hardware: whether the
    watchdog is still armed after a watchdog reset (reset loop), and how long CAM_EN sits low (pads
    are reset) before `setup()`. Fix: resume ACTIVE before SD init; feed the watchdog during SD
    setup; measure the CAM_EN gap. Found by 3/4.

15. **Stale `now` underflow.** `now` is taken at loop start, but `handle()` / `enterActive()` set
    `tClearArmed` / `tCamOn` from a later `millis()`. `now - t` then wraps: line 683 can disarm CLEAR
    immediately (intermittent NOT_ARMED), and line 696 starts camera polling before the cameras have
    booted. Fix: signed difference or refresh `now`. Found by 2/4.

## Low

16. **REBOOT ACK never reaches the RXSM link.** It sits in `txq`; `Serial1.flush()` (576) only
    drains the HW FIFO. Pump `txq` empty first.
17. **NUL in an uplink line truncates it before matching.** `pollLine` stores NUL bytes; `parseCmd`
    then sees e.g. `REBOOT\0junk` as `REBOOT`. Reject lines containing NUL (or add a checksum).
18. **Partial flash page lost on reset.** Up to 3 records (300 ms) in `page[]` are lost on any reset.
19. **Flash record time is per boot.** `FlashRec.ms` is millis since boot; with SD failed
    (`boot = 0xFFFF`) records from a resumed boot cannot be told apart. Store time since SOE.
20. **IMU accel/gyro pairing can slip by one sample** after an overrun or read error (`imuHave`, 373).
21. **USB `Serial.println` in `emit()`** may block when a host is connected but not reading (ground only).
22. **Packets over 64 B.** UM recommends <= 64 B; CH1, ACT, STA can reach 65-66 B with realistic values.
23. **Data handling notes for post-flight:** preallocated CSVs end in NUL padding (stop at first NUL);
    the flash ring and `FLASHn.BIN` contain earlier runs (filter on `boot`); the ring can also be read
    directly with `picotool save -r 0x10180000 0x10200000` if the SD is lost.
24. **Test gaps:** no tests for torn page / half-erased sector, state machine, resume, SD zero-chunk
    logic or camera path.

## Cross-check with the IPR report (02-09-2026)

`references/RX_CAPILUX_IPR_Report_v1_02Sept26.md` does not discuss firmware internals (SD, flash
ring, watchdog, I2C, resets), but several remarks change the weight of the items above:

- **SOE earlier than T−45 s recommended** (IPR l.97, l.231) so nothing switches on/off around the
  "go". An earlier SOE needs a longer `T_EXP_END_MS` (item 1), makes a hold after SOE more likely
  (item 2), and eats into the 13.6 min flash ring. Open ACTION: give the SOE time in SED table 6.3.
- **Early check-out at about T−2 h with all experiments powered** (IPR l.98); nominal power-on is
  T−10 min, not the SED's T−600 s/T−15:00. If power stays on from check-out, the IMU file rolls over
  before launch (item 4). If it is cycled, each power-on is a new SD folder and 26 MB allocation;
  either way the logging must cope with a 2 h+ powered pad phase.
- **Uplink not working with the RXSM simulator** (IPR l.72, l.93, l.138; ground link was USB). Until
  it works, every ground-command recovery path is unproven: EXIT_TEST_MODE (item 7), REBOOT out of
  SHUTDOWN (item 2), CLEAR_MEMORY. Item 9 (32 B RX FIFO, loop blocking) is one candidate cause worth
  ruling out; RS-422 wiring/direction is another.
- **Recording confirmed by SD capacity decreasing** in GS telemetry (IPR l.95). This is the
  closed-loop check item 6 asks for; it is currently only shown to the operator, not used by the
  firmware to retry REC_START.
- **Team reverse-engineered RunCam firmware to delete footage before flight** (IPR l.94). If this
  becomes a command, it must be pre-flight only, two-step like CLEAR_MEMORY, and never reachable
  from the resume path.
- **SD card capacity to be checked with flight recording settings** (IPR l.96): camera-side storage,
  separate from the mainboard SD sizing in item 4.
- **Data-rate margin to be checked with MORABA** (IPR l.83): relevant to items 10 and 22.
- **Test mode vs normal mode** (IPR l.92) was only discussed as "timeline following"; the risk that
  SOE is ignored in TEST (item 7) was not raised.

## Checked and correct

- `FlashRec` 64 B, no padding, matches `decode_flash.py`; CRC16 / CRC8 correct.
- Flash ring: end detection after reset, wrap, interrupted CLEAR, power cut during erase;
  `backupFlash` rotation is oldest-first.
- SdFat only writes the directory entry when the size grows or a date callback is set (verified in
  `FatFile.cpp`), so syncs of the preallocated files touch data sectors only. Keep it that way.
- LSM6DSO32, LPS27HHW and MS5611 register settings, scaling, FIFO tags and 2nd-order compensation.
- MS5611 conversion timing (`baroStep` uses its own `millis()`).
- Command parser: table order, ranges, no prefix matches.
- 74HC595 bit order and RunCam GET_SETTINGS framing; `Serial1.availableForWrite()` exists in arduino-pico.
- Telemetry bandwidth ~60 % of 38.4k, within the 80 % limit.
- (v0.3) Telemetry 5 Hz in code and SED v3.0. The AM Action 38 response still says 1 Hz. SED table 4.27
  must list ACC + GYR (not IMU), CAM (1 Hz) and EVT; worst-case 5 Hz load is ~26 kbit/s of the 30 kbit/s limit.
