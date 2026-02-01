#include "eeprom.h"
#include "kbPickupWinder.h"
#include "taskqueue.h"

// SoftwareSerial nextionSerial(2, 3);

// --- GLOBAL STATE ---

float stepsPerMM;
long absPos = 0; // Traverse steps from 0
bool isHomed = false;
int homingPhase = 0; // 0: searching switch, 1: backing off

unsigned long lastStepMicros = 0;
float traverseAccumulator = 0;
long currentLayerSteps = 0;
int layerDir = 1;

// to było:
long currentStepsW = 0; // Winder progress in steps
long stepsPerLayer = 0;

const float backoffDistanceMM = 1;

// --- UTILS ---

void updateDerivedValues() {
  // Basic math: 1600 steps / 2.0 mm = 800 steps/mm
  if (cfg.screwPitch > 0) {
    stepsPerMM = (float)cfg.stepsPerRevT / cfg.screwPitch;
  } else {
    stepsPerMM = 800.0; // Safety default
  }
}

long rpmToDelay(int rpm) {
  if (rpm < 10)
    rpm = 10;
  return 30000000L / ((long)rpm * cfg.stepsPerRevW);
}

// --- CORE FUNCTIONS: START ---

void parseStartCommand(String params) {
  params.trim();

  // if empty - we're using current 'active' parameters.
  if (params.length() == 0) {
    initiateWinding();
    return;
  } else if (params.startsWith("\"") || !isdigit(params[0])) {
    // Parameter is a preset name (in quotes or just text)
    if (loadPresetByName(params))
      initiateWinding();
  } else {
    // Parameters are numeric values
    if (parseStartCommandNumericValues(params))
      initiateWinding();
  }
}

bool parseStartCommandNumericValues(String params) {
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

  if (count < 3) {
    Serial.println(
        F("ERROR: START requires at least 3 parameters. Syntax:\n"
          "wireDiameter coilWidth turns [targetRPM] [rampRPM] [startOffset])"));
    return false;
  }

  // Przypisz sparsowane wartości do 'active'

  active.wireDia = vals[0];
  active.coilWidth = vals[1];
  active.totalTurns = (long)vals[2];
  if (count >= 4)
    active.targetRPM = (int)vals[3];
  if (count >= 5)
    active.rampRPM = (int)vals[4];
  if (count >= 6)
    active.startOffset = (long)vals[5];

  return true;
}

void initiateWinding() {
  // 1. Walidacja podstawowa
  if (active.totalTurns <= 0 || active.wireDia <= 0 || active.coilWidth <= 0) {
    Serial.println(F("ERROR: Invalid parameters (Wire, Width, or Turns is 0)"));
    return;
  }

  // 2. Inicjalizacja parametrów sesji
  updateDerivedValues();
  // Obliczamy ile kroków nawijarki przypada na jedną pełną warstwę
  // (coilWidth / wireDia) to liczba zwojów na warstwę
  stepsPerLayer =
      (long)((active.coilWidth / active.wireDia) * (float)cfg.stepsPerRevW);
  currentStepsW = 0;
  currentLayerSteps = 0;
  traverseAccumulator = 0;
  layerDir = 1; // Zaczynamy odsuwając się od Home

  // 3. Obsługa Bazowania (Homing) przed startem
  if (cfg.homeBeforeStart && !isHomed) {
    initiateHoming();
  }

  // KROK 2: Dojazd do offsetu
  // enqueueMoveT(active.startOffset, cfg.maxRPM_T);
  // TODO:
  // taskState, motor, targetPosition, isRelative=false, rpm, ramp
  enqueueTask(MOVING, 'T', active.startOffset, false, cfg.maxRPM_T, 0.1))

  // KROK 3: Nawijanie (na końcu kolejki)
  float rampValue = (float)active.rampRPM / 1000.0; // Scale ramp for engine
  // taskState, motor, targetSteps, isRelative=true, rpm, ramp
  enqueueTask(RUNNING, 'S', active.totalTurns * cfg.stepsPerRevW, true,
              active.targetRPM, rampValue);

  Serial.println(F("MSG: Winding sequence enqueued."));

  // 6. Aktywacja silników i zmiana stanu
  digitalWrite(EN, LOW); // Prąd na silniki
  printStatus();
}

// --- CORE FUNCTIONS: PAUSE & RESUME ---

void pauseTask() {
  // ***** TODO
}

void resumeTask() {
  // ***** TODO: obsolete. resumeTask should resume current Task and not winding
  // ;)
  if (state == PAUSED) {
    // currentRPM = (motor=='W') ? cfg.startRPM_W : cfg.startRPM_T; // Restart
    // from slow speed; obsolete, but use this speeds
    windingDelay = rpmToDelay(currentRPM);
    state = RUNNING;
    digitalWrite(EN, LOW); // Motors back online
    Serial.println(F("MSG: Resuming with ramp-up"));
  }
}

// --- CORE FUNCTIONS: GOTO HOME, W, T ---

void handleGotoCommand(String cmd) {
  // GOTO <position> [speed] lub GOTO HOME [speed]
  String param = cmd.substring(5);
  param.trim();
  int spaceIdx = param.indexOf(' ');
  String posStr = (spaceIdx == -1) ? param : param.substring(0, spaceIdx);
  int rpm =
      (spaceIdx == -1) ? cfg.maxRPM_T : param.substring(spaceIdx + 1).toInt();

  long targetStepsAbs = (long)(posStr.toFloat() * stepsPerMM);

  if (posStr == F("HOME")) {
    // GOTO HOME zawsze jedzie do punktu startowego aktualnego presetu
    targetStepsAbs = active.startOffset;
    enqueueTask(MOVING, 'T', targetStepsAbs, false, rpm, 0.1);
  } else {
    targetPos = (long)(posStr.toFloat() * stepsPerMM);
    enqueueTask(MOVING, 'T', targetPos, true, rpm, 0.1);
  }
}

void moveManual(String cmd) {
  char motor = cmd[0]; // 'W' lub 'T'
  String param = cmd.substring(2);
  int spaceIdx = param.indexOf(' ');
  float val = param.substring(0, spaceIdx).toFloat();
  int rpm = (spaceIdx == -1) ? 60 : param.substring(spaceIdx + 1).toInt();

  long steps = 0;
  bool direction;

  if (motor == 'W') {
    steps = (long)(val * cfg.stepsPerRevW);
  } else {
    steps = (long)(val * stepsPerMM);
  }

  // true = ruch relatywny, targetSteps to abs(steps), dir ustawiony
  enqueueTask(MOVING, motor, steps, true, rpm, 0.1);
}

// --- CORE FUNCTIONS: SETUP & LOOP ---

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

  Serial.println(F("--- kbPickupWinder OS V0.1 ---"));
}

void loop() {
  if (Serial.available()) {
    processCommand(Serial.readStringUntil('\n'));
  }
  executeMotion(getCurrentTask());
}

// --- CORE FUNCTIONS: SEEK ZERO ---

void initiateHoming() {
  int speed = cfg.useLimitSwitch ? cfg.maxRPM_T : cfg.startRPM_T;

  homingPhase = 0;
  isHomed = false;

  digitalWrite(EN, LOW);
  // Ustawienie kierunku w stronę krańcówki
  digitalWrite(T_DIR, cfg.dirT ? LOW : HIGH);

  // taskState, motor, targetSteps=1M steps (as a safety limit),
  // isRelative=true, rpm, ramp
  enqueueTask(HOMING, 'T', 1000000L, true, speed, 0.05);

  Serial.print(F("MSG: Homing added to queue at "));
  Serial.print(speed);
  Serial.println(F(" RPM..."));
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

void performSeekZero(Task *current) {
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
      // Note: In Phase 0 we don't necessarily update absPos until it's set to
      // 0
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

void performMovingStep(Task *current) {
  // obsolete
  bool finished = false;
  long dly =
      rpmToDelay(current->rpm,
                 (current->motor == 'W' ? cfg.stepsPerRevW : cfg.stepsPerRevT));

  if (current->motor == 'T') {
    if (current->relative) { // Ruch T <distance>
      if (moveStepsLeft != 0) {
        digitalWrite(T_DIR, (moveStepsLeft > 0) ? cfg.dirT : !cfg.dirT);
        singleStep(T_STEP, dly);
        absPos += (moveStepsLeft > 0) ? 1 : -1;
        moveStepsLeft += (moveStepsLeft > 0) ? -1 : 1;
      } else {
        finished = true;
      }
    } else { // Ruch GOTO <position>
      if (absPos != current->target) {
        digitalWrite(T_DIR, (current->target > absPos) ? cfg.dirT : !cfg.dirT);
        singleStep(T_STEP, dly);
        absPos += (current->target > absPos) ? 1 : -1;
      } else {
        finished = true;
      }
    }
  } else if (activeMotor == 'W') { // Ruch W <distance>
    if (moveStepsLeft != 0) {
      digitalWrite(W_DIR, (moveStepsLeft > 0) ? cfg.dirW : !cfg.dirW);
      singleStep(W_STEP, dly);
      moveStepsLeft += (moveStepsLeft > 0) ? -1 : 1;

      // Jeśli odwijamy na pauzie, aktualizujemy postęp nawijania
      if (isPaused) {
        if (moveStepsLeft > 0)
          currentStepsW++;
        else
          currentStepsW--;
      }
    } else {
      finished = true;
    }
  }
  if (finished) {
    popState();
    if (!isPaused) { // wtf?
      digitalWrite(EN,
                   HIGH); // Offline tylko jeśli nie jesteśmy w trybie pauzy
    }
    Serial.println(F("MSG: Move completed"));
  }
}

void singleStep(int pin, long dly) {
  digitalWrite(pin, HIGH);
  delayMicroseconds(dly);
  digitalWrite(pin, LOW);
  delayMicroseconds(dly);
}

// --- HANDLE ACTUAL TASK OPERATIONS ---
// startTask(), executeMotion(), stepActiveMotor(), updateTaskRamp()

void startTask(Task *t) {
  if (t->isStarted)
    return;

  if (t->isRelative) {
    t->targetPosition = absPos + t->targetSteps;
    // t->dir already set
  } else {
    t->targetSteps = abs(t->targetPosition - absPos);
    t->dir = (t->targetPosition > absPos) ? -1 : 1;
  }

  lastStepMicros = micros();
  digitalWrite(EN, LOW); // Prąd na silniki
  t->isStarted = true;
}

void executeMotion(Task *t) {
  if (t == NULL || t->state == IDLE || t->state == PAUSED ||
      t->state == ERROR) { // IDLE
    digitalWrite(EN, HIGH);
    return;
  }
  // case RUNNING, MOVING, HOMING:

  if (!t->isStarted) {
    startTask(t);
  }

  unsigned long now = micros();

  // Select Master Motor for timing calculation
  int spr = (t->state == RUNNING || t->motor == 'W') ? cfg.stepsPerRevW
                                                     : cfg.stepsPerRevT;
  long currentDelay = 30000000L / ((long)t->currentRPM * spr);

  if (now - lastStepMicros >= currentDelay) {
    lastStepMicros = now;

    stepActiveMotor(t);
    updateTaskRamp(t);
    t->currentSteps++;

    handleHomingLogic(t);
    handleTaskEnd(t);
  }
}

void stepActiveMotor(Task *t) {
  if (t->state == RUNNING) {
    // --- SYNCHRONIZED WINDING (Master: Winder, Slave: Traverse) ---
    digitalWrite(W_DIR, cfg.dirW ? HIGH : LOW);
    digitalWrite(W_STEP, HIGH);

    // Synchronizacja (Bresenham)
    float ratio = (active.wireDia / cfg.screwPitch) *
                  ((float)cfg.stepsPerRevT / cfg.stepsPerRevW);
    traverseAccumulator += ratio;

    // WHILE instead of IF handles wires thicker than screw pitch step
    // equivalent
    while (traverseAccumulator >= 1.0) {
      digitalWrite(T_DIR, (layerDir == 1) ? cfg.dirT : !cfg.dirT);
      digitalWrite(T_STEP, HIGH);
      delayMicroseconds(2); // Small pulse for T driver
      digitalWrite(T_STEP, LOW);

      absPos += (layerDir == 1) ? 1 : -1;
      traverseAccumulator -= 1.0;
      currentLayerSteps++;
    }

    // Layer Flip Logic
    long stepsInLayer =
        (long)((active.coilWidth / active.wireDia) * cfg.stepsPerRevW);
    if (currentLayerSteps >= stepsInLayer) {
      layerDir *= -1;
      currentLayerSteps = 0;
      Serial.println(F("MSG: Layer Flip"));
    }
    digitalWrite(W_STEP, LOW);
  } else {
    // --- SINGLE MOTOR MOVE (T or W or Homing) ---
    // Najpierw ustal kierunek dla ruchów innych niż Homing (bo ten ma
    // kierunek ustalony w initiateHoming)
    if (t->state == MOVING) {
      if (t->motor == 'T') {
        // Tu logika dla GOTO lub relatywnego T (ustalenie DIR przed krokiem)
        // To zazwyczaj dzieje się w executeMotion lub przed dodaniem do
        // kolejki
      }
    }

    // direction
    int dirPin = (t->motor == 'W') ? W_DIR : T_DIR;
    bool dirValue = (t->motor == 'W') ? cfg.dirW : cfg.dirT;
    digitalWrite(dirPin, (t->dir == 1) ? dirValue : !dirValue);

    // motion (pulse)
    int sPin = (t->motor == 'W') ? W_STEP : T_STEP;
    digitalWrite(sPin, HIGH);
    delayMicroseconds(2);
    digitalWrite(sPin, LOW);

    if (t->motor == 'T') {
      absPos += (digitalRead(T_DIR) == cfg.dirT) ? 1 : -1;

      // Limit switch safety (except during homing phase 0)
      if (cfg.useLimitSwitch && digitalRead(LIMIT_PIN) == LOW &&
          t->state != HOMING) {
        emergencyStop(false);
        return;
      }
    }
  }
}

void updateTaskRamp(Task *t) {
  // Homing doesn't use deceleration because we don't know where the switch is
  if (t->state == HOMING && homingPhase == 0) {
    if (t->currentRPM < t->targetRPM) {
      t->currentRPM += t->accelRate;
      if (t->currentRPM > t->targetRPM)
        t->currentRPM = t->targetRPM;
    }
    return;
  }

  long stepsRemaining = t->targetSteps - t->currentSteps;

  // 1. Logika hamowania (Deceleration)
  if (stepsRemaining <= t->accelDistance || t->isDecelerating) {
    t->isDecelerating = true;
    if (t->currentRPM > t->startRPM) {
      t->currentRPM -= t->accelRate;
      if (t->currentRPM < t->startRPM)
        t->currentRPM = t->startRPM;
    }
  }
  // 2. Logika przyspieszania (Acceleration)
  else if (t->currentRPM < t->targetRPM) {
    t->currentRPM += t->accelRate;
    t->accelDistance++; // Dystans potrzebny na rozpędzenie
    if (t->currentRPM > t->targetRPM)
      t->currentRPM = t->targetRPM;
  }
}

void handleHomingLogic(Task *t) {
  if (t->state == HOMING && homingPhase == 0) {
    if (cfg.useLimitSwitch && digitalRead(LIMIT_PIN) == LOW) {
      // Phase 0: Just hit the switch
      absPos = 0;
      isHomed = true;
      homingPhase = 1;

      t->targetSteps = (long)(backoffDistanceMM * stepsPerMM); // 1mm backoff
      t->currentSteps = 0;
      t->accelDistance = 0;
      t->isDecelerating = false;

      digitalWrite(T_DIR, cfg.dirT ? HIGH : LOW); // Away from switch

      Serial.print(F("MSG: Switch hit. Backing off "));
      Serial.print(backoffDistanceMM);
      Serial.println(F(" mm"));
    }
  }
}

// --- CORE FUNCTIONS: STOP & CLOSING TASK ---

void emergencyStop(bool userAsked) {
  digitalWrite(EN, HIGH); // Offline motors
  clearQueue();
  if (userAsked) {
    Serial.println(F("Manual stop, queue cleared."));
  } else {
    Serial.println(
        F("ALARM: EMERGENCY STOP! Limit switch hit. Queue cleared."));
  }
}

void handleTaskEnd(Task *t) {
  if (t->currentSteps >= t->targetSteps) {
    t->isComplete = true;
    dequeueTask();
    if (taskCount == 0)
      digitalWrite(EN, HIGH);

    if (t->state == HOMING) {
      Serial.println(F("MSG: Homing finished. Zero established."));
    } else {
      Serial.println(F("MSG: Task complete."));
    }
  }
}