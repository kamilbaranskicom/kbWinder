#include "variables.h"

// --- COMMAND HANDLERS ---

void handleSet(String line) {
  line.remove(0, 4); // Remove "SET "
  for (int i = 0; i < varCount; i++) {
    if (line.startsWith(varTable[i].label)) {
      String valStr = line.substring(strlen(varTable[i].label));
      valStr.trim();

      if (varTable[i].type == T_FLOAT)
        *(float *)varTable[i].ptr = valStr.toFloat();
      else if (varTable[i].type == T_INT)
        *(int *)varTable[i].ptr = valStr.toInt();
      else if (varTable[i].type == T_LONG)
        *(long *)varTable[i].ptr = valStr.toInt();
      else if (varTable[i].type == T_BOOL)
        *(bool *)varTable[i].ptr = (valStr == "ON" || valStr == "1");

      Serial.print(F("CONFIRMED: "));
      Serial.print(varTable[i].label);
      Serial.print(F(" set to "));
      Serial.println(valStr);
      return;
    }
  }
  Serial.println(F("ERROR: Unknown parameter"));
}

void handleGet(String line) {
  line.remove(0, 4); // Remove "GET "
  line.trim();

  for (int i = 0; i < varCount; i++) {
    if (line.length() == 0 || line == varTable[i].label) {
      Serial.print(varTable[i].label);
      Serial.print(F(": "));
      if (varTable[i].type == T_FLOAT)
        Serial.println(*(float *)varTable[i].ptr, 3);
      else if (varTable[i].type == T_INT)
        Serial.println(*(int *)varTable[i].ptr);
      else if (varTable[i].type == T_LONG)
        Serial.println(*(long *)varTable[i].ptr);
      else if (varTable[i].type == T_BOOL)
        Serial.println(*(bool *)varTable[i].ptr ? "ON" : "OFF");
    }
  }
}

// Helper function to parse human-friendly boolean values
bool parseBool(String val, String label) {
  val.toUpperCase();
  val.trim();

  // Directions logic
  if (label.indexOf(F("DIRECTION")) >= 0) {
    if (val == F("FORWARD"))
      return true;
    if (val == F("BACKWARD"))
      return false;
  }

  // Standard logic
  if (val == F("ON") || val == F("1") || val == F("TRUE") || val == F("YES"))
    return true;
  return false;
}