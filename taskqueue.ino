#include "taskqueue.h"

// --- QUEUE HELPERS ---

void debugEnqueueTask(MachineState s, char m, long target, bool isRelative,
                      int rpm, float ramp) {
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
  Serial.println(")");
}

bool enqueueTask(MachineState s, char m, long target, bool isRelative, int rpm,
                 float ramp) {

  debugEnqueueTask(s, m, target, isRelative, rpm, ramp);

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
  t.startRPM = (t.motor == 'W')
                   ? cfg.startRPM_W
                   : cfg.startRPM_T; // or should we ask active preset for this,
                                     // as wire diameter may affect startRPM?
  t.targetRPM = (float)rpm;
  t.currentRPM = t.startRPM;
  t.accelRate = ramp;
  t.isStarted = false;
  t.isDecelerating = false;
  t.isComplete = false;

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