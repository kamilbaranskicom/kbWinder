#include "presets.h"

// --- EEPROM HELPERS ---

void loadMachineConfig() {
  EEPROM.get(EEPROM_CONF_ADDR, cfg);
  if (cfg.stepsPerRevW == -1 || cfg.stepsPerRevW == 0) { // First run defaults
    cfg = {2.0, 1600, 1600, 600, 400, false, false, true, false};
    EEPROM.put(EEPROM_CONF_ADDR, cfg);
  }
  updateDerivedValues();
}

int findPresetIndex(String name) {
  WindingPreset p;
  for (int i = 0; i < MAX_PRESETS; i++) {
    EEPROM.get(EEPROM_PRESET_START + (i * sizeof(WindingPreset)), p);
    if (String(p.name) == name)
      return i;
    if (p.name[0] == 0)
      break; // End of list
  }
  return -1;
}

int findFirstEmptyPresetSlot() {
  WindingPreset p;
  for (int i = 0; i < MAX_PRESETS; i++) {
    int addr = EEPROM_PRESET_START + (i * sizeof(WindingPreset));
    EEPROM.get(addr, p);
    // If first character is null, the slot is empty
    if (p.name[0] == 0)
      return i;
  }
  return -1; // EEPROM is full
}

void exportCSV() {
  Serial.println(F("--- CSV EXPORT ---"));
  Serial.println(F("name,wire,width,turns,rpm,ramp,offset"));
  WindingPreset p;
  for (int i = 0; i < MAX_PRESETS; i++) {
    EEPROM.get(EEPROM_PRESET_START + (i * sizeof(WindingPreset)), p);
    if (p.name[0] == 0)
      break;
    Serial.print(p.name);
    Serial.print(',');
    Serial.print(p.wireDia, 3);
    Serial.print(',');
    Serial.print(p.coilWidth, 2);
    Serial.print(',');
    Serial.print(p.totalTurns);
    Serial.print(',');
    Serial.print(p.targetRPM);
    Serial.print(',');
    Serial.print(p.rampRPM);
    Serial.print(',');
    Serial.println(p.startOffset);
  }
}

bool loadPresetByName(String name) {
  name.replace("\"", ""); // Usuń ewentualne cudzysłowy
  name.trim();

  int index = findPresetIndex(name);
  if (index == -1) {
    Serial.print(F("ERROR: Preset '"));
    Serial.print(name);
    Serial.println(F("' not found."));
    return false;
  };

  // Wczytaj dane z EEPROM bezpośrednio do zmiennej globalnej 'active'
  EEPROM.get(EEPROM_PRESET_START + (index * sizeof(WindingPreset)), active);

  Serial.print(F("SYSTEM: Loaded preset '"));
  Serial.print(active.name);
  Serial.println(F("' into active memory."));

  // Po załadowaniu warto wyświetlić parametry, żeby użytkownik widział co
  // wczytał
  handleGet(F("GET"));
  return true;
}

void savePreset(String name) {
  if (name.length() == 0)
    return;

  // Clean name and check for existing
  name.replace("\"", "");
  int index = findPresetIndex(name);

  // If not found, find first empty slot
  if (index == -1) {
    index = findFirstEmptyPresetSlot();
  }

  if (index != -1) {
    // Copy current "active" parameters to a preset structure
    WindingPreset pToSave = active;
    memset(pToSave.name, 0, sizeof(pToSave.name));
    name.toCharArray(pToSave.name, 16);

    int addr = EEPROM_PRESET_START + (index * sizeof(WindingPreset));
    EEPROM.put(addr, pToSave);

    Serial.print(F("SYSTEM: Preset '"));
    Serial.print(pToSave.name);
    Serial.println(F("' saved to EEPROM."));
  } else {
    Serial.println(F("ERROR: EEPROM full. Cannot save preset."));
  }
}

void deletePreset(String name) {
  name.replace("\"", ""); // Usuń ewentualne cudzysłowy
  name.trim();

  int index = findPresetIndex(name);
  if (index == -1) {
    Serial.println(F("ERROR: Preset not found."));
    return;
  }

  WindingPreset nextPreset;
  int i;

  // Przesuwanie presetów w górę
  for (i = index; i < MAX_PRESETS - 1; i++) {
    // Pobierz następny preset
    EEPROM.get(EEPROM_PRESET_START + ((i + 1) * sizeof(WindingPreset)),
               nextPreset);

    // Zapisz go w miejsce aktualnego (usuwanego/przesuwanego)
    EEPROM.put(EEPROM_PRESET_START + (i * sizeof(WindingPreset)), nextPreset);

    // Jeśli trafiliśmy na pusty slot (koniec listy), możemy przerwać wcześniej
    if (nextPreset.name[0] == 0)
      break;
  }

  // Wyczyszczenie ostatniego slotu, aby uniknąć "duplikatu" na końcu listy
  WindingPreset empty;
  memset(&empty, 0, sizeof(WindingPreset));
  EEPROM.put(EEPROM_PRESET_START + (i * sizeof(WindingPreset)), empty);

  Serial.print(F("SYSTEM: Preset '"));
  Serial.print(name);
  Serial.println(F("' deleted and EEPROM compacted."));
}