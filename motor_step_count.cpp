// VERSION 2: EXACT STEP-COUNT MOVEMENT ON X, Y, Z.
// The original homing movement functions and MIN->MAX->MIN axis order are kept.
// Based on the working 200 pulses/rev, 150 RPM controller.
// Set TB6600 DIP switches for 200 pulses/revolution for a 200-full-step motor.
// X has two motors that both receive the same pulse; Y and Z have one each.
// Serial command changes to X(pulses),Y(pulses),Z(pulses).
// Y commanded movement now uses stepY() pulses rather than continuous PWM.

#include <Arduino.h>
#include <stdlib.h>
#include <errno.h>

// =====================================================
// X AXIS: Motors 1 and 2 move together
// =====================================================
const int X_DIR_PIN  = 16;
const int X_PUL1_PIN = 17;
const int X_PUL2_PIN = 4;

const int X_MIN_SWITCH = 2;   // GP2 = X home
const int X_MAX_SWITCH = 3;   // GP3 = X maximum

// =====================================================
// Y AXIS: Motor 3
// =====================================================
const int Y_DIR_PIN = 15;
const int Y_PUL_PIN = 14;

const int Y_MIN_SWITCH = 7;   // GP7 = Y home
const int Y_MAX_SWITCH = 8;   // GP8 = Y maximum

// =====================================================
// Z AXIS: Motor 4
// =====================================================
const int Z_PUL_PIN = 9;
const int Z_DIR_PIN = 10;

const int Z_MIN_SWITCH = 11;  // GP11 = Z home
const int Z_MAX_SWITCH = 12;  // GP12 = Z maximum

// =====================================================
// Motion settings
// =====================================================

// Must match the TB6600 DIP-switch pulse/revolution setting.
const int PULSES_PER_REV = 200;

// Start low and increase later only if motion stays reliable.
const int RPM = 150;

const unsigned long PULSE_FREQUENCY =
    (RPM * PULSES_PER_REV) / 60;

const unsigned long STEP_PERIOD_US =
    1000000UL / PULSE_FREQUENCY;

const unsigned long PULSE_WIDTH_US = 10;

const unsigned long TRAVEL_TIMEOUT_MS = 30000;
const unsigned long INPUT_DELAY_MS = 3000;

// Change HIGH/LOW if an axis moves away from the intended switch.
const int X_DIRECTION_TO_MIN = HIGH;
const int X_DIRECTION_TO_MAX = LOW;

const int Y_DIRECTION_TO_MIN = HIGH;
const int Y_DIRECTION_TO_MAX = LOW;

const int Z_DIRECTION_TO_MIN = HIGH;
const int Z_DIRECTION_TO_MAX = LOW;

// =====================================================
// Measured travel time (for diagnostics) and step travel range.
// Homing movement and order are unchanged from the working code.
// =====================================================
unsigned long xMaxTimeMs = 0;
unsigned long yMaxTimeMs = 0;
unsigned long zMaxTimeMs = 0;
unsigned long xMaxSteps = 0;
unsigned long yMaxSteps = 0;
unsigned long zMaxSteps = 0;

// Only enabled during each MIN -> MAX calibration pass.
bool countingXCalibration = false;
bool countingYCalibration = false;
bool countingZCalibration = false;

// Relative commanded positions, in DRIVER PULSES from home.
long currentXSteps = 0;
long currentYSteps = 0;
long currentZSteps = 0;

String inputBuffer = "";

// NPN photoelectric switch: LOW = triggered
bool isTriggered(int switchPin) {
  return digitalRead(switchPin) == LOW;
}

void stopAll() {
  digitalWrite(X_PUL1_PIN, LOW);
  digitalWrite(X_PUL2_PIN, LOW);
  digitalWrite(Y_PUL_PIN, LOW);
  digitalWrite(Z_PUL_PIN, LOW);
}

void stepX() {
  digitalWrite(X_PUL1_PIN, HIGH);
  digitalWrite(X_PUL2_PIN, HIGH);
  delayMicroseconds(PULSE_WIDTH_US);

  digitalWrite(X_PUL1_PIN, LOW);
  digitalWrite(X_PUL2_PIN, LOW);
  if (countingXCalibration) ++xMaxSteps;
}

void stepY() {
  digitalWrite(Y_PUL_PIN, HIGH);
  delayMicroseconds(PULSE_WIDTH_US);
  digitalWrite(Y_PUL_PIN, LOW);
  if (countingYCalibration) ++yMaxSteps;
}

void stepZ() {
  digitalWrite(Z_PUL_PIN, HIGH);
  delayMicroseconds(PULSE_WIDTH_US);
  digitalWrite(Z_PUL_PIN, LOW);
  if (countingZCalibration) ++zMaxSteps;
}

// =====================================================
// Move toward one specific limit switch
// Used for homing and initial travel-time measurement.
// =====================================================
bool moveXUntilSwitch(int targetSwitch, int direction,
                      unsigned long &elapsedMs) {
  digitalWrite(X_DIR_PIN, direction);
  delay(10);

  unsigned long startTime = millis();

  while (!isTriggered(targetSwitch)) {
    if (millis() - startTime > TRAVEL_TIMEOUT_MS) {
      stopAll();
      Serial.println("ERROR: X timeout.");
      return false;
    }

    stepX();
    delayMicroseconds(STEP_PERIOD_US - PULSE_WIDTH_US);
  }

  elapsedMs = millis() - startTime;

  digitalWrite(X_PUL1_PIN, LOW);
  digitalWrite(X_PUL2_PIN, LOW);
  return true;
}

bool moveYUntilSwitch(int targetSwitch, int direction,
                      unsigned long &elapsedMs) {
  digitalWrite(Y_DIR_PIN, direction);
  delay(10);

  unsigned long startTime = millis();

  while (!isTriggered(targetSwitch)) {
    if (millis() - startTime > TRAVEL_TIMEOUT_MS) {
      stopAll();
      Serial.println("ERROR: Y timeout.");
      return false;
    }

    stepY();
    delayMicroseconds(STEP_PERIOD_US - PULSE_WIDTH_US);
  }

  elapsedMs = millis() - startTime;

  digitalWrite(Y_PUL_PIN, LOW);
  return true;
}

bool moveZUntilSwitch(int targetSwitch, int direction,
                      unsigned long &elapsedMs) {
  digitalWrite(Z_DIR_PIN, direction);
  delay(10);

  unsigned long startTime = millis();

  while (!isTriggered(targetSwitch)) {
    if (millis() - startTime > TRAVEL_TIMEOUT_MS) {
      stopAll();
      Serial.println("ERROR: Z timeout.");
      return false;
    }

    stepZ();
    delayMicroseconds(STEP_PERIOD_US - PULSE_WIDTH_US);
  }

  elapsedMs = millis() - startTime;

  digitalWrite(Z_PUL_PIN, LOW);
  return true;
}

// =====================================================
// Home each axis
// =====================================================
bool homeX() {
  unsigned long ignoredTime = 0;

  Serial.println("Homing X to GP2...");
  if (!moveXUntilSwitch(X_MIN_SWITCH, X_DIRECTION_TO_MIN, ignoredTime)) {
    return false;
  }

  currentXSteps = 0;
  Serial.println("X homed.");
  delay(500);
  return true;
}

bool homeY() {
  unsigned long ignoredTime = 0;

  Serial.println("Homing Y to GP7...");
  if (!moveYUntilSwitch(Y_MIN_SWITCH, Y_DIRECTION_TO_MIN, ignoredTime)) {
    return false;
  }

  currentYSteps = 0;
  Serial.println("Y homed.");
  delay(500);
  return true;
}

bool homeZ() {
  unsigned long ignoredTime = 0;

  Serial.println("Homing Z to GP11...");
  if (!moveZUntilSwitch(Z_MIN_SWITCH, Z_DIRECTION_TO_MIN, ignoredTime)) {
    return false;
  }

  currentZSteps = 0;
  Serial.println("Z homed.");
  delay(500);
  return true;
}

// =====================================================
// Measure the travel time from home/minimum to maximum,
// then return the axis home.
// =====================================================
bool calibrateTravelTimes() {
  if (!homeX()) return false;

  Serial.println("Measuring X: GP2 to GP3...");
  xMaxSteps = 0;
  countingXCalibration = true;
  bool xMeasured = moveXUntilSwitch(
      X_MAX_SWITCH, X_DIRECTION_TO_MAX, xMaxTimeMs);
  countingXCalibration = false;
  if (!xMeasured) return false;
  if (xMaxSteps == 0) {
    Serial.println("ERROR: X calibrated to zero pulses.");
    return false;
  }

  if (xMaxTimeMs == 0) {
    Serial.println("ERROR: X travel time measured as zero.");
    return false;
  }

  Serial.print("X full travel time: ");
  Serial.print(xMaxTimeMs);
  Serial.println(" ms");
  Serial.print("X full travel pulses: ");
  Serial.println(xMaxSteps);

  if (!homeX()) return false;

  if (!homeY()) return false;

  Serial.println("Measuring Y: GP7 to GP8...");
  yMaxSteps = 0;
  countingYCalibration = true;
  bool yMeasured = moveYUntilSwitch(
      Y_MAX_SWITCH, Y_DIRECTION_TO_MAX, yMaxTimeMs);
  countingYCalibration = false;
  if (!yMeasured) return false;
  if (yMaxSteps == 0) {
    Serial.println("ERROR: Y calibrated to zero pulses.");
    return false;
  }

  if (yMaxTimeMs == 0) {
    Serial.println("ERROR: Y travel time measured as zero.");
    return false;
  }

  Serial.print("Y full travel time: ");
  Serial.print(yMaxTimeMs);
  Serial.println(" ms");
  Serial.print("Y full travel pulses: ");
  Serial.println(yMaxSteps);

  if (!homeY()) return false;

  if (!homeZ()) return false;

  Serial.println("Measuring Z: GP11 to GP12...");
  zMaxSteps = 0;
  countingZCalibration = true;
  bool zMeasured = moveZUntilSwitch(
      Z_MAX_SWITCH, Z_DIRECTION_TO_MAX, zMaxTimeMs);
  countingZCalibration = false;
  if (!zMeasured) return false;
  if (zMaxSteps == 0) {
    Serial.println("ERROR: Z calibrated to zero pulses.");
    return false;
  }

  if (zMaxTimeMs == 0) {
    Serial.println("ERROR: Z travel time measured as zero.");
    return false;
  }

  Serial.print("Z full travel time: ");
  Serial.print(zMaxTimeMs);
  Serial.println(" ms");
  Serial.print("Z full travel pulses: ");
  Serial.println(zMaxSteps);

  if (!homeZ()) return false;

  return true;
}

// =====================================================
// VERSION 2: commanded movements by COUNTED DRIVER PULSES.
// Uses the SAME stepX/stepY/stepZ functions and pulse timing as homing.
// The Y hardware PWM is not used in this version, because time-limited
// PWM cannot guarantee an exact integer number of step pulses.
// =====================================================

typedef void (*StepFn)();

bool moveAxisForSteps(const char* axisName,
                      int dirPin, int minSwitch, int maxSwitch,
                      int dirToMin, int dirToMax,
                      long &currentSteps, unsigned long maxSteps,
                      long requestedSignedSteps, StepFn stepFn) {
  if (requestedSignedSteps == 0) return true;

  const bool towardMax = requestedSignedSteps > 0;
  // int64_t prevents overflow when negating LONG_MIN on a 32-bit board.
  const unsigned long pulseCount = towardMax
      ? (unsigned long)requestedSignedSteps
      : (unsigned long)(-(int64_t)requestedSignedSteps);

  const int64_t target =
      (int64_t)currentSteps + (int64_t)requestedSignedSteps;
  if (target < 0 || target > (int64_t)maxSteps) {
    Serial.print("ERROR: "); Serial.print(axisName);
    Serial.println(" target outside calibrated travel.");
    return false;
  }

  // Never drive farther INTO an already-active end switch.
  if ((towardMax && isTriggered(maxSwitch)) ||
      (!towardMax && isTriggered(minSwitch)) ||
      (isTriggered(minSwitch) && isTriggered(maxSwitch))) {
    Serial.print("ERROR: "); Serial.print(axisName);
    Serial.println(" requested direction blocked by switch.");
    return false;
  }

  digitalWrite(dirPin, towardMax ? dirToMax : dirToMin);
  delay(10);  // Same direction setup as homing.

  // We allow moving AWAY from an already-active switch.
  bool minWasClear = !isTriggered(minSwitch);
  bool maxWasClear = !isTriggered(maxSwitch);
  const unsigned long startedAt = millis();

  for (unsigned long i = 0; i < pulseCount; ++i) {
    if (millis() - startedAt > TRAVEL_TIMEOUT_MS) {
      stopAll();
      Serial.print("ERROR: "); Serial.print(axisName);
      Serial.println(" step movement timed out. Re-home before continuing.");
      return false;
    }

    // Exit immediately if a limit newly becomes active.
    if ((minWasClear && isTriggered(minSwitch)) ||
        (maxWasClear && isTriggered(maxSwitch))) {
      stopAll();
      if (isTriggered(minSwitch)) currentSteps = 0;
      if (isTriggered(maxSwitch)) currentSteps = (long)maxSteps;
      Serial.print("STOP: "); Serial.print(axisName);
      Serial.println(" switch triggered during command.");
      return false;
    }

    stepFn();  // X sends one pulse to BOTH X motors; Y and Z each pulse once.
    currentSteps += towardMax ? 1L : -1L;

    if (!isTriggered(minSwitch)) minWasClear = true;
    if (!isTriggered(maxSwitch)) maxWasClear = true;

    delayMicroseconds(STEP_PERIOD_US - PULSE_WIDTH_US);

    // Check even after the LAST pulse of the commanded movement.
    if ((minWasClear && isTriggered(minSwitch)) ||
        (maxWasClear && isTriggered(maxSwitch))) {
      stopAll();
      if (isTriggered(minSwitch)) currentSteps = 0;
      if (isTriggered(maxSwitch)) currentSteps = (long)maxSteps;
      Serial.print("STOP: "); Serial.print(axisName);
      Serial.println(" switch triggered; position set to endpoint.");
      return false;
    }
  }
  stopAll();
  return true;
}

bool moveXForSteps(long steps) {
  return moveAxisForSteps("X", X_DIR_PIN, X_MIN_SWITCH, X_MAX_SWITCH,
      X_DIRECTION_TO_MIN, X_DIRECTION_TO_MAX,
      currentXSteps, xMaxSteps, steps, stepX);
}

bool moveYForSteps(long steps) {
  return moveAxisForSteps("Y", Y_DIR_PIN, Y_MIN_SWITCH, Y_MAX_SWITCH,
      Y_DIRECTION_TO_MIN, Y_DIRECTION_TO_MAX,
      currentYSteps, yMaxSteps, steps, stepY);
}

bool moveZForSteps(long steps) {
  return moveAxisForSteps("Z", Z_DIR_PIN, Z_MIN_SWITCH, Z_MAX_SWITCH,
      Z_DIRECTION_TO_MIN, Z_DIRECTION_TO_MAX,
      currentZSteps, zMaxSteps, steps, stepZ);
}

void printPrompt() {
  Serial.println();
  Serial.println("========== STEP POSITION ==========");
  Serial.print("X = "); Serial.print(currentXSteps);
  Serial.print(" pulses, maximum = "); Serial.println(xMaxSteps);
  Serial.print("Y = "); Serial.print(currentYSteps);
  Serial.print(" pulses, maximum = "); Serial.println(yMaxSteps);
  Serial.print("Z = "); Serial.print(currentZSteps);
  Serial.print(" pulses, maximum = "); Serial.println(zMaxSteps);
  Serial.println("===================================");
  Serial.println("Enter RELATIVE driver pulses: X,Y,Z");
  Serial.println("Example: 0,20,0 or 100,-50,25");
  Serial.println("Positive = toward maximum; negative = toward home.");
  Serial.println("X moves first, then Y, then Z.");
  Serial.print("> ");
}

bool parseSignedSteps(const String &text, long &result) {
  if (text.length() == 0) return false;
  errno = 0;
  char *end = nullptr;
  long parsed = strtol(text.c_str(), &end, 10);
  if (errno == ERANGE || *end != '\0') return false;
  result = parsed;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(5000);

  pinMode(X_DIR_PIN, OUTPUT);
  pinMode(X_PUL1_PIN, OUTPUT);
  pinMode(X_PUL2_PIN, OUTPUT);
  pinMode(Y_DIR_PIN, OUTPUT);
  pinMode(Y_PUL_PIN, OUTPUT);
  pinMode(Z_DIR_PIN, OUTPUT);
  pinMode(Z_PUL_PIN, OUTPUT);

  // NPN sensors: external 10 kOhm pull-ups to 3.3 V required.
  pinMode(X_MIN_SWITCH, INPUT);
  pinMode(X_MAX_SWITCH, INPUT);
  pinMode(Y_MIN_SWITCH, INPUT);
  pinMode(Y_MAX_SWITCH, INPUT);
  pinMode(Z_MIN_SWITCH, INPUT);
  pinMode(Z_MAX_SWITCH, INPUT);

  stopAll();
  Serial.println();
  Serial.println("Starting original X->Y->Z homing/calibration, counting pulses...");

  if (!calibrateTravelTimes()) {
    stopAll();
    Serial.println("ERROR: Calibration failed. Motors stopped.");
    while (true) { delay(1000); }
  }

  Serial.println("Calibration complete. All axes are home.");
  Serial.println("Waiting 3 seconds before first input...");
  delay(INPUT_DELAY_MS);
  printPrompt();
}

void loop() {
  while (Serial.available() > 0) {
    char character = Serial.read();
    if (character != '\r' && character != '\n') {
      if (inputBuffer.length() < 63) {
        inputBuffer += character;
        Serial.print(character);
      }
      continue;
    }

    if (inputBuffer.length() == 0) continue;
    Serial.println();
    String input = inputBuffer;
    inputBuffer = "";
    input.trim();
    input.replace("(", "");
    input.replace(")", "");
    input.trim();

    int comma1 = input.indexOf(',');
    int comma2 = input.indexOf(',', comma1 + 1);
    if (comma1 == -1 || comma2 == -1 ||
        input.indexOf(',', comma2 + 1) != -1) {
      Serial.println("Invalid format. Example: 100,-50,25");
      delay(INPUT_DELAY_MS);
      printPrompt();
      continue;
    }

    String xText = input.substring(0, comma1);
    String yText = input.substring(comma1 + 1, comma2);
    String zText = input.substring(comma2 + 1);
    xText.trim(); yText.trim(); zText.trim();

    long xSteps = 0, ySteps = 0, zSteps = 0;
    if (!parseSignedSteps(xText, xSteps) ||
        !parseSignedSteps(yText, ySteps) ||
        !parseSignedSteps(zText, zSteps)) {
      Serial.println("Invalid input. Use signed WHOLE pulse counts.");
    } else {
      const int64_t targetX = (int64_t)currentXSteps + xSteps;
      const int64_t targetY = (int64_t)currentYSteps + ySteps;
      const int64_t targetZ = (int64_t)currentZSteps + zSteps;
      if (targetX < 0 || targetX > (int64_t)xMaxSteps ||
          targetY < 0 || targetY > (int64_t)yMaxSteps ||
          targetZ < 0 || targetZ > (int64_t)zMaxSteps) {
        Serial.println("ERROR: Requested endpoint outside calibrated travel.");
      } else {
        Serial.println("Moving X, then Y, then Z (exact pulses)...");
        // Do not move the next axis if a switch or timeout stops this one.
        bool ok = moveXForSteps(xSteps);
        if (ok) ok = moveYForSteps(ySteps);
        if (ok) ok = moveZForSteps(zSteps);
        stopAll();
        if (ok) Serial.println("Movement complete.");
        else Serial.println("Interrupted. Check switches and re-home if needed.");
      }
    }

    Serial.println("Waiting 3 seconds before next input...");
    delay(INPUT_DELAY_MS);
    printPrompt();
  }
}
