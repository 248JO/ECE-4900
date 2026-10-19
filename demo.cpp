
#include <Arduino.h>

// ---------- Motor 1 and Motor 2 ----------
const int DIR12_PIN = 0;  // Shared direction
const int PUL1_PIN  = 1;  // Motor 1
const int PUL2_PIN  = 4;  // Motor 2

// ---------- Motion settings ----------
const int DIRECTION = LOW;  // LOW = reverse, HIGH = forward
const int PULSE_WIDTH_US = 10;

// ---------- Move both motors one step ----------
void moveMotorsOneStep() {

  // Set direction for both motors
  digitalWrite(DIR12_PIN, DIRECTION);
  delay(10);

  // Send one step pulse to both motors
  digitalWrite(PUL1_PIN, HIGH);
  digitalWrite(PUL2_PIN, HIGH);

  delayMicroseconds(PULSE_WIDTH_US);

  digitalWrite(PUL1_PIN, LOW);
  digitalWrite(PUL2_PIN, LOW);
}

void setup() {
  Serial.begin(115200);

  pinMode(DIR12_PIN, OUTPUT);
  pinMode(PUL1_PIN, OUTPUT);
  pinMode(PUL2_PIN, OUTPUT);

  digitalWrite(PUL1_PIN, LOW);
  digitalWrite(PUL2_PIN, LOW);

  // Move both motors one step
  moveMotorsOneStep();

  Serial.println("Both motors moved one step!");
}

void loop() {
  // Nothing here - motors only move once
}
