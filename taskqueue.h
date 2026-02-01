#ifndef TASKQUEUE_H
#define TASKQUEUE_H

struct Task {
  MachineState state;
  MachineState prevState; // needed for pausing/resuming
  char motor;  // 'W', 'T' lub 'S' (Synchronized)
  bool isRelative;
  long targetPosition;  // absolute
  long targetSteps;     // relative
  int dir;              // direction (-1 backward | 0 not set | 1 forward)
  long currentSteps;    // steps counter
  long accelDistance;   // how many steps it took to accelerate
  float startRPM;
  float targetRPM;
  float currentRPM;
  float accelRate;  // RPM zwiększane na krok Mastera
  bool isStarted;
  bool isDecelerating;
  bool isComplete;
};

#define QUEUE_SIZE 6
Task taskQueue[QUEUE_SIZE];
int head = 0;  // Index of the current task
int tail = 0;  // Index where next task will be added
int taskCount = 0;

bool enqueueTask(MachineState s, char m, long target, bool isRelative, int rpm,
                 float ramp);
Task *getCurrentTask();
void dequeueTask();
void clearQueue();


#endif