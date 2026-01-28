#ifndef VARIABLES_H
#define VARIABLES_H

enum VarType { T_INT, T_FLOAT, T_LONG, T_BOOL };

struct VarMap {
  const char* label;
  void* ptr;
  VarType type;
};

// Map labels to variables for universal SET/GET
VarMap varTable[] = {
  {"SCREW PITCH", &cfg.screwPitch, T_FLOAT},
  {"WINDER SPEED", &cfg.maxRPM_W, T_INT},
  {"TRAVERSE SPEED", &cfg.maxRPM_T, T_INT},
  {"WIRE", &active.wireDia, T_FLOAT},
  {"COIL LENGTH", &active.coilWidth, T_FLOAT},
  {"TURNS", &active.totalTurns, T_LONG},
  {"TARGET RPM", &active.targetRPM, T_INT},
  {"RAMP", &active.rampRPM, T_INT},
  {"START OFFSET", &active.startOffset, T_LONG}
};
const int varCount = sizeof(varTable) / sizeof(VarMap);

#endif