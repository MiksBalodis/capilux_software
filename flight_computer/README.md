# CAPILUX sensor bring-up — PlatformIO / Arduino framework

This is a first hardware bring-up project for the CAPILUX flight computer.

## What it does

- Initializes both I2C buses:
  - I2C bus 1: GP4 SDA, GP5 SCL
  - I2C bus 2: GP2 SDA, GP3 SCL
- Initializes SD card SPI pins:
  - MISO GP16
  - CS GP17
  - SCK GP18
  - MOSI GP19
- Reads:
  - 4x LPS27HHWT chamber pressure/temperature sensors
  - MS5611 pressure/temperature sensor
  - LSM6DSO32 IMU acceleration/gyro
- Prints telemetry to Serial Monitor once per second
- Optionally appends telemetry to `/sensor_log.csv` on the SD card

## PlatformIO

This project uses the Earle Philhower Arduino-Pico core through PlatformIO:

```ini
platform = https://github.com/maxgerhardt/platform-raspberrypi.git
board_build.core = earlephilhower
```

This is needed because the default `platform = raspberrypi` Arduino-Mbed core does not expose the same RP2040 Arduino APIs used here, especially `Wire1`, `Wire.setSDA()`, `Wire.setSCL()`, and the usual `SD.h` workflow.

Open the folder in VS Code with PlatformIO installed.

Build:
```bash
pio run
```

Upload:
```bash
pio run -t upload
```

Open serial monitor:
```bash
pio device monitor
```

## Notes

This is not yet flight software. It is a clean sensor bring-up base.
Next steps:
1. Add robust fault handling.
2. Add mission state machine.
3. Add SOE-triggered experiment start.
4. Add telemetry packet format for GUI/LoRa.
5. Add watchdog and safe logging.
