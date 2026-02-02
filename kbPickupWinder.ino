const char version[4] = "0.1\0";

#include "eeprom.h"
#include "kbPickupWinder.h"
#include "serial.h"
#include "taskqueue.h"
#include "variables.h"

// SoftwareSerial nextionSerial(2, 3);

// --- UTILS ---

void updateDerivedValues() {
  // Basic math: 1600 steps / 1.0 mm = 1600 steps/mm
  if (cfg.screwPitch > 0) {
    stepsPerMM = (float)cfg.stepsPerRevT / cfg.screwPitch;
  } else {
    stepsPerMM = 1600.0; // Safety default
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
  // taskState, motor, targetPosition, isRelative=false, rpm, ramp
  if (cfg.useStartOffset) {
    enqueueTask(MOVING, 'T', active.startOffset, false, cfg.maxRPM_T, 0.1);
  }

  // KROK 3: Nawijanie (na końcu kolejki)
  float rampValue = (float)active.rampRPM / 1000.0; // Scale ramp for engine
  // taskState, motor, targetSteps, isRelative=true, rpm, ramp
  enqueueTask(RUNNING, 'S', active.totalTurns * cfg.stepsPerRevW, true,
              getMaxRPMForCurrentPreset(), rampValue);

  Serial.println(F("MSG: Winding sequence enqueued."));

  // 6. Aktywacja silników i zmiana stanu
  digitalWrite(EN, LOW); // Prąd na silniki
  printStatus();
}

float getMaxRPMForCurrentPreset() {
  // 1. Sprawdź limit nawijarki (Winder)
  float safeRPM = (float)active.targetRPM;
  if (safeRPM > cfg.maxRPM_W) {
    safeRPM = cfg.maxRPM_W;
    Serial.print(F("MSG: Capping RPM to Winder Max: "));
    Serial.println(cfg.maxRPM_W);
  }

  // 2. Sprawdź limit prowadnicy (Traverse)
  // MaxWinderRPM = MaxTraverseRPM * (ScrewPitch / WireDia)
  if (active.wireDia > 0) {
    float maxWinderByTraverse =
        (float)cfg.maxRPM_T * (cfg.screwPitch / active.wireDia);

    if (safeRPM > maxWinderByTraverse) {
      safeRPM = maxWinderByTraverse;
      Serial.print(F("WARNING: Wire too thick! Capping Winder RPM to: "));
      Serial.println(safeRPM);
    }
  }
  return safeRPM;
}
// --- CORE FUNCTIONS: PAUSE & RESUME ---

void pauseTask() {
  Task *t = getCurrentTask();
  // Pauzujemy tylko jeśli coś się faktycznie rusza
  if (t == NULL || t->state == PAUSED || t->state == IDLE)
    return;

  isPauseRequested = true;
  Serial.println(F("MSG: Soft pause initiated..."));
}

void resumeTask() {
  Task *t = getCurrentTask();
  if (t == NULL || t->state != PAUSED)
    return;

  // Przywracamy stan sprzed pauzy (HOMING, MOVING lub RUNNING)
  t->state = t->prevState;

  t->currentRPM = t->startRPM;
  t->isDecelerating = false;
  isPauseRequested = false;

  digitalWrite(EN, LOW);
  lastStepMicros = micros();
  Serial.println(F("MSG: Task resumed"));
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

  if (posStr == F("HOME")) {
    // GOTO HOME zawsze jedzie do punktu startowego aktualnego presetu
    enqueueTask(MOVING, 'T', active.startOffset, false, rpm, 0.1);
  } else {
    long targetPosInSteps = (long)(posStr.toFloat() * stepsPerMM);
    enqueueTask(MOVING, 'T', targetPosInSteps, false, rpm, 0.1);
  }
}

void moveManual(String cmd) {
  char motor = cmd[0]; // 'W' lub 'T'
  String param = cmd.substring(2);
  param.trim();
  float val;
  int rpm;

  int spaceIdx = param.indexOf(' ');

  if (spaceIdx == -1) {
    // Brak spacji - cała reszta to dystans
    val = param.toFloat();
    rpm = (motor == 'W') ? cfg.maxRPM_W : cfg.maxRPM_T; // Domyślne RPM
  } else {
    val = param.substring(0, spaceIdx).toFloat();
    rpm = param.substring(spaceIdx + 1).toInt();
  }

  long steps = 0;

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
  Serial.print(F("\n\n--- kbPickupWinder OS V"));
  Serial.print(version);
  Serial.println(F("---"));

  // nextionSerial.begin(9600);
  loadMachineConfiguration();

  Serial.println(F("At your service, Your Majesty!\n"));
  printHelp();
  Serial.println();
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
  initiateHoming(speed);
}

void initiateHoming(int speed) {
  if (!cfg.useLimitSwitch) {
    Serial.println(F("ERROR: Cannot seek zero. Limit switches disabled."));
    return;
  }

  homingPhase = 0;
  isHomed = false;

  // digitalWrite(EN, LOW);
  //  Ustawienie kierunku w stronę krańcówki
  // digitalWrite(T_DIR, cfg.dirT ? LOW : HIGH);

  // 160 mm
  long maxTravel = 160L * stepsPerMM;

  // taskState, motor, targetSteps=1M steps (as a safety limit),
  // isRelative=true, rpm, ramp
  enqueueTask(HOMING, 'T', -maxTravel, true, speed, 0.05);

  Serial.print(F("MSG: Homing added to queue at "));
  Serial.print(speed);
  Serial.println(F(" RPM..."));
}

void parseSeekZeroCommand(String cmd) {
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
  initiateHoming(rpm);
}

// --- HANDLE ACTUAL TASK OPERATIONS ---
// startTask(), executeMotion(), stepActiveMotor(), updateTaskRamp()

void startTask(Task *t) {
  if (t->isStarted)
    return;

  if (t->isRelative) {
    t->targetPosition =
        absPos + (t->dir == 1 ? t->targetSteps : -t->targetSteps);
    // t->dir already set
  } else {
    long diff = t->targetPosition - absPos;
    t->targetSteps = abs(diff);
    t->dir = (diff >= 0) ? 1 : -1;
  }
  t->currentRPM = t->startRPM;
  lastStepMicros = micros();
  digitalWrite(EN, LOW); // Prąd na silniki
  t->isStarted = true;
  Serial.println(F("Task started."));
  printStatus();
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

  // Jeśli trwa hamowanie do pauzy i osiągnęliśmy prędkość minimalną
  if (isPauseRequested && t->currentRPM <= t->startRPM) {
    t->prevState = t->state; // Zapamiętaj czy to był RUNNING, MOVING czy HOMING
    t->state = PAUSED;
    isPauseRequested = false;
    digitalWrite(EN, HIGH); // Odłącz prąd (bezpieczeństwo i chłodzenie)
    Serial.println(F("MSG: Status set to PAUSED"));
    return;
  }

  unsigned long now = micros();

  // Select Master Motor for timing calculation
  int spr = (t->state == RUNNING || t->motor == 'W') ? cfg.stepsPerRevW
                                                     : cfg.stepsPerRevT;
  unsigned long currentDelay = 60000000L / ((unsigned long)t->currentRPM * spr);

  if (now - lastStepMicros >= currentDelay) {
    lastStepMicros = now;

    stepActiveMotor(t);
    updateTaskRamp(t);
    t->currentSteps++;

    handleHomingLogic(t);
    handleTaskEnd(t);
  }
  if (t->state == ERROR) {
    Serial.println(F("ERROR encountered. Stopping motors, clearing queue."));
    digitalWrite(EN, HIGH);
    clearQueue();
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
      float currentTurns = (float)t->currentSteps / cfg.stepsPerRevW;
      Serial.print(F("MSG: Layer Flip ("));
      Serial.print(currentTurns, 1);
      Serial.print(F(" turns / "));
      Serial.print(active.totalTurns);
      Serial.println(F(")"));
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
  long stepsRemaining = t->targetSteps - t->currentSteps;

  // Flaga wymuszająca hamowanie: albo żądanie pauzy, albo naturalny koniec
  // rampy
  bool forceDecel = isPauseRequested || t->isDecelerating;

  // Hamujemy przy dojeździe do celu TYLKO jeśli to nie jest faza szukania
  // switcha
  bool arrivalDecel = (t->state != HOMING || homingPhase != 0) &&
                      (stepsRemaining <= t->accelDistance);

  if (forceDecel || arrivalDecel) {
    t->isDecelerating = true;
    if (t->currentRPM > t->startRPM) {
      // Przy pauzie możemy hamować nieco szybciej (accelRate * 3)
      float decelStep = isPauseRequested ? (t->accelRate * 3.0) : t->accelRate;
      t->currentRPM -= decelStep;
      if (t->currentRPM < t->startRPM)
        t->currentRPM = t->startRPM;
    }
  } else if (t->currentRPM < t->targetRPM) {
    t->currentRPM += t->accelRate;
    t->accelDistance++;
    if (t->currentRPM > t->targetRPM)
      t->currentRPM = t->targetRPM;
  }
}

void handleHomingLogic(Task *t) {
  if (t->state != HOMING)
    return;

  // ZABEZPIECZENIE 1: Jeśli krańcówki są wyłączone w menu, przerwij bazowanie
  if (!cfg.useLimitSwitch) {
    Serial.println(
        F("ERROR: Homing failed. Limit switches are disabled in CFG."));
    t->state = ERROR;
    t->isComplete = true;
    return;
  }

  // --- FAZA 0 lub 2: Szukamy fizycznego kliknięcia ---
  if (homingPhase == 0 || homingPhase == 2) {
    if (digitalRead(LIMIT_PIN) == LOW) {
      if (homingPhase == 0) {
        homingPhase = 1;
        t->currentSteps = 0;
        t->targetSteps = (long)(cfg.backoffDistanceMM * stepsPerMM);
        t->dir = -t->dir;
        digitalWrite(T_DIR, (t->dir == 1) ? cfg.dirT : !cfg.dirT);
        Serial.println(F("MSG: Switch hit. Phase 1 (Backoff)"));
      } else {
        absPos = 0;
        isHomed = true;
        homingPhase = 3;
        t->currentSteps = t->targetSteps; // Koniec zadania
        Serial.println(F("MSG: Precision Home reached. Zero set."));
      }
    }
  }

  // --- FAZA 1: Koniec odjazdu -> start powolnego podejścia ---
  if (homingPhase == 1 && t->currentSteps >= t->targetSteps) {
    homingPhase = 2;
    t->currentSteps = 0;

    // ZABEZPIECZENIE 2: Zamiast 999999, dajemy np. 2x backoffDistance.
    // Jeśli w tym dystansie nie trafi w switch, znaczy że coś jest nie tak.
    t->targetSteps = (long)(cfg.backoffDistanceMM * 2.0 * stepsPerMM);

    t->targetRPM = 20.0;
    t->currentRPM = 20.0;
    t->dir = -t->dir;
    digitalWrite(T_DIR, (t->dir == 1) ? cfg.dirT : !cfg.dirT);
    Serial.println(F("MSG: Phase 2 (Slow Touch)"));
  }

  // ZABEZPIECZENIE 3: Timeout (jeśli Phase 0 lub 2 przejechały za dużo)
  if (t->currentSteps >= t->targetSteps &&
      (homingPhase == 0 || homingPhase == 2)) {
    Serial.println(F("ERROR: Homing timeout! Switch not found."));
    t->state = ERROR;
    t->isComplete = true;
    homingPhase = -1; // Stan błędu
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
  bool isHomingFinished = (t->state == HOMING && homingPhase >= 3);
  bool isNormalTaskFinished =
      (t->state != HOMING && t->currentSteps >= t->targetSteps);

  if (isHomingFinished || isNormalTaskFinished) {
    t->isComplete = true;
    dequeueTask();

    if (taskCount == 0)
      digitalWrite(EN, HIGH);

    if (t->state == HOMING) {
      Serial.println(F("MSG: Homing finished. Zero established."));
    } else {
      Serial.println(F("MSG: Task complete."));
    }
    printStatus();
  }
}