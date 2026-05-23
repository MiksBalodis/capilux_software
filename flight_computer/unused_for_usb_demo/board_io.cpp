#include "board_io.h"
#include "pins.h"

static const int LED_PINS[4] = {
  PIN_LED_EN1, PIN_LED_EN2, PIN_LED_EN3, PIN_LED_EN4
};

void initBoardPins() {
  for (int pin : LED_PINS) {
    pinMode(pin, OUTPUT);
    analogWrite(pin, 0);
  }

  pinMode(PIN_CAM_EN, OUTPUT);
  digitalWrite(PIN_CAM_EN, LOW);

  pinMode(PIN_SOE_ISO, INPUT_PULLDOWN);
  pinMode(PIN_LO_ISO, INPUT_PULLDOWN);

  pinMode(PIN_VIDEO_MUX_A0, OUTPUT);
  pinMode(PIN_VIDEO_MUX_A1, OUTPUT);
  pinMode(PIN_VIDEO_MUX_A2, OUTPUT);
  digitalWrite(PIN_VIDEO_MUX_A0, LOW);
  digitalWrite(PIN_VIDEO_MUX_A1, LOW);
  digitalWrite(PIN_VIDEO_MUX_A2, LOW);

  initShiftRegister();
}

void setAllLeds(uint8_t pwmValue) {
  for (int pin : LED_PINS) {
    analogWrite(pin, pwmValue);
  }
}

void setCameraPower(bool enabled) {
  digitalWrite(PIN_CAM_EN, enabled ? HIGH : LOW);
}

bool readSOE() {
  return digitalRead(PIN_SOE_ISO) == HIGH;
}

bool readLO() {
  return digitalRead(PIN_LO_ISO) == HIGH;
}

void initShiftRegister() {
  pinMode(PIN_SRCLK, OUTPUT);
  pinMode(PIN_RCLK, OUTPUT);
  pinMode(PIN_OE, OUTPUT);
  pinMode(PIN_SER, OUTPUT);

  digitalWrite(PIN_SRCLK, LOW);
  digitalWrite(PIN_RCLK, LOW);
  digitalWrite(PIN_SER, LOW);

  // OE is active-low on 74HC595.
  digitalWrite(PIN_OE, LOW);

  writeShiftRegister(0x00);
}

void writeShiftRegister(uint8_t data) {
  digitalWrite(PIN_RCLK, LOW);

  for (int i = 7; i >= 0; --i) {
    digitalWrite(PIN_SRCLK, LOW);
    digitalWrite(PIN_SER, ((data >> i) & 0x01) ? HIGH : LOW);
    digitalWrite(PIN_SRCLK, HIGH);
  }

  digitalWrite(PIN_RCLK, HIGH);
}
