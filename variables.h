#ifndef VARIABLES_H
#define VARIABLES_H

// --- STRUCTURES & ENUMS ---

enum VarType { T_INT,
               T_FLOAT,
               T_LONG,
               T_BOOL,
               T_CHAR };
               
enum VarCategory { C_MACHINE,
                   C_PRESET,
                   C_RUNTIME };  // Distinction for EEPROM logic

struct VarMap {
  const char *label;
  void *ptr;
  VarType type;
  VarCategory category;
  uint8_t maxLen;
};

// --- VARIABLE TABLE ---
// Categorizing variables to handle auto-save for machine config
VarMap varTable[] = { { "SCREW PITCH", &cfg.screwPitch, T_FLOAT, C_MACHINE, 0 },
                      { "WINDER STEPS PER REV", &cfg.stepsPerRevW, T_INT, C_MACHINE, 0 },
                      { "TRAVERSE STEPS PER REV", &cfg.stepsPerRevT, T_INT, C_MACHINE, 0 },
                      { "WINDER MAX SPEED", &cfg.maxRPM_W, T_INT, C_MACHINE, 0 },
                      { "TRAVERSE MAX SPEED", &cfg.maxRPM_T, T_INT, C_MACHINE, 0 },
                      { "WINDER START SPEED", &cfg.startRPM_W, T_INT, C_MACHINE, 0 },
                      { "TRAVERSE START SPEED", &cfg.startRPM_T, T_INT, C_MACHINE, 0 },
                      { "WINDER DEFAULT RAMP", &cfg.defaultRamp_W, T_INT, C_MACHINE, 0 },
                      { "TRAVERSE DEFAULT RAMP", &cfg.defaultRamp_T, T_INT, C_MACHINE, 0 },          
                      { "WINDER DIRECTION", &cfg.dirW, T_BOOL, C_MACHINE, 0 },
                      { "TRAVERSE DIRECTION", &cfg.dirT, T_BOOL, C_MACHINE, 0 },
                      { "LIMIT SWITCH", &cfg.useLimitSwitch, T_BOOL, C_MACHINE, 0 },
                      { "HOME BEFORE START", &cfg.homeBeforeStart, T_BOOL, C_MACHINE, 0 },
                      { "USE START OFFSET", &cfg.useStartOffset, T_BOOL, C_MACHINE, 0 },
                      { "BACKOFF DISTANCE", &cfg.backoffDistanceMM, T_FLOAT, C_MACHINE, 0 },

                      { "NAME", active.name, T_CHAR, C_PRESET, 15 },
                      { "WIRE", &active.wireDia, T_FLOAT, C_PRESET, 0 },
                      { "COIL LENGTH", &active.coilWidth, T_FLOAT, C_PRESET, 0 },
                      { "TURNS", &active.totalTurns, T_LONG, C_PRESET, 0 },
                      { "TARGET RPM", &active.targetRPM, T_INT, C_PRESET, 0 },
                      { "RAMP", &active.rampRPM, T_INT, C_PRESET, 0 },
                      { "START OFFSET", &active.startOffset, T_FLOAT, C_PRESET, 0 },

                      { "POSITION", &absPos, T_LONG, C_RUNTIME, 0 },
                      { "OS VERSION", &version, T_CHAR, C_RUNTIME, 0 },
                      { "IS PAUSE REQUESTED", &isPauseRequested, T_BOOL, C_RUNTIME, 0 },

                      { "STEPS PER MM", &stepsPerMM, T_FLOAT, C_RUNTIME, 0 },
                      { "IS HOMED", &isHomed, T_BOOL, C_RUNTIME, 0 },
                      { "HOMING PHASE", &homingPhase, T_INT, C_RUNTIME, 0 },

                      { "LAST STEP MICROS", &lastStepMicros, T_LONG, C_RUNTIME, 0 },
                      { "TRAVERSE ACCUMULATOR", &traverseAccumulator, T_FLOAT, C_RUNTIME, 0 },
                      { "CURRENT LAYER STEPS", &currentLayerSteps, T_LONG, C_RUNTIME, 0 },
                      { "LAYER DIRECTION", &layerDir, T_INT, C_RUNTIME, 0 },
                      { "BACKOFF DISTANCE MM", &backoffDistanceMM, T_FLOAT, C_RUNTIME, 0 } };


const int varCount = sizeof(varTable) / sizeof(VarMap);

void handleSet(String line);
void handleGet(String line);
bool parseBool(String val, String label);

#endif  // VARIABLES_H