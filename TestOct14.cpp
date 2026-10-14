#include <Arduino.h>

const int DIR_PIN = 0;   // Pico GP0 -> TB6600 DIR+
const int PUL_PIN = 1;   // Pico GP1 -> TB6600 PUL+

const int RPM = 150;
const int PULSES_PER_REV = 200;   // TB6600 setting

const int PULSE_FREQUENCY = (RPM * PULSES_PER_REV) / 60; // 100 Hz
const int PERIOD_US = 1000000 / PULSE_FREQUENCY;         // 10,000 us
const int PULSE_WIDTH_US = 10;

void moveSteps(int steps) {
  for (int pulse = 0; pulse < steps; pulse++) {
    digitalWrite(PUL_PIN, HIGH);
    delayMicroseconds(PULSE_WIDTH_US);

    digitalWrite(PUL_PIN, LOW);
    delayMicroseconds(PERIOD_US - PULSE_WIDTH_US);
  }
}

void setup() {
  pinMode(DIR_PIN, OUTPUT);
  pinMode(PUL_PIN, OUTPUT);

  // Move forward one rotation
  digitalWrite(DIR_PIN, HIGH);
  delay(10);
  moveSteps(PULSES_PER_REV);

  delay(1000);  // Wait one second

  // Move backward the same distance
  digitalWrite(DIR_PIN, LOW);
  delay(10);
  moveSteps(PULSES_PER_REV);
}

void loop() {
  // Runs once after power-on/reset, then stops.
}