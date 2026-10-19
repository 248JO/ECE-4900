#include <Arduino.h>

// ---------- Motor 1 and Motor 2 ----------
// These two motors share the same direction pin
const int DIR12_PIN = 0;
const int PUL1_PIN  = 1;
const int PUL2_PIN  = 4;

// End sensors for Motors 1 and 2
const int END12_GP2 = 2;   // One end of their travel
const int END12_GP3 = 3;   // Opposite end of their travel

// ---------- Motor 3 ----------
const int DIR3_PIN = 6;
const int PUL3_PIN = 5;

// End sensors for Motor 3
const int END3_GP7 = 7;    // One end of Motor 3 travel
const int END3_GP8 = 8;    // Opposite end of Motor 3 travel

// ---------- Motion settings ----------
const int RPM = 150;
const int PULSES_PER_REV = 200;

const int PULSE_FREQUENCY = (RPM * PULSES_PER_REV) / 60; // 500 Hz
const int PERIOD_US = 1000000 / PULSE_FREQUENCY;
const int PULSE_WIDTH_US = 10;

const unsigned long TRAVEL_TIMEOUT_MS = 30000;

// Change HIGH/LOW if a motor moves away from its intended switch
const int DIRECTION_12_TO_GP2 = HIGH;
const int DIRECTION_12_TO_GP3 = LOW;

const int DIRECTION_3_TO_GP7 = HIGH;
const int DIRECTION_3_TO_GP8 = LOW;

// ---------- Stop functions ----------
void stopMotors12() {
  digitalWrite(PUL1_PIN, LOW);
  digitalWrite(PUL2_PIN, LOW);
}

void stopMotor3() {
  digitalWrite(PUL3_PIN, LOW);
}

void stopAllMotors() {
  stopMotors12();
  stopMotor3();
}

// ---------- Motors 1 and 2 move together ----------
bool moveMotors12UntilSwitch(int switchPin, int direction) {
  digitalWrite(DIR12_PIN, direction);
  delay(10);

  unsigned long startTime = millis();

  // NPN photoelectric switch: LOW means triggered
  while (digitalRead(switchPin) == HIGH) {
    if (millis() - startTime > TRAVEL_TIMEOUT_MS) {
      stopAllMotors();
      Serial.println("Motor 1/2 timeout — all motors stopped.");
      return false;
    }

    // One step pulse to both Motor 1 and Motor 2
    digitalWrite(PUL1_PIN, HIGH);
    digitalWrite(PUL2_PIN, HIGH);
    delayMicroseconds(PULSE_WIDTH_US);

    digitalWrite(PUL1_PIN, LOW);
    digitalWrite(PUL2_PIN, LOW);
    delayMicroseconds(PERIOD_US - PULSE_WIDTH_US);
  }

  stopMotors12();
  return true;
}

// ---------- Motor 3 ----------
bool moveMotor3UntilSwitch(int switchPin, int direction) {
  digitalWrite(DIR3_PIN, direction);
  delay(10);

  unsigned long startTime = millis();

  // NPN photoelectric switch: LOW means triggered
  while (digitalRead(switchPin) == HIGH) {
    if (millis() - startTime > TRAVEL_TIMEOUT_MS) {
      stopAllMotors();
      Serial.println("Motor 3 timeout — all motors stopped.");
      return false;
    }

    digitalWrite(PUL3_PIN, HIGH);
    delayMicroseconds(PULSE_WIDTH_US);

    digitalWrite(PUL3_PIN, LOW);
    delayMicroseconds(PERIOD_US - PULSE_WIDTH_US);
  }

  stopMotor3();
  return true;
}

void setup() {
  Serial.begin(115200);

  // Motor outputs
  pinMode(DIR12_PIN, OUTPUT);
  pinMode(PUL1_PIN, OUTPUT);
  pinMode(PUL2_PIN, OUTPUT);

  pinMode(DIR3_PIN, OUTPUT);
  pinMode(PUL3_PIN, OUTPUT);

  // Sensor inputs
  // Each sensor-output pin needs its own 10 kΩ pull-up to 3.3 V.
  pinMode(END12_GP2, INPUT);
  pinMode(END12_GP3, INPUT);
  pinMode(END3_GP7, INPUT);
  pinMode(END3_GP8, INPUT);

  stopAllMotors();
}

void loop() {
  // ----- Motors 1 and 2: move back and forth together -----
  Serial.println("Motors 1 and 2 moving toward GP2...");
  if (!moveMotors12UntilSwitch(END12_GP2, DIRECTION_12_TO_GP2)) {
    while (true) {}
  }

  delay(300);

  Serial.println("Motors 1 and 2 moving toward GP3...");
  if (!moveMotors12UntilSwitch(END12_GP3, DIRECTION_12_TO_GP3)) {
    while (true) {}
  }

  delay(300);

  // ----- Motor 3: move back and forth -----
  Serial.println("Motor 3 moving toward GP7...");
  if (!moveMotor3UntilSwitch(END3_GP7, DIRECTION_3_TO_GP7)) {
    while (true) {}
  }

  delay(300);

  Serial.println("Motor 3 moving toward GP8...");
  if (!moveMotor3UntilSwitch(END3_GP8, DIRECTION_3_TO_GP8)) {
    while (true) {}
  }

  delay(300);
}