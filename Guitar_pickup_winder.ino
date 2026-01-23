#include <Arduino.h>
#include "BasicStepperDriver.h"
#include "SyncDriver.h"
#include <SoftwareSerial.h>

// --- KONFIGURACJA PINÓW ---
#define WINDER_STEP 17
#define WINDER_DIR 16
#define TRAVERSE_STEP 15
#define TRAVERSE_DIR 14
#define ENABLE_PIN 12

SoftwareSerial mySerial(2, 3);

// --- PARAMETRY ---
int wireDiameterRaw = 0;  // np. 1 dla 0.1mm
int coilWidthMM = 0;
int targetTurns = 0;

int currentTurns = 0;
int turnsInCurrentLayer = 0;
int turnsPerLayer = 0;
int layerDirection = -1; // JEŚLI UDERZA W BOK NA STARCIĘ -> ZMIEŃ kierunek= 1 albo -1
bool isWinding = false;

#define MICROSTEPS 16
#define MOTOR_STEPS 200
#define MAX_RPM 400
#define START_RPM 50

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

  winder.begin(START_RPM, MICROSTEPS);
  traverse.begin(START_RPM, MICROSTEPS);

  Serial.println("System Ready - Waiting for data...");
}

void stopMachine() {
  isWinding = false;
  digitalWrite(ENABLE_PIN, HIGH);
  Serial.println("STOP/Finished.");
}

void startMachine() {
  if (targetTurns > 0 && wireDiameterRaw > 0) {
    currentTurns = 0;
    turnsInCurrentLayer = 0;
    isWinding = true;
    digitalWrite(ENABLE_PIN, LOW);
    winder.begin(START_RPM, MICROSTEPS);
    Serial.println("WINDING STARTED!");
  }
}

void assignParam(int val) {
  if (inputCounter == 0) {
    wireDiameterRaw = val;
    Serial.print("Wire Raw: ");
    Serial.println(wireDiameterRaw);
    inputCounter++;
  } else if (inputCounter == 1) {
    coilWidthMM = val;
    Serial.print("Width (mm): ");
    Serial.println(coilWidthMM);
    inputCounter++;
  } else if (inputCounter == 2) {
    targetTurns = val;
    Serial.print("Turns: ");
    Serial.println(targetTurns);

    if (wireDiameterRaw > 0) {
      turnsPerLayer = (coilWidthMM * 10) / wireDiameterRaw;
      Serial.print("Turns Per Layer: ");
      Serial.println(turnsPerLayer);
    }
    inputCounter = 0;
    startMachine();  // Automatyczny start po 3-cim parametrze
  }
}

void handleHMI() {
  if (mySerial.available()) {
    byte inByte = mySerial.read();

    // DEBUG: Printing raw data to Serial Monitor
    Serial.print("Recv: '");
    Serial.print((char)inByte);  // Shows as character (e.g., '1')
    Serial.print("' | Hex: ");
    Serial.println(inByte, HEX);  // Shows as HEX value (e.g., 'FF' for 255)

    // Akceptujemy cyfry ORAZ litery (dla komend START/STOP/p)
    if ((inByte >= '0' && inByte <= '9') || (inByte >= 'A' && inByte <= 'Z') || (inByte == 'p')) {
      message += (char)inByte;
    } else if (inByte == 255) {
      endBytes++;
    }

    if (endBytes == 3) {
      message.trim();
      Serial.print("Nextion Command: ");
      Serial.println(message);

      if (message.indexOf("START") >= 0) {
        startMachine();
      } else if (message.indexOf("STOP") >= 0) {
        stopMachine();
      } else {
        // Jeśli w wiadomości jest 'p' na początku, usuwamy go przed konwersją na int
        if (message.startsWith("p")) {
          message = message.substring(1);
        }
        int val = message.toInt();
        if (val > 0) assignParam(val);
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
      // Soft Start logic
      if (currentTurns % 5 == 0 && currentTurns < 200) {
        float currentRPM = map(currentTurns, 0, 200, START_RPM, MAX_RPM);
        winder.begin(currentRPM, MICROSTEPS);
        traverse.begin(currentRPM, MICROSTEPS);
      }

      // 2mm pitch -> 180 deg per 1mm. Wire raw is 0.1 units.
      float degreesToMove = (float)wireDiameterRaw * 18.0;

      controller.rotate((double)360, (double)(layerDirection * degreesToMove));

      currentTurns++;
      turnsInCurrentLayer++;

      if (turnsInCurrentLayer >= turnsPerLayer) {
        layerDirection *= -1;
        turnsInCurrentLayer = 0;
        Serial.println("Layer Switch");
      }

      // Update HMI
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