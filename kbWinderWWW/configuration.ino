/**
 * @file configuration.ino
 * @brief High-level initialization logic for the system configuration.
 * @author Kamil Baranski
 * @date 2026-01-23
 */

#include "configuration.h"

/**
 * @brief Orchestrates the initial loading of settings from the filesystem.
 * * @details This function attempts to read `/configuration.json` from LittleFS.
 * If the file is missing or corrupted, it triggers a factory reset and creates
 * a fresh configuration file to ensure system stability.
 * * @note This should be called after initializeFileSystem() but before
 * starting network or hardware services.
 */
void setupConfiguration() {
  // Attempt to load settings from the internal flash storage
  if (!configuration.loadFromFile("/configuration.json")) {
    logMessage(LOG_LEVEL_WARNING, F("CONFIG: Loading failed or file missing. Resetting to defaults..."));

    // Safety fallback: Load hardcoded defaults and persist them
    configuration.resetToFactoryDefaults();
    configuration.saveToFile("/configuration.json");
    return;
  }

  logMessage(LOG_LEVEL_NOTICE, F("CONFIG: System settings loaded successfully."));

  // Log the current verbosity level to Serial for immediate feedback
  printSerialLogLevel();
}