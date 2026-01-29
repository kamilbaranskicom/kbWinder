#ifndef PRESETS_H
#define PRESETS_H

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