/**
 * @file kbWinderWWW.ino
 * @author Kamil Baranski (https://kamilbaranski.com/)
 * @brief Main entry point for the kbWinderWWW firmware
 * @version 1.0
 * @date 2026-04-04
 * * This firmware manages kbWinder
 */

#include <Arduino.h>

/** @name Software Identity Constants */
///@{
const char S____NAME[] PROGMEM = "kbWinderWWW";                                  ///< Product name
const char S_VERSION[] PROGMEM = "1.0";                                          ///< Software version
const char S____DATE[] PROGMEM = "2026-02-03";                                   ///< Build date
const char S__AUTHOR[] PROGMEM = "Kamil Baranski";                               ///< Author name
const char S_AUTHURL[] PROGMEM = "https://kamilbaranski.com/";                   ///< Author website
const char S_PRODURL[] PROGMEM = "https://kamilbaranski.com/kbWinderWWW/"; ///< Product website
/** @brief OTA Update URL (HTTP only for compatibility) */
const char S_UPDATEU[] PROGMEM = "http://kamilbaranski.com/kbWinderWWW/firmware/update.json?version=";
///@}

#define PUSHOTA ///< Define to enable Push OTA (Note: Consumes significant RAM)

#include "configuration.h"
#include "debug.h"
#include "kbWinderWWW.h"
#include "network.h"
#include "reset.h"

/** @brief Global configuration instance holding system, network, and logic settings */
MainConfiguration configuration;

/** @brief Structure holding software metadata for UI and logs */
SoftwareInfo softwareInfo = {
    FPSTR(S____NAME), FPSTR(S_VERSION), FPSTR(S____DATE), FPSTR(S__AUTHOR), FPSTR(S_AUTHURL), FPSTR(S_PRODURL), FPSTR(S_UPDATEU)};

/** @name Network and Communication */
///@{
#include <ESP8266WiFi.h>
#define debugSerial Serial1 ///< Secondary serial port for debug output
#include <ESPAsyncWebServer.h>
extern AsyncWebServer *server;

///@}

/** @brief Flag indicating if the system has completed the boot process */
bool isSystemReady = false;

/**
 * @brief Standard Arduino setup function.
 * * Orchestrates the system initialization in two phases:
 * 1. Memory and Filesystem initialization.
 * 2. Hardware and Network service startup.
 */
void setup() {
  // --- PHASE 1: Immediate Memory Init ---
  configuration.resetToFactoryDefaults(); ///< Load hardcoded safety defaults

  // --- PHASE 2: Hardware Init ---
  initializeSerial(true); ///< Establish serial communication

  // Serial.begin(115200);
  // delay(100);
  // Serial.println("OK, startujemy kbWinderUI.");

  // Temporarily elevate log level to show hardware diagnostics
  LogLevel previousLogLevel = configuration.system.serialLogLevel;
  configuration.system.serialLogLevel = LOG_LEVEL_DEBUG;
  printHardwareInfo();
  configuration.system.serialLogLevel = previousLogLevel;

  logMessage(LOG_LEVEL_NOTICE, F("SYSTEM: Booting kbWinder..."));

  initializeFileSystem(); ///< Mount LittleFS
  setupConfiguration();   ///< Load configuration.json from Flash

  initializeNetwork();   ///< Start WiFi (AP/STA)
  initializeWebServer(); ///< Start AsyncWebServer and WebSockets

  initializeFlashButton(); ///< Configure physical reset/function button

  blink(); ///< Visual confirmation of successful boot

  isSystemReady = true;
  logMessage(LOG_LEVEL_NOTICE, F("[SYSTEM] Boot process completed."));
  logMessage(LOG_LEVEL_DEBUG, F("Loop starts - logging is now asynchronous"));
}

/**
 * @brief Main execution loop.
 * * Divided into:
 * 1. High-priority tasks (MIDI/Hardware) for low-latency response.
 * 2. Background maintenance (Network/Web/OTA).
 */
void loop() {
  // 1. Background tasks: Network, OTA, Webserver
  processSerialInput();
  processUpdateRequest(); ///< Handle firmware update checks
  processNetworkTasks();  ///< Maintain WiFi and WebSocket connections
  processBlinks();        ///< Handle asynchronous LED patterns
  processPendingReboot(); ///< Execute reboot if requested by Web UI
  processFlashButton();   ///< Monitor button for long-press resets
}

#define NanoUart Serial

void processSerialInput() {
  static String batchBuffer = "";
  static unsigned long lastCharTime = 0;
  const unsigned long timeout = 50; // Czekamy 50ms na koniec paczki danych

  // 1. Czytamy wszystko co jest w buforze sprzętowym do batchBuffer
  while (DEBUG_UART.available()) {
    char c = DEBUG_UART.read();
    batchBuffer += c;
    lastCharTime = millis();
  }

  // 2. Jeśli mamy coś w buforze i DEBUG_UART milczy od 50ms (koniec serii danych)
  if (batchBuffer.length() > 0 && (millis() - lastCharTime > timeout)) {

    // Logujemy to lokalnie (opcjonalnie)
    logMessage(LOG_LEVEL_NOTICE, "Batch received, size: " + String(batchBuffer.length()));

    int startIdx = 0;
    int endIdx = batchBuffer.indexOf('\n');

    // Pętla rozbijająca String na linie
    while (endIdx != -1) {
      String line = batchBuffer.substring(startIdx, endIdx);
      line.trim(); // Usuwamy śmieci typu \r

      if (line.length() > 0) {
        // Wysyłamy każdą linię osobno - JS dostanie to co lubi
        logMessage(LOG_LEVEL_NANO, line);
      }

      startIdx = endIdx + 1;
      endIdx = batchBuffer.indexOf('\n', startIdx);
    }

    // Jeśli coś zostało na końcu bez znaku nowej linii (rzadkie, ale możliwe)
    String remaining = batchBuffer.substring(startIdx);
    remaining.trim();
    if (remaining.length() > 0) {
      logMessage(LOG_LEVEL_NANO, remaining);
    }

    // Czyścimy bufor pod następną serię
    batchBuffer = "";
  }
}

