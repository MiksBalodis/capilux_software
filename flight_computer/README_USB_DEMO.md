# CAPILUX USB demo firmware

This firmware is the simplest possible uplink/downlink test.

It does not need:
- the flight PCB
- RS-422 hardware
- sensors
- SD card

It sends simulated pressure over USB Serial.

## Commands

Type these in PlatformIO Serial Monitor or send them from the GUI:

```text
PING
GET_STATUS
GET_PRESSURE
START_STREAM
STOP_STREAM
```

## Expected reply

```json
{"type":"pressure","t_ms":12345,"ch":1,"ok":1,"hpa":1001.23,"source":"simulated"}
```

## Build/upload

```bash
pio run
pio run -t upload
pio device monitor
```
