#include <Arduino.h>

// TB6600 control pins
const int DIR_PIN  = 0;  // Shared direction
const int PUL1_PIN = 1;  // Motor 1 PUL+
const int PUL2_PIN = 4;  // Motor 2 PUL+

// Two sensors at each end of the X-axis
const int END_GP2 = 2;   // First pair of sensors
const int END_GP3 = 3;   // New pair of sensors

const int RPM = 150;
const int PULSES_PER_REV = 200;

const int PULSE_FREQUENCY = (RPM * PULSES_PER_REV) / 60; // 100 Hz
const int PERIOD_US = 1000000 / PULSE_FREQUENCY;
const int PULSE_WIDTH_US = 10;

// Change these if the physical motion is opposite of what you expect
const int DIRECTION_TO_GP2 = HIGH;
const int DIRECTION_TO_GP3 = LOW;

const unsigned long TRAVEL_TIMEOUT_MS = 30000;

void stopMotors() {
  digitalWrite(PUL1_PIN, LOW);
  digitalWrite(PUL2_PIN, LOW);
}

bool moveUntilSwitch(int switchPin, int direction) {
  digitalWrite(DIR_PIN, direction);
  delay(10);

  unsigned long startTime = millis();

  // NPN switch triggers when its input becomes LOW
  while (digitalRead(switchPin) == HIGH) {
    // Stop safely if a switch is not reached
    if (millis() - startTime > TRAVEL_TIMEOUT_MS) {
      stopMotors();
      Serial.println("Travel timeout — motors stopped.");
      return false;
    }

    // Send one step pulse to both motors
    digitalWrite(PUL1_PIN, HIGH);
    digitalWrite(PUL2_PIN, HIGH);
    delayMicroseconds(PULSE_WIDTH_US);

    digitalWrite(PUL1_PIN, LOW);
    digitalWrite(PUL2_PIN, LOW);
    delayMicroseconds(PERIOD_US - PULSE_WIDTH_US);
  }

  stopMotors();
  return true;
}

void setup() {
  Serial.begin(115200);

  pinMode(DIR_PIN, OUTPUT);
  pinMode(PUL1_PIN, OUTPUT);
  pinMode(PUL2_PIN, OUTPUT);

  // Each GPIO has its own external 10k pull-up resistor to 3V3
  pinMode(END_GP2, INPUT);
  pinMode(END_GP3, INPUT);

  stopMotors();
}

void loop() {
  // Travel toward the first GP2 sensor pair
  Serial.println("Moving toward GP2 end...");
  if (!moveUntilSwitch(END_GP2, DIRECTION_TO_GP2)) {
    while (true) {}  // Timeout: remain stopped
  }

  delay(300);  // Brief pause before reversing

  // Travel toward the second GP3 sensor pair
  Serial.println("Moving toward GP3 end...");
  if (!moveUntilSwitch(END_GP3, DIRECTION_TO_GP3)) {
    while (true) {}  // Timeout: remain stopped
  }

  delay(300);  // Brief pause, then loop repeats
}