#include "BasicStepperDriver.h"
#include "SyncDriver.h"
#include <Arduino.h>
#include <SoftwareSerial.h>


// --- HARDWARE PINS ---
#define WINDER_STEP 17
#define WINDER_DIR 16
#define TRAVERSE_STEP 15
#define TRAVERSE_DIR 14
#define ENABLE_PIN 12

SoftwareSerial mySerial(2, 3); // RX, TX

// --- SETTINGS ---
#define MOTOR_STEPS 200
#define MICROSTEPS 8  // Both set to 1/8 (1600 steps/rev)
#define MAX_RPM 400   // Target speed
#define START_RPM 100 // Minimum starting speed

// --- VARIABLES ---
int wireDiameterRaw = 0; // 1 = 0.1mm
int coilWidthMM = 0;
int targetTurns = 0;

int currentTurns = 0;
int turnsInCurrentLayer = 0;
int turnsPerLayer = 0;
int layerDirection = -1; // Start direction
bool isWinding = false;

BasicStepperDriver winder(MOTOR_STEPS, WINDER_DIR, WINDER_STEP);
BasicStepperDriver traverse(MOTOR_STEPS, TRAVERSE_DIR, TRAVERSE_STEP);
SyncDriver controller(winder, traverse);

String message = "";
int endBytes = 0;
int inputCounter = 0;

void setup() {
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, HIGH); // Motors OFF at start

  Serial.begin(9600);
  mySerial.begin(9600);

  winder.begin(START_RPM, MICROSTEPS);
  traverse.begin(START_RPM, MICROSTEPS);

  Serial.println(">>> System Ready - 1600 steps/rev mode <<<");
}

void startMachine() {
  if (targetTurns > 0 && wireDiameterRaw > 0) {
    currentTurns = 0;
    turnsInCurrentLayer = 0;
    isWinding = true;
    digitalWrite(ENABLE_PIN, LOW);
    winder.begin(START_RPM, MICROSTEPS);
    Serial.println("WINDING STARTED");
  }
}

void stopMachine() {
  isWinding = false;
  digitalWrite(ENABLE_PIN, HIGH);
  Serial.println("WINDING STOPPED");
  inputCounter = 0;
  message = "";
}

void assignParam(int val) {
  // Only accept parameters when the machine is NOT working
  // to avoid loopback feedback from display updates
  if (isWinding)
    return;

  if (inputCounter == 0) {
    wireDiameterRaw = val;
    Serial.print("Wire: ");
    Serial.println(wireDiameterRaw);
    inputCounter++;
  } else if (inputCounter == 1) {
    coilWidthMM = val;
    Serial.print("Width: ");
    Serial.println(coilWidthMM);
    inputCounter++;
  } else if (inputCounter == 2) {
    targetTurns = val;
    Serial.print("Total Turns: ");
    Serial.println(targetTurns);
    if (wireDiameterRaw > 0) {
      turnsPerLayer = (coilWidthMM * 10) / wireDiameterRaw;
    }
    inputCounter = 0;
    // Wait for explicit START command or auto-start if you prefer
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
      if (message.indexOf("START") >= 0)
        startMachine();
      else if (message.indexOf("STOP") >= 0)
        stopMachine();
      else if (message.length() > 0) {
        // Strip the 'p' header if Nextion sends it
        if (message.startsWith("p"))
          message = message.substring(1);
        assignParam(message.toInt());
      }
      message = "";
      endBytes = 0;
    }
  }
}

void loop() {
  handleHMI();

  if (isWinding) {
    if (currentTurns < targetTurns) {
      // Soft Start: Gradual acceleration over 50 turns
      if (currentTurns <= 50) {
        float currentRPM = map(currentTurns, 0, 50, START_RPM, MAX_RPM);
        winder.begin(currentRPM, MICROSTEPS);
        traverse.begin(currentRPM, MICROSTEPS);
      }

      // Calculate movement for this single turn
      float degreesToMove = (float)wireDiameterRaw * 18.0;

      // Sync move: Winder 360, Traverse moves D*180 degrees
      controller.rotate((double)360, (double)(layerDirection * degreesToMove));

      currentTurns++;
      turnsInCurrentLayer++;

      // Check for layer flip
      if (turnsInCurrentLayer >= turnsPerLayer) {
        layerDirection *= -1;
        turnsInCurrentLayer = 0;
        Serial.println("Layer Change");
      }

      // Optimization: Update Nextion display every 10 turns
      // This prevents SoftwareSerial from stalling the stepper timing
      if (currentTurns % 10 == 0 || currentTurns == targetTurns) {
        mySerial.print("n2.val=");
        mySerial.print(currentTurns);
        mySerial.write(0xff);
        mySerial.write(0xff);
        mySerial.write(0xff);
      }
    } else {
      stopMachine();
    }
  }
}