#include "Guitar_pickup_winder.h"
#include "presets.h"

SoftwareSerial nextionSerial(2, 3);

// --- GLOBAL STATE ---
MachineConfig cfg;
WindingPreset prst;

long absPos = 0;            // Traverse steps from 0
long currentStepsW = 0;     // Winder progress in steps
long currentLayerSteps = 0; // Steps in current layer
long stepsPerLayer = 0;
int layerDir = 1;
float traverseAccumulator = 0;

int startRPM = 50;

int currentRPM = 0;
long currentDelay = 800;

int homingPhase = 0;       // 0: searching switch, 1: backing off
long homingDelay = 400;    // Current delay for steps
long backoffStepsLeft = 0; // Counter for the return move

// Zmienne pomocnicze dla ruchów asynchronicznych
char activeMotor = ' ';    // 'W' lub 'T'
long targetAbsPos = 0;     // Docelowa pozycja dla T (w krokach)
long moveStepsLeft = 0;    // Licznik kroków dla ruchu relatywnego (W/T)
long moveDelay = 400;      // Aktualne opóźnienie (prędkość)
bool moveRelative = false; // Flaga określająca typ ruchu

float stepsPerMM;

void updateDerivedValues() {
  // Basic math: 1600 steps / 2.0 mm = 800 steps/mm
  if (cfg.screwPitch > 0) {
    stepsPerMM = (float)cfg.stepsPerRevT / cfg.screwPitch;
  } else {
    stepsPerMM = 800.0; // Safety default
  }
}

// --- UTILS ---

long rpmToDelay(int rpm) {
  if (rpm < 10)
    rpm = 10;
  return 30000000L / ((long)rpm * cfg.stepsPerRevW);
}

// --- CORE FUNCTIONS ---

void stopMachine() {
  state = IDLE;
  digitalWrite(EN, HIGH); // Offline
  Serial.println(F("MSG: Stopped"));
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

void moveManual(String cmd) {
  activeMotor = cmd[0];
  String param = cmd.substring(2);
  int spaceIdx = param.indexOf(' ');
  float dist =
      ((spaceIdx == -1) ? param : param.substring(0, spaceIdx)).toFloat();
  int rpm = (spaceIdx == -1) ? (activeMotor == 'W' ? 100 : 60)
                             : param.substring(spaceIdx + 1).toInt();

  moveRelative = true;
  moveDelay = 30000000L / ((long)rpm * (activeMotor == 'W' ? cfg.stepsPerRevW
                                                           : cfg.stepsPerRevT));

  if (activeMotor == 'W') {
    moveStepsLeft = (long)(dist * cfg.stepsPerRevW); // distance w obrotach
  } else {
    moveStepsLeft = (long)(dist * stepsPerMM); // distance w mm
  }

  state = MOVING;
  digitalWrite(EN, LOW);
}

void handleStartCommand(String params) {
  params.trim();
  if (params.length() == 0) {
    startMachine();
    return;
  }

  // Sprawdź czy parametr to nazwa presetu (w cudzysłowie lub tekst)
  if (params.startsWith("\"") || !isdigit(params[0])) {
    String name = params;
    if (loadPresetByName(name)) {
      startMachine();
    }
    return;
  }

  // Jeśli to liczby, parsujemy pozycje spacji
  float vals[6];
  int count = 0;
  int pos = 0;
  while (pos < params.length() && count < 6) {
    int nextSpace = params.indexOf(' ', pos);
    String part = (nextSpace == -1) ? params.substring(pos)
                                    : params.substring(pos, nextSpace);
    vals[count++] = part.toFloat();
    if (nextSpace == -1)
      break;
    pos = nextSpace + 1;
  }

  // Przypisz sparsowane wartości do 'active'
  if (count >= 3) {
    active.wireDia = vals[0];
    active.coilWidth = vals[1];
    active.totalTurns = (long)vals[2];
    if (count >= 4)
      active.targetRPM = (int)vals[3];
    if (count >= 5)
      active.rampRPM = (int)vals[4];
    if (count >= 6)
      active.startOffset = (long)vals[5];

    startMachine();
  } else {
    Serial.println(
        F("ERROR: START requires at least 3 parameters (wire width turns)"));
  }
}

void startMachine() {
  // 1. Walidacja podstawowa
  if (active.totalTurns <= 0 || active.wireDia <= 0 || active.coilWidth <= 0) {
    Serial.println(F("ERROR: Invalid parameters (Wire, Width, or Turns is 0)"));
    state = IDLE;
    return;
  }

  // 2. Obsługa Bazowania (Homing) przed startem
  if (cfg.homeBeforeStart && !isHomed) {
    Serial.println(F("MSG: Auto-homing before start..."));
    // Tutaj można wywołać proces homingu, ale ponieważ jest asynchroniczny,
    // najlepiej byłoby zmienić stan na HOMING, a po jego zakończeniu wrócić do
    // START. Na potrzeby V3.0 przyjmijmy, że użytkownik musi być zhomowany.
    state = IDLE;
    Serial.println(F("ERROR: Machine not homed. Use SEEK ZERO first."));
    return;
  }

  // 3. Dojazd do pozycji START (Offset)
  if (absPos != active.startOffset) {
    Serial.println(F("MSG: Moving to start offset..."));
    moveTraverseAbs(active.startOffset, cfg.maxRPM_T);
  }

  // 4. Inicjalizacja parametrów sesji
  // Obliczamy ile kroków nawijarki przypada na jedną pełną warstwę
  // (coilWidth / wireDia) to liczba zwojów na warstwę
  stepsPerLayer =
      (long)((active.coilWidth / active.wireDia) * (float)cfg.stepsPerRevW);

  currentStepsW = 0;
  currentLayerSteps = 0;
  traverseAccumulator = 0;
  layerDir = 1; // Zaczynamy odsuwając się od Home

  // 5. Dynamika (Soft Start)
  currentRPM = 50; // Zawsze startujemy bezpiecznie (można dodać cfg.startRPM)
  currentDelay = rpmToDelay(currentRPM);

  // 6. Aktywacja silników i zmiana stanu
  digitalWrite(EN, LOW); // Prąd na silniki
  state = RUNNING;

  Serial.println(F("MSG: Winding started."));
  printStatus();
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

  if (cmd.startsWith(F("STOP"))) {
    stopMachine();
  } else if (cmd.startsWith(F("START"))) {
    handleStartCommand(cmd.substring(6));
  } else if (cmd.startsWith(F("PAUSE"))) {
    state = PAUSED;
    digitalWrite(EN, HIGH);
    Serial.println(F("MSG: Paused (Offline)"));
  } else if (cmd.startsWith(F("RESUME"))) {
    resumeWinding();
  } else if (cmd.startsWith(F("GOTO "))) {
    handleGotoCommand(cmd);
  } else if (cmd.startsWith(F("SEEK ZERO"))) {
    startSeekingZero(cmd);
  } else if (cmd == F("SET ZERO")) {
    absPos = 0;
    isHomed = true;
    Serial.println(F("MSG: Current position set as ZERO"));
  } else if (cmd == F("SET HOME")) {
    cfg.homePos = absPos;
    EEPROM.put(EEPROM_CONF_ADDR, cfg);
    Serial.print(F("MSG: HOME set to "));
    Serial.println(absPos / stepsPerMM);
  } else if (cmd.startsWith(F("SET "))) {
    handleSet(cmd);
  } else if (cmd.startsWith(F("GET"))) {
    handleGet(cmd);
  } else if (cmd.startsWith(F("SAVE "))) {
    savePreset(cmd.substring(5));
  } else if (cmd.startsWith(F("LOAD "))) {
    loadPresetByName(cmd.substring(5));
  } else if (cmd.startsWith(F("DELETE "))) {
    deletePreset(cmd.substring(7));
  } else if (cmd.startsWith(F("EXPORT"))) {
    exportCSV();
  } else if (cmd.startsWith(F("STATUS"))) {
    printStatus();
  } else if (cmd.startsWith(F("HELP"))) {
    printHelp();
  } else if (cmd.startsWith(F("LONGHELP"))) {
    printLongHelp();
  } else if (cmd.startsWith(F("SETHELP"))) {
    printSetHelp();
  } // (... handle more commands...)
  else if (cmd == "H") {
    homeTraverse();
  } else if (cmd.startsWith("T ") || cmd.startsWith("W ")) {
    moveManual(cmd);
  }
}

void handleGotoCommand(String cmd) {
  // GOTO <position> [speed] lub GOTO HOME [speed]
  String param = cmd.substring(5);
  int spaceIdx = param.indexOf(' ');
  String posStr = (spaceIdx == -1) ? param : param.substring(0, spaceIdx);
  int rpm =
      (spaceIdx == -1) ? cfg.maxRPM_T : param.substring(spaceIdx + 1).toInt();

  activeMotor = 'T';
  moveRelative = false;
  moveDelay = 30000000L / ((long)rpm * cfg.stepsPerRevT);

  if (posStr == F("HOME")) {
    targetAbsPos = cfg.homePos; // Ustawione przez SET HOME
  } else {
    targetAbsPos = (long)(posStr.toFloat() * stepsPerMM);
  }

  state = MOVING;
  digitalWrite(EN, LOW);
}

void printHelp() {
  Serial.println(F("Movement: W, T, GOTO T, SEEK ZERO, HOME\n"
                   "Control: START, STOP, PAUSE, RESUME\n"
                   "Presets: SAVE, LOAD, DELETE, EXPORT, IMPORT\n"
                   "Settings: SET ..., GET ...\n"
                   "Info: STATUS, HELP, LONGHELP, SETHELP"));
}

void printLongHelp() {
  Serial.println(F(
      "START (<preset>|<wire-diameter> <coil-length> <turns> [rpm] [ramp] "
      "[offset]):\n"
      "  starts winding the coil\n"
      "STOP: stop winding\n"
      "PAUSE: pause winding and put motors in offline; doesn't reset position\n"
      "RESUME: resume winding after PAUSE\n"
      "SEEK ZERO [speed]: finds ZERO position (by moving Traverse backward\n"
      "  until the limit switch is found or STOP command is sent)\n"
      "W <distance> [speed]: move Winder to relative position\n"
      "T <distance> [speed]: move Traverse to relative position\n"
      "GOTO <position> [speed]: move Traverse to absolute position\n"
      "GOTO HOME [speed]: goes to HOME position\n"
      "SAVE <name> [<wire-diameter> <coil-length> <turns>]: saves preset "
      "<name>\n"
      "LOAD <name>: loads preset <name>\n"
      "DELETE <name>: deletes preset <name>\n"
      "EXPORT: prints presets in CSV format\n"
      "IMPORT: imports presets in CSV format (ends with empty line)\n"
      "STATUS: prints status\n"
      "SET ... : sets parameter(s)\n"
      "GET ... : gets parameter(s)\n"
      "SETHELP: parameters list\n"
      "HELP: short help\n"
      "LONGHELP: this help"));
}

void printSetHelp() {
  Serial.println(
      F("SET <wire-diameter> <coil-length> <turns>: sets parameters\n"
        "SET WIRE <wire-diameter>: in mm\n"
        "SET COIL LENGTH <coil-length>: in mm\n"
        "SET TURNS <turns>: how many turns the winder should go\n"
        "SET ZERO: sets ZERO position to current position\n"
        "SET HOME: sets HOME position to current position\n"
        "SET SCREW PITCH <pitch>: sets screw pitch in mm\n"
        "SET WINDER STEPS <steps>: sets winder steps\n"
        "SET TRAVERSE STEPS <steps>: sets traverse steps\n"
        "SET WINDER SPEED <rpm>: sets winder max speed\n"
        "SET TRAVERSE SPEED <rpm>: sets traverse max speed\n"
        "SET WINDER DIR [FORWARD|BACKWARD]: sets winder direction\n"
        "SET TRAVERSE DIR [FORWARD|BACKWARD]: sets traverse direction\n"
        "SET LIMIT SWITCH [ON|OFF]: if off, seeks for ZERO slower (to allow\n"
        "  for manual STOP command)\n"
        "SET ZERO BEFORE HOME [ON|OFF]: if on, finds zero when going HOME\n"
        "SET HOME BEFORE START [ON|OFF]: if on, goes HOME before winding.\n"
        "GET [parameter]: prints current value of <parameter> (or all "
        "parameters if not specified)"));
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
  case MOVING:
    performMovingStep();
    break;
  case HOMING:
    performSeekZero();
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

    // 1. Set Winder Direction (Winder always spins one way)
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
      // but keeping it for now
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

void startSeekingZero(String cmd) {
  String speedPart = cmd.substring(9);
  speedPart.trim();

  int rpm;
  if (speedPart.length() > 0) {
    rpm = speedPart.toInt();
  } else {
    // If no speed given, use max or slow speed based on switch config
    rpm = cfg.useLimitSwitch ? cfg.maxRPM_T
                             : 60; // 60 RPM is safe for manual STOP
  }

  // Initializing Homing State
  currentRPM = rpm;
  homingDelay = 30000000L / ((long)currentRPM * cfg.stepsPerRevT);
  homingPhase = 0;
  state = HOMING;

  digitalWrite(EN, LOW);
  // Direction: move towards home (reverse of cfg.dirT)
  digitalWrite(T_DIR, cfg.dirT ? LOW : HIGH);

  Serial.print(F("MSG: Seeking ZERO at "));
  Serial.print(currentRPM);
  Serial.println(F(" RPM"));
}

void performSeekZero() {
  if (homingPhase == 0) {
    // Phase 0: Searching for limit switch
    // Check if switch hit (if enabled)
    if (cfg.useLimitSwitch && digitalRead(LIMIT_PIN) == LOW) {
      absPos = 0;
      isHomed = true;
      homingPhase = 1;
      backoffStepsLeft = 800; // Move back 1mm (800 steps for 2mm pitch)
      digitalWrite(T_DIR, cfg.dirT ? HIGH : LOW); // Flip direction
      Serial.println(F("MSG: Switch hit. Backing off..."));
    } else {
      // Perform a single step toward zero
      digitalWrite(T_STEP, HIGH);
      delayMicroseconds(homingDelay);
      digitalWrite(T_STEP, LOW);
      delayMicroseconds(homingDelay);
      // Note: In Phase 0 we don't necessarily update absPos until it's set to 0
    }
  } else if (homingPhase == 1) {
    // Phase 1: Backing off for precision
    if (backoffStepsLeft > 0) {
      digitalWrite(T_STEP, HIGH);
      delayMicroseconds(homingDelay);
      digitalWrite(T_STEP, LOW);
      delayMicroseconds(homingDelay);

      backoffStepsLeft--;
      absPos += cfg.dirT ? 1 : -1;
    } else {
      state = IDLE;
      Serial.print(F("MSG: Zero established. Position: "));
      Serial.println(absPos / stepsPerMM);
    }
  }
}

void performMovingStep() {
  if (activeMotor == 'T') {
    if (moveRelative) { // Ruch T <distance>
      if (moveStepsLeft != 0) {
        digitalWrite(T_DIR, (moveStepsLeft > 0) ? cfg.dirT : !cfg.dirT);
        singleStep(T_STEP, moveDelay);
        absPos += (moveStepsLeft > 0) ? 1 : -1;
        moveStepsLeft += (moveStepsLeft > 0) ? -1 : 1;
      } else {
        state = IDLE;
      }
    } else { // Ruch GOTO <position>
      if (absPos != targetAbsPos) {
        digitalWrite(T_DIR, (targetAbsPos > absPos) ? cfg.dirT : !cfg.dirT);
        singleStep(T_STEP, moveDelay);
        absPos += (targetAbsPos > absPos) ? 1 : -1;
      } else {
        state = IDLE;
      }
    }
  } else if (activeMotor == 'W') { // Ruch W <distance>
    if (moveStepsLeft != 0) {
      digitalWrite(W_DIR, (moveStepsLeft > 0) ? cfg.dirW : !cfg.dirW);
      singleStep(W_STEP, moveDelay);
      moveStepsLeft += (moveStepsLeft > 0) ? -1 : 1;

      // Jeśli odwijamy na pauzie, aktualizujemy postęp nawijania
      if (isPaused) {
        if (moveStepsLeft > 0)
          currentStepsW++;
        else
          currentStepsW--;
      }
    } else {
      state = IDLE;
    }
  }
  if (state == IDLE) {
    if (!isPaused) {          // wtf?
      digitalWrite(EN, HIGH); // Offline tylko jeśli nie jesteśmy w trybie pauzy
    }
    Serial.println(F("MSG: Move complete"));
  }
}

void singleStep(int pin, long dly) {
  digitalWrite(pin, HIGH);
  delayMicroseconds(dly);
  digitalWrite(pin, LOW);
  delayMicroseconds(dly);
}