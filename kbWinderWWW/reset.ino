
#include "reset.h"

void initializeFlashButton() { pinMode(BUTTON_FLASH, INPUT_PULLUP); }

void processFlashButton() {
  static unsigned long pressStart = 0;
  static int lastNotifiedStage = 0;

  // reset stage thresholds definition (ms)
  const unsigned long thresholds[] = {2500, 5000, 10000};
  const int numThresholds = 3;

  bool isPressed = (digitalRead(BUTTON_FLASH) == LOW);

  if (isPressed) {
    if (pressStart == 0)
      pressStart = millis();

    unsigned long duration = millis() - pressStart;

    // Sprawdzanie, czy przekroczyliśmy kolejny próg (tylko dla powiadomienia blink)
    for (int i = numThresholds - 1; i >= 0; i--) {
      int stage = i + 1;
      if (duration >= thresholds[i]) {
        if (lastNotifiedStage < stage) {
          lastNotifiedStage = stage;
          blink(2); // Sygnał: "możesz już puścić"
          logMessagef(LOG_LEVEL_DEBUG, "BUTTON: Stage %d reached", stage);
        }
        break;
      }
    }
  } else {
    // Przycisk puszczony - sprawdzamy czy start był zapisany
    if (pressStart != 0) {
      if (lastNotifiedStage > 0) {
        handleFlashAction(lastNotifiedStage);
      }
      // Reset stanu
      pressStart = 0;
      lastNotifiedStage = 0;
    }
  }
}

/**
 * @brief Wykonuje akcję przypisaną do danego poziomu trzymania przycisku.
 */
void handleFlashAction(int stage) {
  switch (stage) {
  case 1: // 2.5s
    logMessage(LOG_LEVEL_NOTICE, F("RESET BUTTON: [Stage 1] Network reset... Rebooting..."));
    configuration.network.resetToFactoryDefaults();
    configuration.saveToFile();
    blink(1);
    isRebootPending = true;
    break;

  case 2: // 5s
    logMessage(LOG_LEVEL_WARNING, F("RESET BUTTON: [Stage 2] Network & Security Reset... Rebooting..."));
    configuration.network.resetToFactoryDefaults();
    configuration.security.resetToFactoryDefaults();
    configuration.saveToFile();
    blink(5);
    isRebootPending = true;
    break;

  case 3: // 10s
    logMessage(LOG_LEVEL_ERROR, F("RESET BUTTON: [Stage 3] FULL Factory Reset!"));
    configuration.resetToFactoryDefaults();
    configuration.saveToFile();
    // copyFile("configuration_default.json", "configuration.json");
    blink(10);
    isRebootPending = true;
    break;
  }
}
