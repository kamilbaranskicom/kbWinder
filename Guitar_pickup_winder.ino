/* * GUITAR PICKUP WINDER V2.2
 * Language: English comments (as requested)
 * Units: Millimeters (float)
 */

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

// --- PIN CONFIGURATION ---
#define W_STEP 17
#define W_DIR  16
#define T_STEP 15
#define T_DIR  14
#define EN 12
#define LIMIT_PIN 4 // Connect limit switch between D4 and GND

SoftwareSerial nextionSerial(2, 3); // RX, TX for HMI

// --- MECHANICAL SETTINGS ---
const long STEPS_PER_REV = 1600;
const float STEPS_PER_MM = 800.0; // 1600 steps / 2.0mm pitch

// --- SPEED & ACCELERATION ---
const long ULTRA_START_DELAY = 800; // Slowest start
const long START_DELAY = 400;       // Intermediate ramp point
const long MIN_DELAY = 70;         // Max speed (~270 RPM)

const long PHASE1_STEPS = 3200;     // First 2 turns (ramp 800->400)
const long PHASE2_STEPS = 12800;    // Next 8 turns (ramp 400->70)

// --- POSITIONING & EEPROM ---
const int EEPROM_ADDR_OFFSET = 10;
long absPos = 0;          // Absolute position in steps from Home
long startOffset = 0;     // Saved bobbin edge position
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
int layerDir = 1; // 1 = Away from home, -1 = Towards home
bool isWinding = false;

// --- COMMUNICATION ---
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
void printInfo();

void setup() {
  pinMode(EN, OUTPUT);
  pinMode(W_STEP, OUTPUT); pinMode(W_DIR, OUTPUT);
  pinMode(T_STEP, OUTPUT); pinMode(T_DIR, OUTPUT);
  pinMode(LIMIT_PIN, INPUT_PULLUP);
  digitalWrite(EN, HIGH);
  
  Serial.begin(57600); 
  nextionSerial.begin(9600); 

  // Load saved offset from memory
  EEPROM.get(EEPROM_ADDR_OFFSET, startOffset);

  Serial.println(F("--- SYSTEM READY V2.2 ---"));
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

// --- CORE WINDING LOGIC ---

void performWindingStep() {
  digitalWrite(W_STEP, HIGH);
  
  // Sync Traverse using float precision for millimeters
  traverseAccumulator += (wireDia * STEPS_PER_MM);
  if (traverseAccumulator >= (float)STEPS_PER_REV) {
    digitalWrite(T_STEP, HIGH);
    traverseAccumulator -= (float)STEPS_PER_REV;
    absPos += layerDir; // Update absolute coordinate
  }
  
  delayMicroseconds(currentDelay);
  digitalWrite(W_STEP, LOW);
  digitalWrite(T_STEP, LOW);
  delayMicroseconds(currentDelay);

  currentStepsW++;
  currentLayerSteps++;

  // Layer Change Logic
  if (currentLayerSteps >= stepsPerLayer) {
    layerDir *= -1;
    digitalWrite(T_DIR, (layerDir == 1) ? HIGH : LOW);
    currentLayerSteps = 0;
    Serial.println(F("EVENT: Layer Change"));
  }
}

void updateSpeed() {
  if (currentStepsW < PHASE1_STEPS) {
    currentDelay = map(currentStepsW, 0, PHASE1_STEPS, ULTRA_START_DELAY, START_DELAY);
  } else if (currentStepsW < (PHASE1_STEPS + PHASE2_STEPS)) {
    currentDelay = map(currentStepsW, PHASE1_STEPS, PHASE1_STEPS + PHASE2_STEPS, START_DELAY, MIN_DELAY);
  } else {
    currentDelay = MIN_DELAY;
  }
}

// --- POSITIONING FUNCTIONS ---

void moveTraverseAbs(long targetAbs, int sDelay = 250) {
  if (isWinding) return;
  digitalWrite(EN, LOW);
  long stepsToMove = targetAbs - absPos;
  digitalWrite(T_DIR, (stepsToMove >= 0) ? HIGH : LOW);
  
  long remaining = abs(stepsToMove);
  for (long i = 0; i < remaining; i++) {
    digitalWrite(T_STEP, HIGH); delayMicroseconds(sDelay);
    digitalWrite(T_STEP, LOW);  delayMicroseconds(sDelay);
    absPos += (stepsToMove >= 0) ? 1 : -1;
  }
  digitalWrite(EN, HIGH);
}

void homeTraverse() {
  Serial.println(F("HOMING: Searching limit switch..."));
  digitalWrite(EN, LOW);
  digitalWrite(T_DIR, LOW); 
  
  while(digitalRead(LIMIT_PIN) == HIGH) {
    digitalWrite(T_STEP, HIGH); delayMicroseconds(400);
    digitalWrite(T_STEP, LOW);  delayMicroseconds(400);
  }
  
  absPos = 0; 
  isHomed = true;
  moveTraverseAbs(800); // Move 1mm away from switch
  Serial.println(F("HOMING: Success. Origin at 0."));
}

void setStartPoint() {
  if (!isHomed) { Serial.println(F("ERR: Home first!")); return; }
  startOffset = absPos;
  EEPROM.put(EEPROM_ADDR_OFFSET, startOffset);
  Serial.print(F("Start point saved: ")); Serial.print(startOffset / STEPS_PER_MM); Serial.println(F(" mm"));
}

void goToStart() {
  if (!isHomed) homeTraverse();
  Serial.println(F("Moving to start point..."));
  moveTraverseAbs(startOffset);
}

// --- COMMAND HANDLING ---

void processCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();

  if (cmd.length() == 0 || cmd == "STOP") { stopMachine(); return; }
  if (cmd == "?" || cmd == "HELP") { printHelp(); return; }
  if (cmd == "I" || cmd == "STATUS") { printInfo(); return; }
  if (cmd == "H" || cmd == "HOME") { homeTraverse(); return; }
  if (cmd == "SET_START") { setStartPoint(); return; }
  if (cmd == "GO_START") { goToStart(); return; }
  if (cmd == "START") { startMachine(); return; }

  // Manual Move: "T 1600 200"
  if (cmd.startsWith("T") || cmd.startsWith("W")) {
    char motor = cmd[0];
    int firstSpace = cmd.indexOf(' ');
    int lastSpace = cmd.lastIndexOf(' ');
    long steps = cmd.substring(firstSpace + 1, lastSpace == firstSpace ? cmd.length() : lastSpace).toInt();
    int sDelay = (firstSpace == lastSpace) ? 400 : cmd.substring(lastSpace + 1).toInt();
    
    if (motor == 'T') moveTraverseAbs(absPos + steps, sDelay);
    // Winder manual move simplified:
    if (motor == 'W') {
        digitalWrite(EN, LOW); digitalWrite(W_DIR, steps > 0 ? HIGH : LOW);
        for(long i=0; i<abs(steps); i++) { digitalWrite(W_STEP, HIGH); delayMicroseconds(sDelay); digitalWrite(W_STEP, LOW); delayMicroseconds(sDelay); }
        digitalWrite(EN, HIGH);
    }
    return;
  }

  // Winding Config: "0.1 12.5 5000"
  int s1 = cmd.indexOf(' ');
  int s2 = cmd.lastIndexOf(' ');
  if (s1 != -1 && s2 != -1) {
    wireDia = cmd.substring(0, s1).toFloat();
    coilWidth = cmd.substring(s1 + 1, s2).toFloat();
    targetTurns = cmd.substring(s2 + 1).toInt();
    printInfo();
    startMachine();
  }
}

void startMachine() {
  if (wireDia > 0 && coilWidth > 0 && targetTurns > 0) {
    if (!isHomed) homeTraverse();
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
    Serial.println(F(">>> WINDING STARTED"));
  } else {
    Serial.println(F("ERR: Params not set!"));
  }
}

void stopMachine() {
  isWinding = false;
  digitalWrite(EN, HIGH);
  Serial.println(F(">>> STOPPED"));
}

void handleInputs() {
  if (Serial.available()) processCommand(Serial.readStringUntil('\n'));

  while (nextionSerial.available()) {
    byte b = nextionSerial.read();
    if ((b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || b == ' ' || b == '.' || b == '-') {
      hmiBuffer += (char)b;
    } else if (b == 0xFF) {
      hmiEndCount++;
    }
    if (hmiEndCount >= 3) {
      processCommand(hmiBuffer);
      hmiBuffer = ""; hmiEndCount = 0;
    }
  }
}

void updateDisplays() {
  long turns = currentStepsW / STEPS_PER_REV;
  nextionSerial.print("n2.val=");
  nextionSerial.print(turns);
  nextionSerial.write(0xff); nextionSerial.write(0xff); nextionSerial.write(0xff);
  Serial.print(F("Turn: ")); Serial.println(turns);
}

void printHelp() {
  Serial.println(F("\n--- COMMANDS ---"));
  Serial.println(F("1. [wire] [width] [turns] -> Setup & Start"));
  Serial.println(F("2. H                     -> Home Traverse"));
  Serial.println(F("3. SET_START             -> Save current T as start"));
  Serial.println(F("4. GO_START              -> Move to start point"));
  Serial.println(F("5. T [steps] [delay]     -> Move Traverse"));
  Serial.println(F("6. STOP                  -> Stop all"));
  Serial.println(F("7. I                     -> Show status"));
}

void printInfo() {
  Serial.println(F("\n--- STATUS ---"));
  Serial.print(F("Wire: ")); Serial.print(wireDia, 3); Serial.println(F(" mm"));
  Serial.print(F("Width: ")); Serial.print(coilWidth, 2); Serial.println(F(" mm"));
  Serial.print(F("Turns: ")); Serial.println(targetTurns);
  Serial.print(F("Pos: ")); Serial.print(absPos / STEPS_PER_MM); Serial.println(F(" mm from Home"));
}