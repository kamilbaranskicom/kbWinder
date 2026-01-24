#include <Arduino.h>
#include <SoftwareSerial.h>

// --- PINS ---
#define W_STEP 17
#define W_DIR 16
#define T_STEP 15
#define T_DIR 14
#define EN 12

SoftwareSerial nextionSerial(2, 3);

// --- MECHANICAL SETTINGS (1600 steps/rev) ---
#define STEPS_PER_REV 1600
#define SCREW_PITCH 2.0

// --- WINDING PARAMETERS ---
long wireDiaRaw = 0;
long coilWidthMM = 0;
long targetTurns = 0;
long currentStepsW = 0;
long totalTargetSteps = 0;
long stepsPerLayer = 0;
long currentLayerSteps = 0;
long traverseAccumulator = 0;
int layerDir = -1;
bool isWinding = false;

// --- SPEED & ACCELERATION ---
long currentDelay = 400;
long minDelay = 70; // Lower value = Much higher speed, 70 seems to be the limit
long startDelay = 400;
long rampInterval = 90; // Decrease delay every 20 steps for ultra-smooth ramp

void setup() {
  pinMode(EN, OUTPUT);
  pinMode(W_STEP, OUTPUT);
  pinMode(W_DIR, OUTPUT);
  pinMode(T_STEP, OUTPUT);
  pinMode(T_DIR, OUTPUT);
  digitalWrite(EN, HIGH);

  // High speed for PC upload and monitoring
  Serial.begin(57600);
  nextionSerial.begin(9600);

  Serial.println("--- MANUAL SYNC MODE READY ---");
  Serial.println(
      "try: \"1 10 1000\" for 0.1 mm wire / 10 mm width / 1000 turns.");
  Serial.println("Empty line to stop.");
}

void stopMachine() {
  isWinding = false;
  digitalWrite(EN, HIGH);
  Serial.println(">>> STOPPED");
}

void updateHMI(long turns) {
  // Update Nextion
  nextionSerial.print("n2.val=");
  nextionSerial.print(turns);
  nextionSerial.write(0xff);
  nextionSerial.write(0xff);
  nextionSerial.write(0xff);

  // Update PC
  Serial.print("Turn: ");
  Serial.print(turns);
  Serial.println(" / delay = " + (String)currentDelay);
}

void startMachine() {
  if (wireDiaRaw > 0 && coilWidthMM > 0 && targetTurns > 0) {
    totalTargetSteps = targetTurns * STEPS_PER_REV;
    stepsPerLayer = (coilWidthMM * 10 * STEPS_PER_REV) / wireDiaRaw;

    currentStepsW = 0;
    currentLayerSteps = 0;
    traverseAccumulator = 0;
    currentDelay = startDelay;
    isWinding = true;

    digitalWrite(EN, LOW);
    digitalWrite(W_DIR, HIGH); // Set default winding direction
    digitalWrite(T_DIR, (layerDir == 1) ? HIGH : LOW);
    Serial.println(">>> WINDING STARTED");
  }
}

void handleInput() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0)
      stopMachine();
    else {
      int s1 = input.indexOf(' ');
      int s2 = input.lastIndexOf(' ');
      if (s1 != -1 && s2 != -1) {
        wireDiaRaw = input.substring(0, s1).toInt();
        coilWidthMM = input.substring(s1 + 1, s2).toInt();
        targetTurns = input.substring(s2 + 1).toInt();
        startMachine();
      }
    }
  }
}

void loop() {
  handleInput();

  if (isWinding) {
    if (currentStepsW < totalTargetSteps) {

      // 1. STEP BOTH MOTORS
      digitalWrite(W_STEP, HIGH);

      // Calculate if Traverse needs to step (Bresenham sync)
      // Traverse moves (wireDiaRaw * 80) steps per 1600 winder steps
      traverseAccumulator += (wireDiaRaw * 80);
      if (traverseAccumulator >= STEPS_PER_REV) {
        digitalWrite(T_STEP, HIGH);
        traverseAccumulator -= STEPS_PER_REV;
      }

      // Shared delay determines the speed of both motors
      delayMicroseconds(currentDelay);
      digitalWrite(W_STEP, LOW);
      digitalWrite(T_STEP, LOW);
      delayMicroseconds(currentDelay);

      // 2. TRUE SOFT START (Continuous per-step acceleration)
      if (currentDelay > minDelay) {
        if (currentStepsW % rampInterval == 0)
          currentDelay--;
      }

      currentStepsW++;
      currentLayerSteps++;

      // 3. DIRECTION FLIP
      if (currentLayerSteps >= stepsPerLayer) {
        layerDir *= -1;
        digitalWrite(T_DIR, (layerDir == 1) ? HIGH : LOW);
        currentLayerSteps = 0;
        Serial.println("--- LAYER CHANGE ---");
      }

      // 4. PER-TURN FEEDBACK
      if (currentStepsW % STEPS_PER_REV == 0) {
        updateHMI(currentStepsW / STEPS_PER_REV);
      }

    } else {
      stopMachine();
    }
  }
}