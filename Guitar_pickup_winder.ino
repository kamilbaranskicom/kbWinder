#include "BasicStepperDriver.h"
#include "SyncDriver.h"
#include <Arduino.h>
#include <SoftwareSerial.h>

// --- HARDWARE CONFIGURATION ---
#define WINDER_STEP 17
#define WINDER_DIR 16
#define TRAVERSE_STEP 15
#define TRAVERSE_DIR 14
#define ENABLE_PIN 12

SoftwareSerial mySerial(2, 3);

// --- PARAMETERS ---
int wireDiameterRaw = 0; // from x1 (e.g., 1 = 0.1mm)
int coilWidthMM = 0;     // from n1
int targetTurns = 0;     // from n2

int currentTurns = 0;
int turnsInCurrentLayer = 0;
int turnsPerLayer = 0;
int layerDirection = -1; // -1 starts "away" from home based on your test
bool isWinding = false;

// --- STEPPER CONFIGURATION (Updated from your calibration) ---
#define MOTOR_STEPS 200
#define WINDER_MICROSTEPS 64  // Found: 12800 steps/rev
#define TRAVERSE_MICROSTEPS 8 // Found: 1600 steps/rev

// Speeds adjusted for Nano processing limits and mechanical weight
#define MAX_RPM 400 // 12.8k microsteps is heavy for Nano, starting safe
#define START_RPM 60

BasicStepperDriver winder(MOTOR_STEPS, WINDER_DIR, WINDER_STEP);
BasicStepperDriver traverse(MOTOR_STEPS, TRAVERSE_DIR, TRAVERSE_STEP);
SyncDriver controller(winder, traverse);

String message = "";
int endBytes = 0;
int inputCounter = 0;

void setup() {
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, HIGH);

  Serial.begin(9600);
  mySerial.begin(9600);

  // Initialize with specific microsteps found during test
  winder.begin(START_RPM, WINDER_MICROSTEPS);
  traverse.begin(START_RPM, TRAVERSE_MICROSTEPS);

  Serial.println(">>> Calibrated System Ready <<<");
}

void startMachine() {
  if (targetTurns > 0 && wireDiameterRaw > 0) {
    currentTurns = 0;
    turnsInCurrentLayer = 0;
    isWinding = true;
    digitalWrite(ENABLE_PIN, LOW);
    winder.begin(START_RPM, WINDER_MICROSTEPS);
    Serial.println("WINDING START");
  }
}

void stopMachine() {
  isWinding = false;
  digitalWrite(ENABLE_PIN, HIGH);
  Serial.println("WINDING STOP");
  inputCounter = 0;
}

void assignParam(int val) {
  if (inputCounter == 0) {
    wireDiameterRaw = val;
    inputCounter++;
  } else if (inputCounter == 1) {
    coilWidthMM = val;
    inputCounter++;
  } else if (inputCounter == 2) {
    targetTurns = val;
    if (wireDiameterRaw > 0) {
      turnsPerLayer = (coilWidthMM * 10) / wireDiameterRaw;
      Serial.print("Turns Per Layer: ");
      Serial.println(turnsPerLayer);
    }
    inputCounter = 0;
    startMachine();
  }
}

void handleHMI() {
  if (mySerial.available()) {
    byte inByte = mySerial.read();
    if ((inByte >= '0' && inByte <= '9') || (inByte >= 'A' && inByte <= 'Z')) {
      message += (char)inByte;
    } else if (inByte == 255) {
      endBytes++;
    }

    if (endBytes == 3) {
      message.trim();
      if (message == "START")
        startMachine();
      else if (message == "STOP")
        stopMachine();
      else if (message.length() > 0)
        assignParam(message.toInt());
      message = "";
      endBytes = 0;
    }
  }
}

void loop() {
  handleHMI();

  if (isWinding) {
    if (currentTurns < targetTurns) {
      // Soft Start over first 30 turns
      if (currentTurns <= 30) {
        float currentRPM = map(currentTurns, 0, 30, START_RPM, MAX_RPM);
        winder.begin(currentRPM, WINDER_MICROSTEPS);
        traverse.begin(currentRPM, TRAVERSE_MICROSTEPS);
      }

      // 2mm pitch screw = 180 degrees/mm.
      // Library handles the 1600 steps conversion via TRAVERSE_MICROSTEPS
      float degreesToMove = (float)wireDiameterRaw * 18.0;

      // Sync movement: Winder does 360 deg, Traverse moves calculated distance
      controller.rotate((double)360, (double)(layerDirection * degreesToMove));

      currentTurns++;
      turnsInCurrentLayer++;

      if (turnsInCurrentLayer >= turnsPerLayer) {
        layerDirection *= -1;
        turnsInCurrentLayer = 0;
        Serial.println("Direction Change");
      }

      // Update Nextion
      mySerial.print("n2.val=");
      mySerial.print(currentTurns);
      mySerial.write(0xff);
      mySerial.write(0xff);
      mySerial.write(0xff);
    } else {
      stopMachine();
    }
  }
}