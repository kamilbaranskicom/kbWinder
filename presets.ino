#include "presets.h"


// --- EEPROM HELPERS ---

void loadConfig() {
  EEPROM.get(EEPROM_CONF_ADDR, cfg);
  if (cfg.stepsPerRevW == -1) { // First run defaults
    cfg = {2.0, 1600, 1600, 600, 400, false, false, true, false};
    EEPROM.put(EEPROM_CONF_ADDR, cfg);
  }
}

int findPresetIndex(String name) {
  WindingPreset p;
  for (int i = 0; i < MAX_PRESETS; i++) {
    EEPROM.get(EEPROM_PRESET_START + (i * sizeof(WindingPreset)), p);
    if (String(p.name) == name) return i;
    if (p.name[0] == 0) break; // End of list
  }
  return -1;
}

void exportCSV() {
  Serial.println(F("--- CSV EXPORT ---"));
  Serial.println(F("name,wire,width,turns,rpm,ramp,offset"));
  WindingPreset p;
  for (int i = 0; i < MAX_PRESETS; i++) {
    EEPROM.get(EEPROM_PRESET_START + (i * sizeof(WindingPreset)), p);
    if (p.name[0] == 0) break;
    Serial.print(p.name); Serial.print(',');
    Serial.print(p.wireDia, 3); Serial.print(',');
    Serial.print(p.coilWidth, 2); Serial.print(',');
    Serial.print(p.totalTurns); Serial.print(',');
    Serial.print(p.targetRPM); Serial.print(',');
    Serial.print(p.rampRPM); Serial.print(',');
    Serial.println(p.startOffset);
  }
}
