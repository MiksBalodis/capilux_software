#pragma once

// Debug UART
constexpr int PIN_DEBUG_TX = 0;
constexpr int PIN_DEBUG_RX = 1;

// I2C buses
constexpr int PIN_I2C2_SDA = 2;  // chamber 3/4 LPS27HHWT
constexpr int PIN_I2C2_SCL = 3;

constexpr int PIN_I2C1_SDA = 4;  // chamber 1/2 LPS27HHWT + onboard sensors
constexpr int PIN_I2C1_SCL = 5;

// Video mux address pins
constexpr int PIN_VIDEO_MUX_A0 = 6;
constexpr int PIN_VIDEO_MUX_A1 = 27;
constexpr int PIN_VIDEO_MUX_A2 = 28;

// LEDs
constexpr int PIN_LED_EN1 = 7;
constexpr int PIN_LED_EN2 = 11;
constexpr int PIN_LED_EN3 = 12;
constexpr int PIN_LED_EN4 = 13;

// Camera UART and power
constexpr int PIN_CAM_TX = 8;
constexpr int PIN_CAM_RX = 9;
constexpr int PIN_CAM_EN = 10;

// RXSM signals
constexpr int PIN_SOE_ISO = 14;
constexpr int PIN_LO_ISO  = 15;

// SD card SPI
constexpr int PIN_SD_MISO = 16;
constexpr int PIN_SD_CS   = 17;
constexpr int PIN_SD_CLK  = 18;
constexpr int PIN_SD_MOSI = 19;

// SN74HC595 shift register
constexpr int PIN_SRCLK = 20;
constexpr int PIN_RCLK  = 21;
constexpr int PIN_OE    = 22;
constexpr int PIN_SER   = 26;
