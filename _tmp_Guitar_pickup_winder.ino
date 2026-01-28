/* * GUITAR PICKUP WINDER V2.3
 * Fixed: Non-blocking start, Manual Winder control, Floating point units
 */

#include <Arduino.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>


// --- PIN CONFIGURATION ---
#define W_STEP 17
#define W_DIR 16
#define T_STEP 15
#define T_DIR 14
#define EN 12
#define LIMIT_PIN 4

SoftwareSerial nextionSerial(2, 3);

// --- MECHANICAL SETTINGS ---
const long STEPS_PER_REV = 1600;
const float STEPS_PER_MM = 800.0;

// --- SPEED & ACCELERATION ---
const long ULTRA_START_DELAY = 800;
const long START_DELAY = 400;
const long MIN_DELAY = 70;

const long PHASE1_STEPS = 3200;
const long PHASE2_STEPS = 12800;

// --- POSITIONING ---
const int EEPROM_ADDR_OFFSET = 10;
long absPos = 0;
long startOffset = 0;
bool isHomed = false;

// --- WINDING STATE ---
float wireDia = 0.0;
float coilWidth = 0.0;
long targetTurns = 0;

long currentStepsW = 0;
long totalTargetSteps = 0;
long currentLayerSteps = 0;
long stepsPerLayer = 0;
float traverseAccumulator = 0.0;

long currentDelay = ULTRA_START_DELAY;
int layerDir = 1;
bool isWinding = false;

String hmiBuffer = "";
int hmiEndCount = 0;

// --- FUNCTION PROTOTYPES ---
void stopMachine();
void startMachine();
void homeTraverse();
void setStartPoint();
void goToStart();
void moveTraverseAbs(long targetAbs, int sDelay);
void performWindingStep();
void updateSpeed();
void updateDisplays();
void handleInputs();
void processCommand(String cmd);
void printHelp();

void setup() {
  pinMode(EN, OUTPUT);
  pinMode(W_STEP, OUTPUT);
  pinMode(W_DIR, OUTPUT);
  pinMode(T_STEP, OUTPUT);
  pinMode(T_DIR, OUTPUT);
  pinMode(LIMIT_PIN, INPUT_PULLUP);
  digitalWrite(EN, HIGH);

  Serial.begin(57600);
  nextionSerial.begin(9600);

  EEPROM.get(EEPROM_ADDR_OFFSET, startOffset);

  Serial.println(F("--- SYSTEM READY V2.3 ---"));
  printHelp();
}

void loop() {
  handleInputs();

  if (isWinding) {
    if (currentStepsW < totalTargetSteps) {
      performWindingStep();
      updateSpeed();
      if (currentStepsW % STEPS_PER_REV == 0) {
        updateDisplays();
      }
    } else {
      stopMachine();
    }
  }
}

// --- CORE LOGIC ---

void performWindingStep() {
  digitalWrite(W_STEP, HIGH);

  traverseAccumulator += (wireDia * STEPS_PER_MM);
  if (traverseAccumulator >= (float)STEPS_PER_REV) {
    digitalWrite(T_STEP, HIGH);
    traverseAccumulator -= (float)STEPS_PER_REV;
    absPos += layerDir;
  }

  delayMicroseconds(currentDelay);
  digitalWrite(W_STEP, LOW);
  digitalWrite(T_STEP, LOW);
  delayMicroseconds(currentDelay);

  currentStepsW++;
  currentLayerSteps++;

  if (currentLayerSteps >= stepsPerLayer) {
    layerDir *= -1;
    digitalWrite(T_DIR, (layerDir == 1) ? HIGH : LOW);
    currentLayerSteps = 0;
    Serial.println(F("L_FLIP"));
  }
}

void updateSpeed() {
  if (currentStepsW < PHASE1_STEPS) {
    currentDelay =
        map(currentStepsW, 0, PHASE1_STEPS, ULTRA_START_DELAY, START_DELAY);
  } else if (currentStepsW < (PHASE1_STEPS + PHASE2_STEPS)) {
    currentDelay = map(currentStepsW, PHASE1_STEPS, PHASE1_STEPS + PHASE2_STEPS,
                       START_DELAY, MIN_DELAY);
  } else {
    currentDelay = MIN_DELAY;
  }
}

// --- POSITIONING ---

void moveTraverseAbs(long targetAbs, int sDelay = 250) {
  if (isWinding)
    return;
  digitalWrite(EN, LOW);
  long stepsToMove = targetAbs - absPos;
  digitalWrite(T_DIR, (stepsToMove >= 0) ? HIGH : LOW);

  long remaining = abs(stepsToMove);
  for (long i = 0; i < remaining; i++) {
    digitalWrite(T_STEP, HIGH);
    delayMicroseconds(sDelay);
    digitalWrite(T_STEP, LOW);
    delayMicroseconds(sDelay);
    absPos += (stepsToMove >= 0) ? 1 : -1;
  }
  digitalWrite(EN, HIGH);
}

void homeTraverse() {
  Serial.println(F("Searching limit switch... (Press Enter to Cancel)"));
  digitalWrite(EN, LOW);
  digitalWrite(T_DIR, LOW);

  while (digitalRead(LIMIT_PIN) == HIGH) {
    digitalWrite(T_STEP, HIGH);
    delayMicroseconds(400);
    digitalWrite(T_STEP, LOW);
    delayMicroseconds(400);
    if (Serial.available()) {
      Serial.read();
      break;
    } // Cancel if key pressed
  }

  absPos = 0;
  isHomed = true;
  moveTraverseAbs(800);
  Serial.println(F("Origin set to 0."));
}

void goToStart() {
  Serial.println(F("Moving to start point..."));
  moveTraverseAbs(startOffset);
}

// --- COMMANDS ---

void processCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();

  if (cmd.length() == 0 || cmd == "STOP") {
    stopMachine();
    return;
  }
  if (cmd == "?" || cmd == "HELP") {
    printHelp();
    return;
  }
  if (cmd == "SET_ZERO") {
    absPos = 0;
    isHomed = true;
    Serial.println(F("Pos set to 0"));
    return;
  }
  if (cmd == "HOME" || cmd == "H") {
    homeTraverse();
    return;
  }
  if (cmd == "SET_START") {
    startOffset = absPos;
    EEPROM.put(EEPROM_ADDR_OFFSET, startOffset);
    Serial.println(F("Start point saved"));
    return;
  }
  if (cmd == "GO_START") {
    goToStart();
    return;
  }
  if (cmd == "START") {
    startMachine();
    return;
  }

  // Manual Moves
  if (cmd.startsWith("T") || cmd.startsWith("W")) {
    char motor = cmd[0];
    int firstSpace = cmd.indexOf(' ');
    int lastSpace = cmd.lastIndexOf(' ');
    long steps =
        cmd.substring(firstSpace + 1,
                      lastSpace == firstSpace ? cmd.length() : lastSpace)
            .toInt();
    int sDelay =
        (firstSpace == lastSpace) ? 400 : cmd.substring(lastSpace + 1).toInt();

    digitalWrite(EN, LOW);
    if (motor == 'T')
      moveTraverseAbs(absPos + steps, sDelay);
    if (motor == 'W') {
      digitalWrite(W_DIR, steps > 0 ? HIGH : LOW);
      for (long i = 0; i < abs(steps); i++) {
        digitalWrite(W_STEP, HIGH);
        delayMicroseconds(sDelay);
        digitalWrite(W_STEP, LOW);
        delayMicroseconds(sDelay);
      }
    }
    digitalWrite(EN, HIGH);
    return;
  }

  // Set Params: "0.1 12.5 1000"
  int s1 = cmd.indexOf(' ');
  int s2 = cmd.lastIndexOf(' ');
  if (s1 != -1 && s2 != -1) {
    wireDia = cmd.substring(0, s1).toFloat();
    coilWidth = cmd.substring(s1 + 1, s2).toFloat();
    targetTurns = cmd.substring(s2 + 1).toInt();
    Serial.println(F("Params loaded."));
    startMachine();
  }
}

void startMachine() {
  if (wireDia > 0 && coilWidth > 0 && targetTurns > 0) {
    // If not homed, we assume current position is 0
    if (!isHomed) {
      absPos = 0;
      isHomed = true;
    }

    goToStart();

    totalTargetSteps = targetTurns * STEPS_PER_REV;
    stepsPerLayer = (long)((coilWidth / wireDia) * STEPS_PER_REV);

    currentStepsW = 0;
    currentLayerSteps = 0;
    traverseAccumulator = 0;
    currentDelay = ULTRA_START_DELAY;
    layerDir = 1;
    isWinding = true;

    digitalWrite(EN, LOW);
    digitalWrite(W_DIR, HIGH);
    digitalWrite(T_DIR, HIGH);
    Serial.println(F("WINDING..."));
  }
}

void stopMachine() {
  isWinding = false;
  digitalWrite(EN, HIGH);
  Serial.println(F("STOPPED"));
}

void handleInputs() {
  if (Serial.available())
    processCommand(Serial.readStringUntil('\n'));
  while (nextionSerial.available()) {
    byte b = nextionSerial.read();
    if ((b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || b == ' ' ||
        b == '.' || b == '-')
      hmiBuffer += (char)b;
    else if (b == 0xFF)
      hmiEndCount++;
    if (hmiEndCount >= 3) {
      processCommand(hmiBuffer);
      hmiBuffer = "";
      hmiEndCount = 0;
    }
  }
}

void updateDisplays() {
  long t = currentStepsW / STEPS_PER_REV;
  nextionSerial.print("n2.val=");
  nextionSerial.print(t);
  nextionSerial.write(0xff);
  nextionSerial.write(0xff);
  nextionSerial.write(0xff);
  Serial.print(F("Turn: "));
  Serial.println(t);
}

void printHelp() {
  Serial.println(F("1. [wire] [width] [turns] -> Setup & Start"));
  Serial.println(F("2. W [steps] [delay]     -> Move Winder"));
  Serial.println(F("3. T [steps] [delay]     -> Move Traverse"));
  Serial.println(F("4. SET_ZERO              -> Set current pos as 0"));
  Serial.println(
      F("5. SET_START             -> Save current T as bobbin edge"));
  Serial.println(F("6. GO_START              -> Move to bobbin edge"));
  Serial.println(F("7. STOP                  -> Stop all"));
}