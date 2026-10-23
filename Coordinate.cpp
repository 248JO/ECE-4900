#include <Arduino.h>

// =====================================================
// X AXIS: Motors 1 and 2 move together
// =====================================================
const int X_DIR_PIN  = 0;
const int X_PUL1_PIN = 1;
const int X_PUL2_PIN = 4;

const int X_MIN_SWITCH = 2;  // GP2 = X = 0
const int X_MAX_SWITCH = 3;  // GP3 = X = 20

// =====================================================
// Y AXIS: Motor 3
// =====================================================
const int Y_DIR_PIN = 6;
const int Y_PUL_PIN = 5;

const int Y_MIN_SWITCH = 7;  // GP7 = Y = 0
const int Y_MAX_SWITCH = 8;  // GP8 = Y = 20

// =====================================================
// Motion settings
// =====================================================
const int RPM = 150;
const int PULSES_PER_REV = 200;

const unsigned long PULSE_FREQUENCY =
    (RPM * PULSES_PER_REV) / 60;

const unsigned long STEP_PERIOD_US = 1000000UL / PULSE_FREQUENCY;
const unsigned long PULSE_WIDTH_US = 10;

const unsigned long TRAVEL_TIMEOUT_MS = 30000;
const unsigned long SWITCH_DELAY_MS = 1000;

// Change HIGH/LOW if an axis moves away from its intended switch.
const int X_DIRECTION_TO_MIN = HIGH;
const int X_DIRECTION_TO_MAX = LOW;

const int Y_DIRECTION_TO_MIN = HIGH;
const int Y_DIRECTION_TO_MAX = LOW;

// Coordinate range
const float COORDINATE_MIN = 0.0;
const float COORDINATE_MAX = 20.0;

// Measured during startup
unsigned long xMaxSteps = 0;
unsigned long yMaxSteps = 0;

// Stores Serial characters until Enter is pressed
String inputBuffer = "";

// =====================================================
// Basic functions
// =====================================================

// NPN photoelectric switch: LOW = triggered
bool isTriggered(int switchPin) {
  return digitalRead(switchPin) == LOW;
}

void stopAll() {
  digitalWrite(X_PUL1_PIN, LOW);
  digitalWrite(X_PUL2_PIN, LOW);
  digitalWrite(Y_PUL_PIN, LOW);
}

void stepX() {
  digitalWrite(X_PUL1_PIN, HIGH);
  digitalWrite(X_PUL2_PIN, HIGH);
  delayMicroseconds(PULSE_WIDTH_US);

  digitalWrite(X_PUL1_PIN, LOW);
  digitalWrite(X_PUL2_PIN, LOW);
}

void stepY() {
  digitalWrite(Y_PUL_PIN, HIGH);
  delayMicroseconds(PULSE_WIDTH_US);
  digitalWrite(Y_PUL_PIN, LOW);
}

// =====================================================
// Move X until selected switch is triggered
// =====================================================
bool moveXUntilSwitch(int switchPin, int direction, unsigned long &stepCount) {
  digitalWrite(X_DIR_PIN, direction);
  delay(10);

  stepCount = 0;
  unsigned long startTime = millis();

  while (!isTriggered(switchPin)) {
    if (millis() - startTime > TRAVEL_TIMEOUT_MS) {
      stopAll();
      Serial.println("ERROR: X-axis timeout. Motors stopped.");
      return false;
    }

    stepX();
    stepCount++;

    delayMicroseconds(STEP_PERIOD_US - PULSE_WIDTH_US);
  }

  digitalWrite(X_PUL1_PIN, LOW);
  digitalWrite(X_PUL2_PIN, LOW);

  Serial.println("X switch detected. Waiting 1 second...");
  delay(SWITCH_DELAY_MS);

  return true;
}

// =====================================================
// Move Y until selected switch is triggered
// =====================================================
bool moveYUntilSwitch(int switchPin, int direction, unsigned long &stepCount) {
  digitalWrite(Y_DIR_PIN, direction);
  delay(10);

  stepCount = 0;
  unsigned long startTime = millis();

  while (!isTriggered(switchPin)) {
    if (millis() - startTime > TRAVEL_TIMEOUT_MS) {
      stopAll();
      Serial.println("ERROR: Y-axis timeout. Motors stopped.");
      return false;
    }

    stepY();
    stepCount++;

    delayMicroseconds(STEP_PERIOD_US - PULSE_WIDTH_US);
  }

  digitalWrite(Y_PUL_PIN, LOW);

  Serial.println("Y switch detected. Waiting 1 second...");
  delay(SWITCH_DELAY_MS);

  return true;
}

// =====================================================
// Home to coordinate (0,0)
// =====================================================
bool homeXMin() {
  unsigned long ignoredSteps;

  Serial.println("Homing X to GP2: X = 0");
  return moveXUntilSwitch(X_MIN_SWITCH, X_DIRECTION_TO_MIN, ignoredSteps);
}

bool homeYMin() {
  unsigned long ignoredSteps;

  Serial.println("Homing Y to GP7: Y = 0");
  return moveYUntilSwitch(Y_MIN_SWITCH, Y_DIRECTION_TO_MIN, ignoredSteps);
}

// =====================================================
// Measure full travel distance at startup
// =====================================================
bool calibrateAxes() {
  if (!homeXMin()) return false;

  Serial.println("Measuring X from GP2 to GP3...");
  if (!moveXUntilSwitch(X_MAX_SWITCH, X_DIRECTION_TO_MAX, xMaxSteps)) {
    return false;
  }

  Serial.print("X maximum steps: ");
  Serial.println(xMaxSteps);

  if (!homeXMin()) return false;

  if (!homeYMin()) return false;

  Serial.println("Measuring Y from GP7 to GP8...");
  if (!moveYUntilSwitch(Y_MAX_SWITCH, Y_DIRECTION_TO_MAX, yMaxSteps)) {
    return false;
  }

  Serial.print("Y maximum steps: ");
  Serial.println(yMaxSteps);

  if (!homeYMin()) return false;

  return true;
}

// =====================================================
// Move both axes from (0,0) to the requested coordinate
// =====================================================
bool moveToCoordinate(float xCoordinate, float yCoordinate) {
  unsigned long xTargetSteps =
      (unsigned long)((xCoordinate / COORDINATE_MAX) * xMaxSteps + 0.5);

  unsigned long yTargetSteps =
      (unsigned long)((yCoordinate / COORDINATE_MAX) * yMaxSteps + 0.5);

  if (xTargetSteps > xMaxSteps || yTargetSteps > yMaxSteps) {
    Serial.println("ERROR: Coordinate is outside maximum travel.");
    return false;
  }

  Serial.print("Moving to coordinate: (");
  Serial.print(xCoordinate);
  Serial.print(", ");
  Serial.print(yCoordinate);
  Serial.println(")");

  digitalWrite(X_DIR_PIN, X_DIRECTION_TO_MAX);
  digitalWrite(Y_DIR_PIN, Y_DIRECTION_TO_MAX);
  delay(10);

  unsigned long xStepsMoved = 0;
  unsigned long yStepsMoved = 0;

  unsigned long xLastStep = micros();
  unsigned long yLastStep = micros();
  unsigned long startTime = millis();

  while (xStepsMoved < xTargetSteps || yStepsMoved < yTargetSteps) {
    if (millis() - startTime > TRAVEL_TIMEOUT_MS) {
      stopAll();
      Serial.println("ERROR: Coordinate movement timeout.");
      return false;
    }

    unsigned long now = micros();

    if (xStepsMoved < xTargetSteps &&
        now - xLastStep >= STEP_PERIOD_US) {

      if (isTriggered(X_MAX_SWITCH)) {
        stopAll();
        Serial.println("ERROR: X reached GP3 before target.");
        return false;
      }

      stepX();
      xStepsMoved++;
      xLastStep = now;
    }

    if (yStepsMoved < yTargetSteps &&
        now - yLastStep >= STEP_PERIOD_US) {

      if (isTriggered(Y_MAX_SWITCH)) {
        stopAll();
        Serial.println("ERROR: Y reached GP8 before target.");
        return false;
      }

      stepY();
      yStepsMoved++;
      yLastStep = now;
    }
  }

  stopAll();

  Serial.print("Reached coordinate: (");
  Serial.print(xCoordinate);
  Serial.print(", ");
  Serial.print(yCoordinate);
  Serial.println(")");

  return true;
}

void requestCoordinate() {
  Serial.println();
  Serial.println("Enter coordinate as x,y from 0 to 20.");
  Serial.println("Example: 10,15");
}

// =====================================================
// Startup
// =====================================================
void setup() {
  Serial.begin(115200);

  pinMode(X_DIR_PIN, OUTPUT);
  pinMode(X_PUL1_PIN, OUTPUT);
  pinMode(X_PUL2_PIN, OUTPUT);

  pinMode(Y_DIR_PIN, OUTPUT);
  pinMode(Y_PUL_PIN, OUTPUT);

  // External 10 kΩ pull-up resistors to 3.3 V required.
  pinMode(X_MIN_SWITCH, INPUT);
  pinMode(X_MAX_SWITCH, INPUT);
  pinMode(Y_MIN_SWITCH, INPUT);
  pinMode(Y_MAX_SWITCH, INPUT);

  stopAll();

  Serial.println("Waiting 1 second before starting...");
  delay(SWITCH_DELAY_MS);

  Serial.println("Starting calibration...");

  if (!calibrateAxes()) {
    Serial.println("Calibration failed. Motors stopped.");
    while (true) {}
  }

  Serial.println("Calibration complete. Current position is (0,0).");
  requestCoordinate();
}

// =====================================================
// Read Serial input, home axes, then move to coordinate
// =====================================================
void loop() {
  while (Serial.available() > 0) {
    char character = Serial.read();

    // Add characters until Return/Enter is received.
    if (character != '\r' && character != '\n') {
      inputBuffer += character;

      // Echo what you type in case your serial monitor does not.
      Serial.print(character);
      continue;
    }

    // Ignore the second character of CR+LF.
    if (inputBuffer.length() == 0) {
      continue;
    }

    Serial.println();

    String input = inputBuffer;
    inputBuffer = "";

    input.trim();
    input.replace("(", "");
    input.replace(")", "");

    // -------- FIX: manually split x,y; do not use sscanf %f --------
    int commaPosition = input.indexOf(',');

    // Must have exactly one comma.
    if (commaPosition == -1 ||
        input.indexOf(',', commaPosition + 1) != -1) {
      Serial.println("Invalid format. Use x,y — example: 10,15");
      requestCoordinate();
      continue;
    }

    String xText = input.substring(0, commaPosition);
    String yText = input.substring(commaPosition + 1);

    xText.trim();
    yText.trim();

    if (xText.length() == 0 || yText.length() == 0) {
      Serial.println("Invalid format. Use x,y — example: 10,15");
      requestCoordinate();
      continue;
    }

    float xInput = xText.toFloat();
    float yInput = yText.toFloat();
    // ---------------------------------------------------------------

    if (xInput < COORDINATE_MIN || xInput > COORDINATE_MAX ||
        yInput < COORDINATE_MIN || yInput > COORDINATE_MAX) {

      Serial.println("Invalid coordinate. X and Y must both be 0 to 20.");
      requestCoordinate();
      continue;
    }

    Serial.println("Homing both axes to (0,0)...");

    if (!homeXMin() || !homeYMin()) {
      Serial.println("Homing failed. Motors stopped.");
      while (true) {}
    }

    Serial.println("Home complete: current position is (0,0).");

    if (!moveToCoordinate(xInput, yInput)) {
      Serial.println("Movement failed. Motors stopped.");
      while (true) {}
    }

    requestCoordinate();
  }
}