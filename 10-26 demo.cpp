#include <Arduino.h>
#include <stdlib.h>  // strtoul for integer step command parsing
#include <errno.h>   // ERANGE for overflow detection

// =====================================================
// X AXIS: Motors 1 and 2 move together
// =====================================================
const int X_DIR_PIN  = 0;
const int X_PUL1_PIN = 1;
const int X_PUL2_PIN = 4;

const int X_MIN_SWITCH = 2;  // GP2 = X = 0
const int X_MAX_SWITCH = 3;  // GP3 = X maximum (calibrated steps)

// =====================================================
// Y AXIS: Motor 3
// =====================================================
const int Y_DIR_PIN = 6;
const int Y_PUL_PIN = 5;

const int Y_MIN_SWITCH = 7;  // GP7 = Y = 0
const int Y_MAX_SWITCH = 8;  // GP8 = Y maximum (calibrated steps)

// =====================================================
// Z AXIS: Motor 4
// =====================================================
const int Z_PUL_PIN = 9;
const int Z_DIR_PIN = 10;

const int Z_MIN_SWITCH = 11;  // GP11 = Z = 0
const int Z_MAX_SWITCH = 12;  // GP12 = Z maximum (calibrated steps)

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

// Reverse these two values if Z moves away from its selected switch.
const int Z_DIRECTION_TO_MIN = HIGH;
const int Z_DIRECTION_TO_MAX = LOW;

// Position coordinates are DIRECT step-pulse counts from home:
// 1 coordinate unit = 1 complete step pulse.
// Each axis has its own maximum measured between its two limit switches.
// No fixed 0-to-20 coordinate scaling is used.

// OPTIONAL: Measure the physical switch-to-switch travel with a ruler
// and enter it in millimeters. Leave 0 if the distance is unknown.
// For example, if X moves 40 mm between GP2 and GP3, use 40.0f.
// These constants affect REPORTING ONLY, not motion or calibration.
const float X_FULL_TRAVEL_MM = 0.0f;
const float Y_FULL_TRAVEL_MM = 0.0f;
const float Z_FULL_TRAVEL_MM = 0.0f;

// Measured during startup
unsigned long xMaxSteps = 0;
unsigned long yMaxSteps = 0;
unsigned long zMaxSteps = 0;

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
  digitalWrite(Z_PUL_PIN, LOW);
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

void stepZ() {
  digitalWrite(Z_PUL_PIN, HIGH);
  delayMicroseconds(PULSE_WIDTH_US);
  digitalWrite(Z_PUL_PIN, LOW);
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
// Move Z until selected switch is triggered
// =====================================================
bool moveZUntilSwitch(int switchPin, int direction, unsigned long &stepCount) {
  digitalWrite(Z_DIR_PIN, direction);
  delay(10);

  stepCount = 0;
  unsigned long startTime = millis();

  while (!isTriggered(switchPin)) {
    if (millis() - startTime > TRAVEL_TIMEOUT_MS) {
      stopAll();
      Serial.println("ERROR: Z-axis timeout. Motors stopped.");
      return false;
    }

    stepZ();
    stepCount++;

    delayMicroseconds(STEP_PERIOD_US - PULSE_WIDTH_US);
  }

  digitalWrite(Z_PUL_PIN, LOW);

  Serial.println("Z switch detected. Waiting 1 second...");
  delay(SWITCH_DELAY_MS);

  return true;
}

// =====================================================
// Home to coordinate (0,0,0)
// Optional count reports the pulses commanded while homing.
// =====================================================
bool homeXMin(unsigned long *homingPulses = nullptr) {
  unsigned long steps = 0;
  Serial.println("Homing X to GP2: X = 0");
  bool success = moveXUntilSwitch(X_MIN_SWITCH, X_DIRECTION_TO_MIN, steps);
  if (homingPulses) *homingPulses = steps;
  return success;
}

bool homeYMin(unsigned long *homingPulses = nullptr) {
  unsigned long steps = 0;
  Serial.println("Homing Y to GP7: Y = 0");
  bool success = moveYUntilSwitch(Y_MIN_SWITCH, Y_DIRECTION_TO_MIN, steps);
  if (homingPulses) *homingPulses = steps;
  return success;
}

bool homeZMin(unsigned long *homingPulses = nullptr) {
  unsigned long steps = 0;
  Serial.println("Homing Z to GP11: Z = 0");
  bool success = moveZUntilSwitch(Z_MIN_SWITCH, Z_DIRECTION_TO_MIN, steps);
  if (homingPulses) *homingPulses = steps;
  return success;
}

// =====================================================
// Serial reporting: each coordinate corresponds to ONE driver pulse.
// These are commanded pulses; there is no encoder position feedback.
// =====================================================
void printAxisCalibration(const char *axis, const char *motors,
                          unsigned long totalPulses, float totalMm) {
  Serial.print(axis);
  Serial.print(" (");
  Serial.print(motors);
  Serial.println("):");
  Serial.print("  Measured limit-to-limit: ");
  Serial.print(totalPulses);
  Serial.println(" step pulses");
  Serial.print("  Allowed coordinate range: 0 to ");
  Serial.print(totalPulses);
  Serial.println(" (whole numbers only)");
  Serial.println("  1 coordinate unit = 1 commanded step pulse");
  if (totalMm > 0.0f) {
    Serial.print("  Physical limit-to-limit distance: ");
    Serial.print(totalMm, 3);
    Serial.println(" mm");
    Serial.print("  Millimeters per step pulse (estimated): ");
    Serial.println(totalMm / (float)totalPulses, 6);
  } else {
    Serial.println("  Physical distance (mm): UNKNOWN; set *_FULL_TRAVEL_MM.");
  }
}

void printFullCalibrationReport() {
  Serial.println();
  Serial.println("========== AXIS LIMIT-TO-LIMIT STEP CALIBRATION ==========");
  printAxisCalibration("X", "Motors 1 and 2, each gets same pulses",
                       xMaxSteps, X_FULL_TRAVEL_MM);
  printAxisCalibration("Y", "Motor 3", yMaxSteps, Y_FULL_TRAVEL_MM);
  printAxisCalibration("Z", "Motor 4", zMaxSteps, Z_FULL_TRAVEL_MM);
  Serial.println("NOTE: Step counts are electrical driver pulses, not verified motion.");
  Serial.println("====================================================");
}

void printAxisMovement(const char *axis, const char *motors,
                       unsigned long targetCoordinate, unsigned long homingPulses,
                       unsigned long outwardPulses, unsigned long maxPulses,
                       float fullTravelMm) {
  Serial.print(axis);
  Serial.print(" (");
  Serial.print(motors);
  Serial.println("):");
  Serial.print("  Requested position: 0 -> ");
  Serial.print(targetCoordinate);
  Serial.println(" step pulses from home");
  Serial.print("  Axis maximum: ");
  Serial.print(maxPulses);
  Serial.println(" step pulses");
  Serial.print("  Pulses commanded during homing: ");
  Serial.println(homingPulses);
  Serial.print("  Pulses commanded from home to target: ");
  Serial.println(outwardPulses);
  Serial.print("  Total commanded pulses this command (home + outward): ");
  Serial.println(homingPulses + outwardPulses);
  if (fullTravelMm > 0.0f) {
    Serial.print("  Home to target estimated physical distance: ");
    Serial.print((float)outwardPulses / (float)maxPulses * fullTravelMm, 3);
    Serial.println(" mm");
    Serial.print("  Estimated total physical path incl. homing: ");
    Serial.print((float)(homingPulses + outwardPulses) / (float)maxPulses * fullTravelMm, 3);
    Serial.println(" mm");
  } else {
    Serial.println("  Physical distance (mm): UNKNOWN; enter limit-to-limit mm.");
  }
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

  if (xMaxSteps == 0) {
    Serial.println("ERROR: X calibrated to 0 steps. Check limit switches.");
    return false;
  }

  if (!homeXMin()) return false;

  if (!homeYMin()) return false;

  Serial.println("Measuring Y from GP7 to GP8...");
  if (!moveYUntilSwitch(Y_MAX_SWITCH, Y_DIRECTION_TO_MAX, yMaxSteps)) {
    return false;
  }

  Serial.print("Y maximum steps: ");
  Serial.println(yMaxSteps);

  if (yMaxSteps == 0) {
    Serial.println("ERROR: Y calibrated to 0 steps. Check limit switches.");
    return false;
  }

  if (!homeYMin()) return false;

  if (!homeZMin()) return false;

  Serial.println("Measuring Z from GP11 to GP12...");
  if (!moveZUntilSwitch(Z_MAX_SWITCH, Z_DIRECTION_TO_MAX, zMaxSteps)) {
    return false;
  }

  Serial.print("Z maximum steps: ");
  Serial.println(zMaxSteps);

  if (zMaxSteps == 0) {
    Serial.println("ERROR: Z calibrated to 0 steps. Check limit switches.");
    return false;
  }

  if (!homeZMin()) return false;

  return true;
}

// =====================================================
// X/Y command: two numbers, for example 1,2
// X motors (1 and 2) and Y motor (3) move concurrently.
// Motor 4 receives no steps during an X/Y command.
// =====================================================
bool moveXYToCoordinate(unsigned long xCoordinate, unsigned long yCoordinate,
                        unsigned long xHomingPulses, unsigned long yHomingPulses) {
  // One coordinate unit IS one pulse; no conversion or rounding necessary.
  const unsigned long xTargetSteps = xCoordinate;
  const unsigned long yTargetSteps = yCoordinate;

  if (xTargetSteps > xMaxSteps || yTargetSteps > yMaxSteps) {
    Serial.println("ERROR: XY coordinate is outside measured maximum travel.");
    return false;
  }

  Serial.print("Moving XY to: (");
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

  // Preserves the original X/Y coordinate movement timing.
  while (xStepsMoved < xTargetSteps || yStepsMoved < yTargetSteps) {
    if (millis() - startTime > TRAVEL_TIMEOUT_MS) {
      stopAll();
      Serial.println("ERROR: XY coordinate movement timeout.");
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

  Serial.print("Reached XY: (");
  Serial.print(xCoordinate);
  Serial.print(", ");
  Serial.print(yCoordinate);
  Serial.println(")");
  Serial.println("========== XY MOVEMENT REPORT ==========");
  printAxisMovement("X", "Motor 1 AND Motor 2, each", xCoordinate,
                    xHomingPulses, xStepsMoved, xMaxSteps, X_FULL_TRAVEL_MM);
  printAxisMovement("Y", "Motor 3", yCoordinate,
                    yHomingPulses, yStepsMoved, yMaxSteps, Y_FULL_TRAVEL_MM);
  Serial.println("Z (Motor 4): not moved by this command.");
  Serial.println("========================================");
  return true;
}

// =====================================================
// Z command: one number, for example 5
// Only Motor 4 receives steps during a Z command.
// =====================================================
bool moveZToCoordinate(unsigned long zCoordinate, unsigned long zHomingPulses) {
  // One coordinate unit IS one pulse; no conversion or rounding necessary.
  const unsigned long zTargetSteps = zCoordinate;

  if (zTargetSteps > zMaxSteps) {
    Serial.println("ERROR: Z coordinate is outside maximum travel.");
    return false;
  }

  Serial.print("Moving Z to: ");
  Serial.println(zCoordinate);

  digitalWrite(Z_DIR_PIN, Z_DIRECTION_TO_MAX);
  delay(10);

  unsigned long zStepsMoved = 0;
  unsigned long zLastStep = micros();
  unsigned long startTime = millis();

  while (zStepsMoved < zTargetSteps) {
    if (millis() - startTime > TRAVEL_TIMEOUT_MS) {
      stopAll();
      Serial.println("ERROR: Z coordinate movement timeout.");
      return false;
    }

    unsigned long now = micros();

    if (now - zLastStep >= STEP_PERIOD_US) {
      if (isTriggered(Z_MAX_SWITCH)) {
        stopAll();
        Serial.println("ERROR: Z reached GP12 before target.");
        return false;
      }

      stepZ();
      zStepsMoved++;
      zLastStep = now;
    }
  }

  stopAll();
  Serial.print("Reached Z: ");
  Serial.println(zCoordinate);
  Serial.println("========== Z MOVEMENT REPORT ==========");
  printAxisMovement("Z", "Motor 4", zCoordinate,
                    zHomingPulses, zStepsMoved, zMaxSteps, Z_FULL_TRAVEL_MM);
  Serial.println("X (Motors 1 and 2) and Y (Motor 3): not moved by this command.");
  Serial.println("=======================================");
  return true;
}

// Coordinates must be nonnegative whole-number pulse counts.
// A value like 101.5 is invalid: physical drivers receive whole pulses.
// Reject signs, decimals, letters and integer overflow.
bool parseStepCoordinate(const String &text, unsigned long &result) {
  if (text.length() == 0) return false;
  for (unsigned int i = 0; i < text.length(); ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
  }

  errno = 0;
  char *end = nullptr;
  unsigned long parsed = strtoul(text.c_str(), &end, 10);
  if (errno == ERANGE || *end != '\0') return false;
  result = parsed;
  return true;
}

void requestCoordinate() {
  Serial.println();
  Serial.println("Enter X,Y in STEP PULSES (example: 4,5) OR Z only (example: 5).");
  Serial.print("Valid X: 0.."); Serial.println(xMaxSteps);
  Serial.print("Valid Y: 0.."); Serial.println(yMaxSteps);
  Serial.print("Valid Z: 0.."); Serial.println(zMaxSteps);
  Serial.println("Whole nonnegative numbers only. Out-of-range inputs are rejected.");
  Serial.println("1 coordinate unit = 1 pulse; X/Y commands leave Z still and vice versa.");
  Serial.println("Enter INFO to reprint calibrated maximum steps for every axis.");
  Serial.println("Each command homes its axis/axes BEFORE moving to the new position.");
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

  pinMode(Z_DIR_PIN, OUTPUT);
  pinMode(Z_PUL_PIN, OUTPUT);

  // External 10 kΩ pull-up resistors to 3.3 V required.
  pinMode(X_MIN_SWITCH, INPUT);
  pinMode(X_MAX_SWITCH, INPUT);
  pinMode(Y_MIN_SWITCH, INPUT);
  pinMode(Y_MAX_SWITCH, INPUT);
  pinMode(Z_MIN_SWITCH, INPUT);
  pinMode(Z_MAX_SWITCH, INPUT);

  stopAll();

  Serial.println("Waiting 1 second before starting...");
  delay(SWITCH_DELAY_MS);

  Serial.println("Starting calibration...");

  if (!calibrateAxes()) {
    Serial.println("Calibration failed. Motors stopped.");
    while (true) {}
  }

  Serial.println("Calibration complete. Current position is (0,0,0).");
  printFullCalibrationReport();
  requestCoordinate();
}

// =====================================================
// Serial commands:
//   x,y  => home X, home Y, then move X/Y together; leave Z alone.
//   z    => home Z, then move Z; leave X/Y alone.
// Each target is an absolute INTEGER STEP COUNT relative to its home switch.
// =====================================================
void loop() {
  while (Serial.available() > 0) {
    char character = Serial.read();

    // Add characters until Return/Enter is received.
    if (character != '\r' && character != '\n') {
      // Avoid unbounded allocation if accidentally sent a huge serial string.
      if (inputBuffer.length() >= 63) {
        inputBuffer = "";
        Serial.println("\nERROR: Input too long. Try again.");
        requestCoordinate();
        // Discard remainder of the current line.
        while (Serial.available() > 0) {
          char c = Serial.read();
          if (c == '\r' || c == '\n') break;
        }
        continue;
      }
      inputBuffer += character;
      Serial.print(character);
      continue;
    }

    // Ignore second character in CR+LF and empty lines.
    if (inputBuffer.length() == 0) continue;

    Serial.println();
    String input = inputBuffer;
    inputBuffer = "";
    input.trim();
    input.replace("(", "");
    input.replace(")", "");
    input.trim();

    if (input == "INFO" || input == "info" || input == "?") {
      printFullCalibrationReport();
      requestCoordinate();
      continue;
    }

    int comma = input.indexOf(',');
    if (comma != -1) {
      // ---------------- X/Y command: exactly one comma ----------------
      if (input.indexOf(',', comma + 1) != -1) {
        Serial.println("Invalid XY input. Example: 1,2");
        requestCoordinate();
        continue;
      }

      String xText = input.substring(0, comma);
      String yText = input.substring(comma + 1);
      xText.trim();
      yText.trim();
      unsigned long xInput = 0;
      unsigned long yInput = 0;

      if (!parseStepCoordinate(xText, xInput) || !parseStepCoordinate(yText, yInput)) {
        Serial.println("Invalid X,Y. Enter whole nonnegative step counts, e.g. 4,5.");
        requestCoordinate();
        continue;
      }

      if (xInput > xMaxSteps || yInput > yMaxSteps) {
        Serial.print("Invalid X,Y range. X max = "); Serial.print(xMaxSteps);
        Serial.print(", Y max = "); Serial.println(yMaxSteps);
        requestCoordinate();
        continue;
      }

      Serial.println("Homing X, then Y (Z stays where it is)...");
      unsigned long xHomingPulses = 0;
      unsigned long yHomingPulses = 0;
      if (!homeXMin(&xHomingPulses) || !homeYMin(&yHomingPulses)) {
        stopAll();
        Serial.println("XY homing failed. Motors stopped.");
        while (true) {}
      }
      if (!moveXYToCoordinate(xInput, yInput, xHomingPulses, yHomingPulses)) {
        stopAll();
        Serial.println("XY movement failed. Motors stopped.");
        while (true) {}
      }
    } else {
      // ---------------- Z command: a single number ----------------
      unsigned long zInput = 0;
      if (!parseStepCoordinate(input, zInput)) {
        Serial.println("Invalid Z. Enter a whole nonnegative step count, e.g. 5.");
        requestCoordinate();
        continue;
      }

      if (zInput > zMaxSteps) {
        Serial.print("Invalid Z range. Z max = "); Serial.println(zMaxSteps);
        requestCoordinate();
        continue;
      }

      Serial.println("Homing Z only (X/Y stay where they are)...");
      unsigned long zHomingPulses = 0;
      if (!homeZMin(&zHomingPulses)) {
        stopAll();
        Serial.println("Z homing failed. Motors stopped.");
        while (true) {}
      }
      if (!moveZToCoordinate(zInput, zHomingPulses)) {
        stopAll();
        Serial.println("Z movement failed. Motors stopped.");
        while (true) {}
      }
    }

    requestCoordinate();
  }
}
