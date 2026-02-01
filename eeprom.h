#ifndef EEPROM_H
#define EEPROM_H

// --- STRUCTURES ---

struct MachineConfig {
  float screwPitch;
  int stepsPerRevW;
  int stepsPerRevT;
  int maxRPM_W;
  int maxRPM_T;
  int startRPM_T;
  int startRPM_W;
  bool dirW;
  bool dirT;
  bool useLimitSwitch;
  bool homeBeforeStart;
};

MachineConfig cfg;

// --- SYSTEM CONSTANTS ---

const int MAX_PRESETS = 25;
const int EEPROM_CONF_ADDR = 0;
const int EEPROM_PRESET_START = 50; // Start address for presets

struct WindingPreset {
  char name[16];
  float wireDia;
  float coilWidth;
  long totalTurns;
  int targetRPM;
  int rampRPM;
  long startOffset;
};

WindingPreset active; // Currently loaded/edited parameters

#endif