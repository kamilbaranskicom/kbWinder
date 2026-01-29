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

enum MachineState { IDLE, RUNNING, PAUSED, HOMING, MOVING, ERROR };
MachineState state = IDLE;

// --- STRUCTURES ---

struct MachineConfig {
  float screwPitch;
  int stepsPerRevW;
  int stepsPerRevT;
  int maxRPM_W;
  int maxRPM_T;
  bool dirW;
  bool dirT;
  bool useLimitSwitch;
  bool homeBeforeStart;
};
