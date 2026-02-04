#include "variables.h"

// --- COMMAND HANDLERS ---

void handleSet(String query) {
  query.trim();

  for (int i = 0; i < varCount; i++) {
    size_t labelLen = strlen(varTable[i].label);
    if (strncasecmp(query.c_str(), varTable[i].label, labelLen) == 0) {
      String valStr = query.substring(labelLen);
      valStr.trim();
      if (valStr.length() == 0)
        return;

      if (varTable[i].category == C_RUNTIME) {
        Serial.println(F("ERROR: Runtime values are read-only."));
        return;
      }

      // Update RAM
      switch (varTable[i].type) {
        case T_FLOAT:
          *(float *)varTable[i].ptr = valStr.toFloat();
          break;
        case T_INT:
          *(int *)varTable[i].ptr = valStr.toInt();
          break;
        case T_LONG:
          *(long *)varTable[i].ptr = valStr.toInt();
          break;
        case T_BOOL:
          *(bool *)varTable[i].ptr = parseBool(valStr, String(varTable[i].label));
          break;
        case T_CHAR:
          {
            uint8_t limit = varTable[i].maxLen - 1;  // Zostawiamy 1 bajt na \0
            strncpy((char *)varTable[i].ptr, valStr.c_str(), limit);
            ((char *)varTable[i].ptr)[limit] = '\0';
            break;
          }
      }

      // AUTO-SAVE for machine category only
      if (varTable[i].category == C_MACHINE) {
        EEPROM.put(EEPROM_CONF_ADDR, cfg);
        Serial.println(F("SYSTEM: Machine config updated and saved to EEPROM. "));
      } else {
        Serial.println(F("SYSTEM: Preset parameter updated in RAM. "));
      }

      Serial.print(varTable[i].label);
      Serial.print(F(" = "));
      handleGet(varTable[i].label);
      updateDerivedValues();
      return;
    }
  }
  Serial.println(F("ERROR: Unknown parameter"));
}

void handleGet(String query) {
  query.trim();
  if (query.startsWith(F("GET "))) query.remove(0, 4);
  query.trim();

  // Identify if the request is for a specific category
  // -1: Not a category filter (searching for label or printing ALL)
  int filterCategory = -1;
  if (query.equalsIgnoreCase(F("MACHINE"))) filterCategory = C_MACHINE;
  else if (query.equalsIgnoreCase(F("PRESET"))) filterCategory = C_PRESET;
  else if (query.equalsIgnoreCase(F("RUNTIME"))) filterCategory = C_RUNTIME;

  bool showAll = (query.length() == 0);

  if (showAll) {
    Serial.println(F("ALL SETTINGS:"));
  } else if (filterCategory != -1) {
    Serial.print(query);
    Serial.println(F(" SETTINGS:"));
  }

  bool found = false;
  for (int i = 0; i < varCount; i++) {
    // Filtering logic:
    // Show if: 1. It's 'GET' (all), 2. Category matches, 3. Label matches
    bool categoryMatch = (filterCategory != -1 && varTable[i].category == filterCategory);
    bool labelMatch = (strcasecmp(query.c_str(), varTable[i].label) == 0);

    if (showAll || categoryMatch || labelMatch) {
      found = true;

      // Wypisywanie kategorii parametru
      if (varTable[i].category == C_MACHINE) {
        Serial.print(F("[MACHINE] "));
      } else if (varTable[i].category == C_PRESET) {
        Serial.print(F("[PRESET]  "));
      } else {
        Serial.print(F("[RUNTIME] "));
      }

      Serial.print(varTable[i].label);
      Serial.print(F(": "));

      switch (varTable[i].type) {
        case T_CHAR:
          Serial.println((char *)varTable[i].ptr);
          break;
        case T_FLOAT:
          Serial.println(*(float *)varTable[i].ptr, 3);
          break;
        case T_INT:
          Serial.println(*(int *)varTable[i].ptr);
          break;
        case T_LONG:
          Serial.println(*(long *)varTable[i].ptr);
          break;
        case T_BOOL:
          {
            bool val = *(bool *)varTable[i].ptr;
            // Specjalna obsługa dla kierunków i przełączników
            if (strstr(varTable[i].label, "DIRECTION") != NULL) {
              Serial.println(val ? F("FORWARD") : F("BACKWARD"));
            } else {
              Serial.println(val ? F("ON") : F("OFF"));
            }
            break;
          }
      }
    }
  }

  if (!found) {
    Serial.println(F("ERROR: Parameter or category not found"));
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