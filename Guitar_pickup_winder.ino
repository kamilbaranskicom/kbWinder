#include <Arduino.h>

// --- PINS ---
#define W_STEP 17
#define W_DIR 16
#define T_STEP 15
#define T_DIR 14
#define EN 12

// --- MECHANICAL SETTINGS (1600 steps/rev) ---
#define STEPS_PER_REV 1600
#define SCREW_PITCH 2.0 // 2mm per rev

// --- WINDING PARAMETERS ---
long wireDiaRaw = 0, coilWidth = 0, targetTurns = 0;
long currentStepsW = 0;
long totalTargetSteps = 0;
long stepsPerLayer = 0;
long currentLayerSteps = 0;
int layerDir = -1;
bool isWinding = false;

// --- SPEED & ACCELERATION ---
// Delay in microseconds between step toggles
long currentDelay = 400;
long minDelay = 60;    // Approx 600 RPM
long startDelay = 400; // Starting speed
long rampSteps = 8000; // Acceleration over 5 turns (5 * 1600)

void setup() {
  pinMode(EN, OUTPUT);
  pinMode(W_STEP, OUTPUT);
  pinMode(W_DIR, OUTPUT);
  pinMode(T_STEP, OUTPUT);
  pinMode(T_DIR, OUTPUT);
  digitalWrite(EN, HIGH);

  Serial.begin(9600);
  Serial.println("--- MANUAL SYNC MODE READY ---");
}

void stopMachine() {
  isWinding = false;
  digitalWrite(EN, HIGH);
  Serial.println("STOP");
}

void startMachine() {
  if (wireDiaRaw > 0 && coilWidth > 0 && targetTurns > 0) {
    totalTargetSteps = targetTurns * STEPS_PER_REV;
    // Calculate how many winder steps fit in one layer
    // (Width / (WireDia/10)) * 1600
    stepsPerLayer = (coilWidth * 16000L) / wireDiaRaw;

    currentStepsW = 0;
    currentLayerSteps = 0;
    currentDelay = startDelay;
    isWinding = true;
    digitalWrite(EN, LOW);
    digitalWrite(W_DIR, HIGH);
    digitalWrite(T_DIR, (layerDir == 1) ? HIGH : LOW);
  }
}

void loop() {
  // --- SERIAL HANDLING ---
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
        coilWidth = input.substring(s1 + 1, s2).toInt();
        targetTurns = input.substring(s2 + 1).toInt();
        startMachine();
      }
    }
  }

  // --- MAIN WINDING LOOP ---
  if (isWinding) {
    if (currentStepsW < totalTargetSteps) {

      // 1. STEP WINDER
      digitalWrite(W_STEP, HIGH);

      // 2. CALCULATE AND STEP TRAVERSE (Bresenham-like sync)
      // Traverse steps per 1 winder step = (wireDia / 10) / 2.0
      // To avoid floats, we use a scaling factor
      if ((currentStepsW * wireDiaRaw * 80) / 1600 >
          ((currentStepsW - 1) * wireDiaRaw * 80) / 1600) {
        digitalWrite(T_STEP, HIGH);
      }

      delayMicroseconds(currentDelay);
      digitalWrite(W_STEP, LOW);
      digitalWrite(T_STEP, LOW);
      delayMicroseconds(currentDelay);

      // 3. GRADUAL SOFT START (Smooth ramp per step)
      if (currentStepsW < rampSteps && currentDelay > minDelay) {
        if (currentStepsW % 20 == 0)
          currentDelay--;
      }

      currentStepsW++;
      currentLayerSteps++;

      // 4. LAYER FLIP
      if (currentLayerSteps >= stepsPerLayer) {
        layerDir *= -1;
        digitalWrite(T_DIR, (layerDir == 1) ? HIGH : LOW);
        currentLayerSteps = 0;
        Serial.println("L_FLIP");
      }

      // 5. UPDATE PROGRESS (Every 10 turns to save CPU)
      if (currentStepsW % 16000 == 0) {
        Serial.print("T: ");
        Serial.println(currentStepsW / 1600);
      }

    } else {
      stopMachine();
    }
  }
}