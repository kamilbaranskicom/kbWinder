#include <Arduino.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

// --- PINS ---
#define W_STEP 17
#define W_DIR  16
#define T_STEP 15
#define T_DIR  14
#define EN 12
#define LIMIT_PIN 4 

SoftwareSerial nextionSerial(2, 3);

// --- STRUCTURES ---

struct MachineConfig {
  float screwPitch;    // mm per rev
  int stepsPerRevW;    // e.g. 1600
  int stepsPerRevT;    // e.g. 1600
  int maxRPM_W;
  int maxRPM_T;
  byte directions;     // Bit 0: Winder, Bit 1: Traverse
};

struct WindingPreset {
  char name[16];
  float wireDia;
  float coilWidth;
  long totalTurns;
  long startOffset;    // steps from home
  int targetRPM;
  int rampRPM;         // RPM increase per turn
};

// --- GLOBAL STATE ---
MachineConfig cfg;
WindingPreset prst;
bool isWinding = false;
bool isPaused = false;
bool isHomed = false;

long absPos = 0;             // Traverse steps from 0
long currentStepsW = 0;      // Winder progress in steps
long currentLayerSteps = 0;  // Steps in current layer
long stepsPerLayer = 0;
int layerDir = 1;
float traverseAccumulator = 0;

int currentRPM = 0;
long currentDelay = 800;

// --- UTILS ---

long rpmToDelay(int rpm) {
  if (rpm < 10) rpm = 10;
  return 30000000L / ((long)rpm * cfg.stepsPerRevW);
}

void loadMachineConfig() {
  EEPROM.get(0, cfg);
  if (cfg.stepsPerRevW == -1 || cfg.stepsPerRevW == 0) {
    cfg = {2.0, 1600, 1600, 800, 400, 0b00};
    EEPROM.put(0, cfg);
  }
}

void savePreset(int idx, WindingPreset p) {
  int addr = sizeof(MachineConfig) + (idx * sizeof(WindingPreset));
  EEPROM.put(addr, p);
}

void loadPreset(int idx) {
  int addr = sizeof(MachineConfig) + (idx * sizeof(WindingPreset));
  EEPROM.get(addr, prst);
}

// --- CORE FUNCTIONS ---

void stopMachine() {
  isWinding = false;
  isPaused = false;
  digitalWrite(EN, HIGH);
  Serial.println(F("MSG: Full Stop"));
}

void homeTraverse() {
  if (isWinding) return;
  Serial.println(F("MSG: Homing..."));
  digitalWrite(EN, LOW);
  digitalWrite(T_DIR, (cfg.directions & 0x02) ? HIGH : LOW); // Move towards switch
  
  while(digitalRead(LIMIT_PIN) == HIGH) {
    digitalWrite(T_STEP, HIGH); delayMicroseconds(400);
    digitalWrite(T_STEP, LOW);  delayMicroseconds(400);
  }
  
  absPos = 0;
  isHomed = true;
  
  // Back off
  digitalWrite(T_DIR, (cfg.directions & 0x02) ? LOW : HIGH);
  for(int i=0; i<800; i++) {
    digitalWrite(T_STEP, HIGH); delayMicroseconds(400);
    digitalWrite(T_STEP, LOW);  delayMicroseconds(400);
    absPos += (cfg.directions & 0x02) ? -1 : 1;
  }
  Serial.println(F("MSG: Home 0 established"));
}

void printStatus() {
  long turnsDone = currentStepsW / cfg.stepsPerRevW;
  long turnsLeft = prst.totalTurns - turnsDone;
  int currentLayer = (currentStepsW / stepsPerLayer) + 1;
  int etaMin = (currentRPM > 0) ? (turnsLeft / currentRPM) : 0;

  Serial.print(F("STAT|T:")); Serial.print(turnsDone);
  Serial.print(F("/")); Serial.print(prst.totalTurns);
  Serial.print(F("|L:")); Serial.print(currentLayer);
  Serial.print(F("|RPM:")); Serial.print(currentRPM);
  Serial.print(F("|ETA:")); Serial.print(etaMin); Serial.println(F("m"));
}

void moveManual(char motor, long steps, int rpm) {
  digitalWrite(EN, LOW);
  long dly = rpmToDelay(rpm);
  int sPin = (motor == 'W') ? W_STEP : T_STEP;
  int dPin = (motor == 'W') ? W_DIR : T_DIR;
  
  bool dirFlag = (steps > 0);
  if (motor == 'W' && (cfg.directions & 0x01)) dirFlag = !dirFlag;
  if (motor == 'T' && (cfg.directions & 0x02)) dirFlag = !dirFlag;
  
  digitalWrite(dPin, dirFlag ? HIGH : LOW);
  
  for(long i=0; i<abs(steps); i++) {
    digitalWrite(sPin, HIGH); delayMicroseconds(dly);
    digitalWrite(sPin, LOW);  delayMicroseconds(dly);
    
    // Update progress if we are unwinding while paused
    if (isPaused && motor == 'W') {
      if (steps > 0) currentStepsW++; else currentStepsW--;
    }
    if (motor == 'T') {
      if (steps > 0) absPos++; else absPos--;
    }
  }
  if (!isWinding && !isPaused) digitalWrite(EN, HIGH);
}

void startWinding() {
  if (prst.totalTurns <= 0) return;
  
  stepsPerLayer = (long)((prst.coilWidth / prst.wireDia) * cfg.stepsPerRevW);
  currentStepsW = 0;
  currentLayerSteps = 0;
  traverseAccumulator = 0;
  currentRPM = 50; // Start RPM
  currentDelay = rpmToDelay(currentRPM);
  layerDir = 1;
  
  isWinding = true;
  isPaused = false;
  digitalWrite(EN, LOW);
  Serial.println(F("MSG: Winding Start"));
}

// --- COMMAND INTERPRETER ---

void processSerial(String cmd) {
  cmd.trim();
  if (cmd == "STOP") { stopMachine(); return; }
  if (cmd == "PAUSE") { isPaused = true; Serial.println(F("MSG: Paused")); return; }
  if (cmd == "RESUME") { isPaused = false; Serial.println(F("MSG: Resuming")); return; }
  if (cmd == "H") { homeTraverse(); return; }
  if (cmd == "I") { printStatus(); return; }
  
  // Quick Winding: "START [dia] [width] [turns] [rpm] [ramp]"
  if (cmd.startsWith("START ")) {
     // Parsing logic...
  }

  // Manual: "T 800 200" (steps, rpm)
  if (cmd.startsWith("T ") || cmd.startsWith("W ")) {
    char m = cmd[0];
    int s1 = cmd.indexOf(' ');
    int s2 = cmd.lastIndexOf(' ');
    long stp = cmd.substring(s1+1, s2).toInt();
    int rpm = cmd.substring(s2+1).toInt();
    moveManual(m, stp, rpm);
  }
}

void setup() {
  pinMode(EN, OUTPUT);
  pinMode(W_STEP, OUTPUT); pinMode(W_DIR, OUTPUT);
  pinMode(T_STEP, OUTPUT); pinMode(T_DIR, OUTPUT);
  pinMode(LIMIT_PIN, INPUT_PULLUP);
  digitalWrite(EN, HIGH);
  
  Serial.begin(57600);
  nextionSerial.begin(9600);
  loadMachineConfig();
  
  Serial.println(F("--- WINDER OS V3.0 ---"));
}

void loop() {
  if (Serial.available()) processSerial(Serial.readStringUntil('\n'));
  
  if (isWinding && !isPaused) {
    if (currentStepsW < (prst.totalTurns * cfg.stepsPerRevW)) {
      
      // Step Winder
      digitalWrite(W_DIR, (cfg.directions & 0x01) ? LOW : HIGH);
      digitalWrite(W_STEP, HIGH);
      
      // Sync Traverse
      traverseAccumulator += (prst.wireDia * (cfg.stepsPerRevT / cfg.screwPitch));
      if (traverseAccumulator >= (float)cfg.stepsPerRevW) {
        digitalWrite(T_DIR, (layerDir == 1) ? HIGH : LOW); // Simplified logic
        digitalWrite(T_STEP, HIGH);
        traverseAccumulator -= (float)cfg.stepsPerRevW;
        absPos += layerDir;
      }
      
      delayMicroseconds(currentDelay);
      digitalWrite(W_STEP, LOW);
      digitalWrite(T_STEP, LOW);
      delayMicroseconds(currentDelay);
      
      currentStepsW++;
      currentLayerSteps++;
      
      // Acceleration & Feedback
      if (currentStepsW % cfg.stepsPerRevW == 0) {
        if (currentRPM < prst.targetRPM) {
          currentRPM += prst.rampRPM;
          currentDelay = rpmToDelay(currentRPM);
        }
        printStatus();
      }
      
      // Layer Flip
      if (currentLayerSteps >= stepsPerLayer) {
        layerDir *= -1;
        currentLayerSteps = 0;
      }
      
    } else {
      stopMachine();
    }
  }
}