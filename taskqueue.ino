#include "taskqueue.h"

// --- QUEUE HELPERS ---

void debugEnqueueTask(MachineState s, char m, long target, bool isRelative,
                      int rpm, int ramp, bool isJogMove) {
  Serial.print("Enqueue task: (");
  Serial.print(s);
  Serial.print(", ");
  Serial.print(m);
  Serial.print(", ");
  Serial.print(target);
  Serial.print(", ");
  Serial.print(isRelative);
  Serial.print(", ");
  Serial.print(rpm);
  Serial.print(", ");
  Serial.print(ramp);
  Serial.print(", ");
  Serial.print(isJogMove);
  Serial.println(")");
}

bool printTaskDebug(Task *t) {
  if (t == NULL) {
    Serial.println(F("DEBUG: No active task to print."));
    return;
  }

  Serial.println(F("--- TASK DUMP START ---"));
  
  // Stany (enumy rzutujemy na int)
  Serial.print(F("STATE: ")); Serial.println((int)t->state);
  Serial.print(F("PREV_STATE: ")); Serial.println((int)t->prevState);
  
  // Identyfikacja i tryb
  Serial.print(F("MOTOR: ")); Serial.println(t->motor);
  Serial.print(F("RELATIVE: ")); Serial.println(t->isRelative);
  
  // Pozycje i kroki
  Serial.print(F("TARGET_POS: ")); Serial.println(t->targetPosition);
  Serial.print(F("TARGET_STEPS: ")); Serial.println(t->targetSteps);
  Serial.print(F("DIR: ")); Serial.println(t->dir);
  Serial.print(F("CUR_STEPS: ")); Serial.println(t->currentSteps);
  Serial.print(F("ACCEL_DIST: ")); Serial.println(t->accelDistance);
  
  // Prędkości i Rampa (floaty)
  Serial.print(F("START_RPM: ")); Serial.println(t->startRPM);
  Serial.print(F("TARGET_RPM: ")); Serial.println(t->targetRPM);
  Serial.print(F("CUR_RPM: ")); Serial.println(t->currentRPM);
  Serial.print(F("ACCEL_RATE: ")); Serial.println(t->accelRate);
  
  // Timingi i Cache
  Serial.print(F("CACHED_DELAY: ")); Serial.println(t->cachedDelay);
  Serial.print(F("LAST_RAMP_UP: ")); Serial.println(t->lastRampUpdate);
  
  // Flagi stanu
  Serial.print(F("IS_STARTED: ")); Serial.println(t->isStarted);
  Serial.print(F("IS_DECEL: ")); Serial.println(t->isDecelerating);
  Serial.print(F("IS_COMPLETE: ")); Serial.println(t->isComplete);
  Serial.print(F("IS_JOG: ")); Serial.println(t->isJogMove);
  
  // Watchdog i start
  Serial.print(F("TASK_STARTED: ")); Serial.println(t->taskStarted);
  Serial.print(F("LAST_PING: ")); Serial.println(t->taskLastPinged);
  
  Serial.println(F("--- TASK DUMP END ---"));
}

bool enqueueTask(MachineState s, char m, long target, bool isRelative, int rpm,
                 int ramp, bool isJogMove) {

  debugEnqueueTask(s, m, target, isRelative, rpm, ramp, isJogMove);

  if (taskCount >= QUEUE_SIZE) {
    Serial.println(F("ERROR: Task Queue Full!"));
    return false;
  }

  Task &t = taskQueue[tail];
  t.state = s;
  t.motor = m;
  t.isRelative = isRelative;
  if (isRelative) {
    t.dir = (target >= 0) ? 1 : -1;
    t.targetSteps = abs(target);
    t.targetPosition = NAN; // will be set when starting the task
  } else {
    t.targetPosition = target;
    t.targetSteps = NAN; // will be set when starting the task
    t.dir = 0;           // will be set when starting the task
  }
  t.currentSteps = 0;
  t.accelDistance = 0;

  int absoluteMax = (m == 'W' || m == 'S') ? cfg.maxRPM_W : cfg.maxRPM_T;
  if (rpm > absoluteMax) {
    Serial.println("WARNING: Requested RPM exceeds maximum. Limiting to max.");
    rpm = absoluteMax;
  };

  t.startRPM = (t.motor == 'W')
                   ? cfg.startRPM_W
                   : cfg.startRPM_T; // or should we ask active preset for this,
                                     // as wire diameter may affect startRPM?
  t.targetRPM = (float)rpm;

  if (t.startRPM > t.targetRPM) {
    t.startRPM = t.targetRPM / 2; // safe startRPM when target is low
  }

  t.currentRPM = t.startRPM;
  t.accelRate = ramp;
  t.isStarted = false;
  t.isDecelerating = false;
  t.isComplete = false;

  t.isJogMove = isJogMove;
  // t.taskRequested = millis();
  t.taskStarted = 0;
  t.taskLastPinged = 0;

  tail = (tail + 1) % QUEUE_SIZE;
  taskCount++;
  return true;
}

Task *getCurrentTask() {
  if (taskCount > 0)
    return &taskQueue[head];
  return NULL;
}

String getTaskStateStr(MachineState state) {
  switch (state) {
  case HOMING:
    return F("HOMING");
  case MOVING:
    return F("MOVING");
  case RUNNING:
    return F("WINDING");
  case IDLE:
    return F("IDLE");
  case PAUSED:
    return F("PAUSE");
  case ERROR:
    return F("ERROR");
  }
  return "";
}

void dequeueTask() {
  if (taskCount > 0) {
    head = (head + 1) % QUEUE_SIZE;
    taskCount--;
  }
}

void clearQueue() {
  head = 0;
  tail = 0;
  taskCount = 0;
}