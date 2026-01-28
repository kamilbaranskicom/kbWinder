#ifndef PRESETS_H
#define PRESETS_H

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