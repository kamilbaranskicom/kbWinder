#include "eeprom.h"

// --- EEPROM HELPERS ---

void loadMachineConfiguration() {
  EEPROM.get(EEPROM_CONF_ADDR, cfg);
  if (areAnySettingNonsense(cfg)) { // First run defaults
    loadFallbackConfiguration();
  }

  if (!loadPresetByName("INIT")) {
    // fallback preset:
    Serial.println(F("Loading default preset."));
    active = (WindingPreset){
        "INIT", // char name[16];
        0.1,    // float wireDia;
        10.0,   // float coilWidth;
        1000,   // long totalTurns;
        120,    // int targetRPM;
        30,     // int rampRPM;
        10      // float startOffset;
    };
  }

  updateDerivedValues();
}

void loadFallbackConfiguration() {
  Serial.println(F("Loading default configuration."));
  // fallback config
  cfg = {
      1.0,   // float screwPitch;
      1600,  // int stepsPerRevW;
      1600,  // int stepsPerRevT;
      120,   // int maxRPM_W;
      150,   // int maxRPM_T;
      40,    // int startRPM_W;
      40,    // int startRPM_T;
      30,    // int defaultRamp_T;
      30,    // int defaultRamp_W;
      false, // bool dirW;
      false, // bool dirT;
      true,  // bool useLimitSwitch;
      false, // bool homeBeforeStart;
      true,  // bool useStartOffset;
      2      // float backoffDistanceMM;
  };
  saveMachineConfiguration();
}

void saveMachineConfiguration() { EEPROM.put(EEPROM_CONF_ADDR, cfg); }

bool areAnySettingNonsense(const MachineConfig &c) {
  // 1. Screw pitch nie może być zerem ani ujemny (standard to 1.0 - 5.0)
  if (c.screwPitch <= 0.01 || c.screwPitch > 50.0)
    return true;

  // 2. Kroki na obrót (zazwyczaj 200, 400, 800, 1600, 3200)
  if (c.stepsPerRevW < 200 || c.stepsPerRevW > 10000)
    return true;
  if (c.stepsPerRevT < 200 || c.stepsPerRevT > 10000)
    return true;

  // 3. RPM-y: jeśli są ujemne lub absurdalnie wysokie (np. błąd odczytu float)
  if (c.maxRPM_W <= 5 || c.maxRPM_W > 4000)
    return true;
  if (c.maxRPM_T <= 5 || c.maxRPM_T > 4000)
    return true;

  // 4. Start RPM musi być mniejszy niż Max RPM
  if (c.startRPM_W <= 0 || c.startRPM_W >= c.maxRPM_W)
    return true;
  if (c.startRPM_T <= 0 || c.startRPM_T >= c.maxRPM_T)
    return true;

  if (c.defaultRamp_W > 150)
    return true;
  if (c.defaultRamp_T > 150)
    return true;

  return false; // Wszystko wygląda okej
}

int findPresetIndex(String name) {
  name.trim();
  if (name.length() == 0)
    return -1;

  // Konwertujemy szukaną nazwę na tymczasowy bufor char, by uniknąć Stringów w
  // pętli
  char searchBuf[16];
  name.toCharArray(searchBuf, 16);

  WindingPreset p;
  for (int i = 0; i < MAX_PRESETS; i++) {
    EEPROM.get(EEPROM_PRESET_START + (i * sizeof(WindingPreset)), p);

    // Jeśli slot jest całkowicie pusty (0 lub 255) - koniec listy (brak dziur)
    if (p.name[0] == 0 || (uint8_t)p.name[0] == 255)
      break;

    // Porównujemy surowe tablice znaków - najpewniejsza metoda w C
    if (strcmp(p.name, searchBuf) == 0) {
      return i;
    }
  }
  return -1;
}

int findFirstEmptyPresetSlot() {
  WindingPreset p;
  for (int i = 0; i < MAX_PRESETS; i++) {
    int addr = EEPROM_PRESET_START + (i * sizeof(WindingPreset));
    EEPROM.get(addr, p);
    // If first character is null, the slot is empty
    if (p.name[0] == 0 || (uint8_t)p.name[0] == 255)
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
    if (p.name[0] == 0 || (uint8_t)p.name[0] == 255)
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
  Serial.println(F("--- CSV EXPORT END ---"));
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
  handleGet(F("PRESET"));
  return true;
}

char *trimChar(char *str) {
  char *end;
  while (isspace((unsigned char)*str))
    str++;
  if (*str == 0)
    return str;
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end))
    end--;
  end[1] = '\0';
  return str;
}

bool savePreset(String cmd) {
  cmd.trim();

  // 1. Oczyszczenie komendy z przedrostka "SAVE"
  if (cmd.startsWith("SAVE")) {
    cmd = cmd.substring(4);
    cmd.trim();
  }

  // Tworzymy NOWY obiekt zamiast kopii active, żeby nie dziedziczyć błędnych
  // nazw
  WindingPreset pToSave;
  memset(&pToSave, 0, sizeof(WindingPreset));

  if (cmd.length() > 0) {
    if (cmd.indexOf(',') != -1) {
      // SCENARIUSZ 1: IMPORT CSV
      char buf[128];
      cmd.toCharArray(buf, sizeof(buf));
      char *token = strtok(buf, ",");

      if (token) {
        String n = String(token);
        n.trim();
        n.toCharArray(pToSave.name, sizeof(pToSave.name));
      }

      // Wczytujemy resztę wartości do pToSave
      if ((token = strtok(NULL, ",")))
        pToSave.wireDia = atof(token);
      if ((token = strtok(NULL, ",")))
        pToSave.coilWidth = atof(token);
      if ((token = strtok(NULL, ",")))
        pToSave.totalTurns = atol(token);
      if ((token = strtok(NULL, ",")))
        pToSave.targetRPM = atoi(token);
      if ((token = strtok(NULL, ",")))
        pToSave.rampRPM = atoi(token);
      if ((token = strtok(NULL, ",")))
        pToSave.startOffset = atof(token);

      // Dopiero teraz aktualizujemy active (gdy mamy pewność, że wczytaliśmy
      // CSV)
      active = pToSave;
    } else {
      // SCENARIUSZ 2: SAVE NowaNazwa
      // Nazwa to po prostu trimowany cmd
      cmd.toCharArray(pToSave.name, sizeof(pToSave.name));

      // Kopiujemy resztę wartości z active
      pToSave.wireDia = active.wireDia;
      pToSave.coilWidth = active.coilWidth;
      pToSave.totalTurns = active.totalTurns;
      pToSave.targetRPM = active.targetRPM;
      pToSave.rampRPM = active.rampRPM;
      pToSave.startOffset = active.startOffset;

      strncpy(active.name, pToSave.name, sizeof(active.name));
    }
  } else {
    // SCENARIUSZ 3: SAVE (Puste)
    pToSave = active;
  }

  // --- FINALNY ZAPIS ---
  if (pToSave.name[0] == 0)
    return false;

  int index = findPresetIndex(pToSave.name);
  if (index == -1)
    index = findFirstEmptyPresetSlot();

  if (index != -1) {
    EEPROM.put(EEPROM_PRESET_START + (index * sizeof(WindingPreset)), pToSave);
    Serial.print(F("SYSTEM: Preset '"));
    Serial.print(pToSave.name);
    Serial.print(F("' saved to slot "));
    Serial.println(index);
    return true;
  }
  return false;
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

void formatPresets() {
  WindingPreset empty;
  memset(&empty, 0, sizeof(WindingPreset));
  for (int i = 0; i < MAX_PRESETS; i++) {
    EEPROM.put(EEPROM_PRESET_START + (i * sizeof(WindingPreset)), empty);
  }
  Serial.println(F("SYSTEM: EEPROM Presets wiped."));
}
