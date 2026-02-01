#include "variables.h"

// --- COMMAND HANDLERS ---

void handleSet(String line) {
  line.remove(0, 4);  // Remove "SET "
  line.trim();

  for (int i = 0; i < varCount; i++) {
    String label = String(varTable[i].label);
    if (line.startsWith(label)) {
      String valStr = line.substring(label.length());
      valStr.trim();
      if (valStr.length() == 0)
        return;

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
          *(bool *)varTable[i].ptr = parseBool(valStr, label);
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

      Serial.print(label);
      Serial.print(F(" = "));
      handleGet("GET " + label);
      updateDerivedValues();
      return;
    }
  }
  Serial.println(F("ERROR: Unknown parameter"));
}

void handleGet(String line) {
  line.remove(0, 4);  // Usuwa "GET "
  line.trim();

  if (line.length() == 0) {
    Serial.println(F("ALL SETTINGS:"));
  }

  bool found = false;
  for (int i = 0; i < varCount; i++) {
    String label = String(varTable[i].label);

    // Jeśli linia jest pusta, wypisz WSZYSTKIE. Jeśli etykieta pasuje, wypisz JEDNĄ.
    if (line.length() == 0 || line == label) {
      found = true;

      // Wypisywanie kategorii parametru
      if (varTable[i].category == C_MACHINE) {
        Serial.print(F("[MACHINE] "));
      } else {
        Serial.print(F("[PRESET]  "));
      }

      Serial.print(label);
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
            if (label.indexOf(F("DIRECTION")) >= 0) {
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
    Serial.println(F("ERROR: Parameter not found"));
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