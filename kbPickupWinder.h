/* * GUITAR PICKUP WINDER V3.0
 * Features: State Machine, Key-Value SET/GET, CSV Export/Import, EEPROM
 * Management
 */

#include <Arduino.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

// --- HARDWARE PINS ---
#define W_STEP 17
#define W_DIR 16
#define T_STEP 15
#define T_DIR 14
#define EN 12
#define LIMIT_PIN 4

enum MachineState { IDLE,
                    RUNNING,
                    PAUSED,
                    HOMING,
                    MOVING,
                    ERROR };

// --- GLOBAL STATE ---

bool isPauseRequested = false;

float stepsPerMM;
long absPos = 0;  // Traverse steps from 0
bool isHomed = false;
int homingPhase = 0;  // 0: searching switch, 1: backing off

unsigned long lastStepMicros = 0;
float traverseAccumulator = 0;
long currentLayerSteps = 0;
int layerDir = 1;

// to było:
long currentStepsW = 0;  // Winder progress in steps
long stepsPerLayer = 0;

const float backoffDistanceMM = 1;