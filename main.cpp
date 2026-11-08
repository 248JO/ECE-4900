#include <Arduino.h>

const int STEP_PIN = 4;   // A4988 STEP -> ESP32 D4 / GPIO 4
const int DIR_PIN  = 2;   // A4988 DIR  -> ESP32 D2 / GPIO 2

const int LIMIT_1_PIN = 5;    // First/home-side limit switch
const int LIMIT_2_PIN = 18;   // Second/opposite-side limit switch

const int STEP_INTERVAL_US = 10000;  // 30 RPM in full-step mode
const int NUMBER_OF_PORTIONS = 100;

// For NC wiring: normal = LOW; touched/open = HIGH
bool limitTouched(int pin) {
  return digitalRead(pin) == HIGH;
}

void takeOneStep() {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(5);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(STEP_INTERVAL_US - 5);
}

void stopForever() {
  while (true) {
    delay(1000);  // Motor remains stopped here
  }
}

void setup() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);

  pinMode(LIMIT_1_PIN, INPUT_PULLUP);
  pinMode(LIMIT_2_PIN, INPUT_PULLUP);

  digitalWrite(STEP_PIN, LOW);

  // ------------------------------------------------
  // 1. Home to the first limit switch: D5
  // ------------------------------------------------
  digitalWrite(DIR_PIN, HIGH);  // Change both directions if needed

  while (!limitTouched(LIMIT_1_PIN)) {
    takeOneStep();
  }

  delay(1000);  // Stay at the first switch for 1 second

  // ------------------------------------------------
  // 2. Move to D18 and count the travel distance
  // ------------------------------------------------
  digitalWrite(DIR_PIN, LOW);

  long stepsBetweenLimits = 0;

  while (!limitTouched(LIMIT_2_PIN)) {
    takeOneStep();
    stepsBetweenLimits++;
  }

  // ------------------------------------------------
  // 3. Return toward D5 in 100 equal timed portions
  // ------------------------------------------------
  digitalWrite(DIR_PIN, HIGH);

  long totalStepsMoved = 0;
  bool limit2Released = false;

  for (int portion = 1; portion <= NUMBER_OF_PORTIONS; portion++) {

    // Makes all 100 portions add up to exactly stepsBetweenLimits.
    long targetSteps =
        (stepsBetweenLimits * portion) / NUMBER_OF_PORTIONS;

    long stepsThisPortion = targetSteps - totalStepsMoved;

    for (long step = 0; step < stepsThisPortion; step++) {

      // Stop immediately if either limit is touched.
      // D18 is ignored only until the motor has moved away from it.
      if (limitTouched(LIMIT_1_PIN) ||
          (limit2Released && limitTouched(LIMIT_2_PIN))) {
        stopForever();
      }

      takeOneStep();
      totalStepsMoved++;

      // Once D18 is no longer pressed, start monitoring it too.
      if (!limitTouched(LIMIT_2_PIN)) {
        limit2Released = true;
      }
    }

    delay(1000);  // Wait one second before the next 1/100 movement
  }

  // At this point the motor should have arrived at D5.
  // Stop as a safety measure if D5 was not detected.
  stopForever();
}

void loop() {
  // The complete sequence runs only once after reset/upload.
}

