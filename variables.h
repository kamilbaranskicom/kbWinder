#ifndef VARIABLES_H
#define VARIABLES_H

// --- STRUCTURES & ENUMS ---

enum VarType { T_INT, T_FLOAT, T_LONG, T_BOOL, T_CHAR };
enum VarCategory { C_MACHINE, C_PRESET }; // Distinction for EEPROM logic

struct VarMap {
  const char *label;
  void *ptr;
  VarType type;
  VarCategory category;
  uint8_t maxLen;
};

// --- VARIABLE TABLE ---
// Categorizing variables to handle auto-save for machine config
VarMap varTable[] = {{"SCREW PITCH", &cfg.screwPitch, T_FLOAT, C_MACHINE, 0},
                     {"WINDER SPEED", &cfg.maxRPM_W, T_INT, C_MACHINE, 0},
                     {"TRAVERSE SPEED", &cfg.maxRPM_T, T_INT, C_MACHINE, 0},
                     {"WINDER DIRECTION", &cfg.dirW, T_BOOL, C_MACHINE, 0},
                     {"TRAVERSE DIRECTION", &cfg.dirT, T_BOOL, C_MACHINE, 0},
                     {"LIMIT SWITCH", &cfg.useLimitSwitch, T_BOOL, C_MACHINE, 0},

                     {"NAME", active.name, T_CHAR, C_PRESET, 15},
                     {"WIRE", &active.wireDia, T_FLOAT, C_PRESET, 0},
                     {"COIL LENGTH", &active.coilWidth, T_FLOAT, C_PRESET, 0},
                     {"TURNS", &active.totalTurns, T_LONG, C_PRESET, 0},
                     {"TARGET RPM", &active.targetRPM, T_INT, C_PRESET, 0},
                     {"RAMP", &active.rampRPM, T_INT, C_PRESET, 0},
                     {"START OFFSET", &active.startOffset, T_LONG, C_PRESET, 0}};
                     
const int varCount = sizeof(varTable) / sizeof(VarMap);

void handleSet(String line);
void handleGet(String line);
bool parseBool(String val, String label);

#endif // VARIABLES_H