#include <Arduino.h>

// TB6600 control pins
const int DIR_PIN  = 0;  // Shared direction
const int PUL1_PIN = 1;  // Motor 1
const int PUL2_PIN = 4;  // Motor 2

// Limit switches
const int END_GP2 = 2;
const int END_GP3 = 3;

// Motor settings
const int RPM = 150;
const int STEPS_PER_REV = 200;
const int REVOLUTIONS = 1;

// Total number of steps
const int TOTAL_STEPS = STEPS_PER_REV * REVOLUTIONS;

// Pulse timing
const unsigned long PERIOD_US =
    60000000UL / (RPM * STEPS_PER_REV);

const int PULSE_WIDTH_US = 10;

// Motor directions
const int DIRECTION_TO_GP2 = HIGH;  //swap to move in opposite direction
const int DIRECTION_TO_GP3 = LOW;

void stopMotors() {
  digitalWrite(PUL1_PIN, LOW);
  digitalWrite(PUL2_PIN, LOW);
}

// Move both motors together
bool moveMotors(int direction, int limitPin) {

  digitalWrite(DIR_PIN, direction);
  delay(10);

  for (int step = 0; step < TOTAL_STEPS; step++) {

    // Stop if the limit switch is triggered
    if (digitalRead(limitPin) == LOW) {
      stopMotors();
      Serial.println("Limit switch triggered!");
      return false;
    }

    // Send step pulse to both motors
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

  pinMode(END_GP2, INPUT);
  pinMode(END_GP3, INPUT);

  stopMotors();

  Serial.println("Motor control ready!");
}

void loop() {

  // Move both motors toward GP2
  Serial.println("Moving both motors forward...");

  moveMotors(DIRECTION_TO_GP2, END_GP2);

  delay(300);

  // Move both motors toward GP3
  Serial.println("Moving both motors backward...");

  moveMotors(DIRECTION_TO_GP3, END_GP3);

  delay(300);
}
