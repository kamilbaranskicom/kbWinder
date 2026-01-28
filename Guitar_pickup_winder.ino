#include "Guitar_pickup_winder.h"
#include "presets.h"

SoftwareSerial nextionSerial(2, 3);

// --- GLOBAL STATE ---
MachineConfig cfg;
WindingPreset prst;

bool isWinding = false;
bool isPaused = false;
bool isHomed = false;

long absPos = 0;            // Traverse steps from 0
long currentStepsW = 0;     // Winder progress in steps
long currentLayerSteps = 0; // Steps in current layer
long stepsPerLayer = 0;
int layerDir = 1;
float traverseAccumulator = 0;

int startRPM = 50;

int currentRPM = 0;
long currentDelay = 800;

// --- UTILS ---

long rpmToDelay(int rpm) {
  if (rpm < 10)
    rpm = 10;
  return 30000000L / ((long)rpm * cfg.stepsPerRevW);
}

void loadMachineConfig() {
  EEPROM.get(0, cfg);
  if (cfg.stepsPerRevW == -1 || cfg.stepsPerRevW == 0) {
    cfg = {2.0, 1600, 1600, 800, 400, 0b00};
    EEPROM.put(0, cfg);
  }
}

// --- CORE FUNCTIONS ---

void stopMachine() {
  state = IDLE;
  digitalWrite(EN, HIGH); // Offline
  Serial.println(F("MSG: Stopped"));
}

void homeTraverse() {
  if (isWinding)
    return;
  Serial.println(F("MSG: Homing..."));
  digitalWrite(EN, LOW);
  digitalWrite(T_DIR,
               (cfg.directions & 0x02) ? HIGH : LOW); // Move towards switch

  while (digitalRead(LIMIT_PIN) == HIGH) {
    digitalWrite(T_STEP, HIGH);
    delayMicroseconds(400);
    digitalWrite(T_STEP, LOW);
    delayMicroseconds(400);
  }

  absPos = 0;
  isHomed = true;

  // Back off
  digitalWrite(T_DIR, (cfg.directions & 0x02) ? LOW : HIGH);
  for (int i = 0; i < 800; i++) {
    digitalWrite(T_STEP, HIGH);
    delayMicroseconds(400);
    digitalWrite(T_STEP, LOW);
    delayMicroseconds(400);
    absPos += (cfg.directions & 0x02) ? -1 : 1;
  }
  Serial.println(F("MSG: Home 0 established"));
}

void printStatus() {
  // Simplified status output
  Serial.print(F("STATUS STATE="));
  Serial.print(state);
  Serial.print(F(" TURNS="));
  Serial.print(active.totalTurns);
  Serial.println();

  /*
  // old status format
  long turnsDone = currentStepsW / cfg.stepsPerRevW;
  long turnsLeft = prst.totalTurns - turnsDone;
  int currentLayer = (currentStepsW / stepsPerLayer) + 1;
  int etaMin = (currentRPM > 0) ? (turnsLeft / currentRPM) : 0;

  Serial.print(F("STAT|T:"));
  Serial.print(turnsDone);
  Serial.print(F("/"));
  Serial.print(prst.totalTurns);
  Serial.print(F("|L:"));
  Serial.print(currentLayer);
  Serial.print(F("|RPM:"));
  Serial.print(currentRPM);
  Serial.print(F("|ETA:"));
  Serial.print(etaMin);
  Serial.println(F("m"));
*/
}

void moveManual(char motor, long steps, int rpm) {
  digitalWrite(EN, LOW);
  long dly = rpmToDelay(rpm);
  int sPin = (motor == 'W') ? W_STEP : T_STEP;
  int dPin = (motor == 'W') ? W_DIR : T_DIR;

  bool dirFlag = (steps > 0);
  if (motor == 'W' && (cfg.directions & 0x01))
    dirFlag = !dirFlag;
  if (motor == 'T' && (cfg.directions & 0x02))
    dirFlag = !dirFlag;

  digitalWrite(dPin, dirFlag ? HIGH : LOW);

  for (long i = 0; i < abs(steps); i++) {
    digitalWrite(sPin, HIGH);
    delayMicroseconds(dly);
    digitalWrite(sPin, LOW);
    delayMicroseconds(dly);

    // Update progress if we are unwinding while paused
    if (isPaused && motor == 'W') {
      if (steps > 0)
        currentStepsW++;
      else
        currentStepsW--;
    }
    if (motor == 'T') {
      if (steps > 0)
        absPos++;
      else
        absPos--;
    }
  }
  if (!isWinding && !isPaused)
    digitalWrite(EN, HIGH);
}

void startWinding() {
  if (prst.totalTurns <= 0)
    return;

  stepsPerLayer = (long)((prst.coilWidth / prst.wireDia) * cfg.stepsPerRevW);
  currentStepsW = 0;
  currentLayerSteps = 0;
  traverseAccumulator = 0;
  currentRPM = startRPM;
  currentDelay = rpmToDelay(currentRPM);
  layerDir = 1;

  isWinding = true;
  isPaused = false;
  digitalWrite(EN, LOW);
  Serial.println(F("MSG: Winding Start"));
}

void resumeWinding() {
  if (state == PAUSED) {
    currentRPM = 50; // Restart from slow speed
    currentDelay = rpmToDelay(currentRPM);
    state = RUNNING;
    digitalWrite(EN, LOW); // Motors back online
    Serial.println(F("MSG: Resuming with ramp-up"));
  }
}

// --- COMMAND INTERPRETER ---

void processCommand(String cmd) {
  cmd.trim();

  if (cmd == "STOP") {
    stopMachine();
  } else if (cmd == "PAUSE") {
    state = PAUSED;
    digitalWrite(EN, HIGH);
    Serial.println(F("MSG: Paused (Offline)"));
  } else if (cmd == "RESUME") {
    resumeWinding();
  } else if (cmd.startsWith("SET ")) {
    handleSet(cmd);
  } else if (cmd.startsWith("GET")) {
    handleGet(cmd);
  } else if (cmd == "EXPORT") {
    exportCSV();
  } else if (cmd == "STATUS") {
    printStatus();
  } // (... handle more commands...)
  else if (cmd == "H") {
    homeTraverse();
  } else if (cmd.startsWith("START ")) {
    // Quick Winding: "START [dia] [width] [turns] [rpm] [ramp]"
    // Parsing logic...
  } else if (cmd.startsWith("T ") || cmd.startsWith("W ")) {
    // Manual: "T 800 200" (steps, rpm)
    char m = cmd[0];
    int s1 = cmd.indexOf(' ');
    int s2 = cmd.lastIndexOf(' ');
    long stp = cmd.substring(s1 + 1, s2).toInt();
    int rpm = cmd.substring(s2 + 1).toInt();
    moveManual(m, stp, rpm);
  }
}

void printHelp() {
  Serial.println(F("Movement: W, T, GOTO T, SEEK ZERO, HOME\n"
                   "Control: START, STOP, PAUSE, RESUME\n"
                   "Presets: SAVE, LOAD, DELETE, EXPORT, IMPORT\n"
                   "Settings: SET ..., GET ...\n"
                   "Info: STATUS, HELP, LONGHELP"));
}

void printLongHelp() {
  Serial.println(F(
    "START (<preset>|<wire-diameter> <coil-length> <turns>): starts winding the coil\n"
    "STOP: stop winding\n"
    "PAUSE: pause winding and put motors in offline; doesn't reset position\n"
    "RESUME: resume winding after PAUSE\n"
    "SEEK ZERO: finds ZERO position (by moving Traverse backward until the limit switch is found or STOP command is sent)\n"
    "HOME: goes to HOME position\n"
    "W <distance> [speed]: move Winder to relative position\n"
    "T <distance> [speed]: move Traverse to relative position\n"
    "GOTO T <position> [speed]: move Traverse to absolutee position\n"
    "SAVE <name> [<wire-diameter> <coil-length> <turns>]: saves preset <name>\n"
    "LOAD <name>: loads preset <name>\n"
    "DELETE <name>: deletes preset <name>\n"
    "EXPORT: prints presets in CSV format\n"
    "IMPORT: imports presets in CSV format (ends with empty line)\n"
    "STATUS: prints status\n"
    "HELP: short help\n"
    "LONGHELP: this help\n"
    "SET <wire-diameter> <coil-length> <turns>: sets parameters\n"
    "SET WIRE <wire-diameter>: in mm\n"
    "SET COIL LENGTH <coil-length>: in mm\n"
    "SET TURNS <turns>: how many turns the winder should go\n"
    "SET HOME: sets HOME position to current position\n"
    "SET SCREW PITCH <pitch>: sets screw pitch in mm\n"
    "SET WINDER STEPS <steps>: sets winder steps\n"
    "SET TRAVERSE STEPS <steps>: sets traverse steps\n"
    "SET WINDER SPEED <rpm>: sets winder max speed\n"
    "SET TRAVERSE SPEED <rpm>: sets traverse max speed\n"
    "SET WINDER DIR [FORWARD|BACKWARD]: sets winder direction\n"
    "SET TRAVERSE DIR [FORWARD|BACKWARD]: sets traverse direction\n"
    "SET LIMIT SWITCH [ON|OFF]: if off, seeks for ZERO slower (to allow for manual STOP command)\n"
    "SET ZERO BEFORE HOME [ON|OFF]: if on, finds zero when going HOME\n"
    "SET HOME BEFORE START [ON|OFF]: if on, goes HOME before winding.\n"
    "GET <parameter>: prints current value of <parameter>");
}

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
  loadMachineConfig();

  Serial.println(F("--- WINDER OS V3.0 ---"));
}

void loop() {
  if (Serial.available()) {
    processCommand(Serial.readStringUntil('\n'));
  }

  switch (state) {
  case RUNNING:
    performWindingStep();
    break;
  case HOMING:
    break;
  case IDLE:
  case PAUSED:
    break;
  }
}

void performWindingStep() {
  if (state != RUNNING)
    return; // Safety check

  if (currentStepsW < (active.totalTurns * cfg.stepsPerRevW)) {

    // 1. Set Winder Direction (Winder usually spins one way per preset)
    digitalWrite(W_DIR, cfg.dirW ? HIGH : LOW);

    // 2. Set Traverse Direction
    // If layerDir is 1 (Forward), use cfg.dirT. If -1, flip it.
    bool tDirActual = (layerDir == 1) ? cfg.dirT : !cfg.dirT;
    digitalWrite(T_DIR, tDirActual ? HIGH : LOW);

    // 3. Winder Pulse START
    digitalWrite(W_STEP, HIGH);

    // 4. Sync Traverse (Bresenham)
    // Steps per turn = wireDia * (StepsPerRevT / ScrewPitch)
    traverseAccumulator +=
        (active.wireDia * ((float)cfg.stepsPerRevT / cfg.screwPitch));

    if (traverseAccumulator >= (float)cfg.stepsPerRevW) {
      digitalWrite(T_STEP, HIGH);
      traverseAccumulator -= (float)cfg.stepsPerRevW;
      absPos += layerDir; // Tracking absolute steps for GOTO commands
    }

    // 5. Timing (Speed control)
    delayMicroseconds(currentDelay);
    digitalWrite(W_STEP, LOW);
    digitalWrite(T_STEP, LOW);
    delayMicroseconds(currentDelay);

    currentStepsW++;
    currentLayerSteps++;

    // 6. Acceleration & RPM Feedback (Every full turn)
    if (currentStepsW % cfg.stepsPerRevW == 0) {
      if (currentRPM < active.targetRPM) {
        currentRPM += active.rampRPM;
        if (currentRPM > active.targetRPM)
          currentRPM = active.targetRPM;
        currentDelay = rpmToDelay(currentRPM);
      }
      // Auto-status update every turn could be too chatty,
      // but keeping it for now as per your request
      printStatus();
    }

    // 7. Layer Flip Logic
    // stepsPerLayer is precalculated at start: (coilWidth / wireDia) *
    // stepsPerRevW
    if (currentLayerSteps >= stepsPerLayer) {
      layerDir *= -1;
      currentLayerSteps = 0;
      Serial.println(F("MSG: Layer flipped"));
    }

  } else {
    Serial.println(F("MSG: Winding complete"));
    stopMachine();
  }
}