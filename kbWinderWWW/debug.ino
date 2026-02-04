/**
 * @file debug.ino
 * @brief Implementation of the logging system (Stream-only version).
 */

#include "c_types.h"
#include "configuration.h" // Required to access configuration.system.debugEnabled
#include "debug.h"

// #define USB_TX 1 // GPIO1
// #define USB_RX 3 // GPIO3
// SoftwareSerial debugSerial(USB_RX, USB_TX);

void initializeSerial(bool firstTime) {

  if (configuration.system.serialLogLevel == LOG_LEVEL_NOTHING) {
    logMessageEmptyLine(3);
    logMessage(LOG_LEVEL_NOTICE, F("\nSerial1 Debug disabled"));
    return;
  }

  DEBUG_UART.setRxBufferSize(2048);

  // 115200, SERIAL_8N1, USB_TX, USB_RX itp.
  DEBUG_UART.begin(57600); // to tylko port do debugu, nie wiadomo, czy będziemy tu coś pisać.
  serialInitialized = true;

  uint32_t startTime = millis();
  while (!DEBUG_UART && (millis() - startTime < 200)) { // 0.2s delay
    yield();
  }

  if (firstTime) {
    sayHello();

    if (DEBUG_UART) {
      logMessage(LOG_LEVEL_NOTICE, F("Serial1 Debug initialized with TX on D4"));
    } else {
      logMessage(LOG_LEVEL_WARNING, F("Serial1 Debug initialization failed."));
    }
  } else {
    logMessage(LOG_LEVEL_INFO, F("Serial1 Debug re-synchronized."));
  }
}

/**
 * Force UART0 to swapped pins (D7/D8) regardless of current state.
 * This is "idempotent" - calling it multiple times is safe.
 */
void forceSerialSwap() {
  // Accessing the ESP8266 register directly
  // Bit 0 of U0S (UART0 Swap) in the IOMUX register
  // This is more reliable than toggle-based Serial.swap()

  // In ESP8266 Arduino Core, you can check the bit or just use:
  if (!(USS(0) & (1 << U0S))) {
    Serial.swap();
  }
}

void sayHello() {
  logMessage(LOG_LEVEL_EMPTYLINE, F("\033[2J\033[H"));
  logMessageEmptyLine(2);
  for (uint8 i = 0; i < 9; i++) {
    logMessage(LOG_LEVEL_EMPTYLINE, logo[i]);
  }
  logMessagef(LOG_LEVEL_EMPTYLINE, PSTR("%s v. %s [%s]."), softwareInfo.name, softwareInfo.version, softwareInfo.date);
  logMessagef(LOG_LEVEL_EMPTYLINE, PSTR("%s"), softwareInfo.productUrl);
  logMessagef(LOG_LEVEL_EMPTYLINE, PSTR("by %s [ %s ]"), softwareInfo.author, softwareInfo.authorUrl);
  logMessageEmptyLine(2);
}

void debugSerialFlush() { DEBUG_UART.flush(); }

void serialPrintLog(LogLevel level, String message, bool newLine = true) {
  if (!serialInitialized)
    return;

  if (level == LOG_LEVEL_SENDCMD) {
    DEBUG_UART.println(message);
  }
  return;

  //  following just for debug when using UART bridge on the PC side

  // if (level == LOG_LEVEL_NANO)
  //   return; // nano nie odsyłamy z powrotem, bo nie zdążymy.

  const __FlashStringHelper *levelName = getLogLevelName(level);
  const char *color = getLogLevelAnsiColor(level);
  const char *reset = "\x1B[0m";

  // Wariant A: Kolorujemy tylko prefiks (spójne z log-prefix w CSS)
  // DEBUG_UART.printf("\r%s[%-7S]%s %s\n", color, levelName, reset, message.c_str());

  // Wariant B: Kolorujemy cały wiersz (lepiej widoczne w KiTTY)
  if (newLine) {
    DEBUG_UART.printf("\r%s[%-7S] %s%s\n", color, levelName, message.c_str(), reset);
  } else {
    DEBUG_UART.printf("\r%s[%-7S] %s%s", color, levelName, message.c_str(), reset);
  }
}

/**
 * @brief Internal helper to format and distribute logs.
 * @param level Severity level.
 * @param message The message string.
 */
void processLog(LogLevel level, String message, bool newLine = true) {
  if (level == LOG_LEVEL_NOTHING)
    return;

  // 1. Logowanie przez Web (WebSocket)
  if (level <= configuration.system.webLogLevel) {
    broadcastLog(level, message);
  }

  // 2. Logowanie przez Serial1 (D4)
  if (level <= configuration.system.serialLogLevel) {
    serialPrintLog(level, message, newLine);

    // W fazie bootu czekamy na wypchnięcie danych, żeby nic nie umknęło
    if (!isSystemReady) {
      debugSerialFlush();
      delay(DEBUG_UART_DELAY);
    }
  }
}

/**
 * @brief Overload for standard Strings.
 */
void logMessage(LogLevel level, const String &message) { processLog(level, message); }

/**
 * @brief Overload for Flash Strings (F() macro) - Fixes the "ambiguous" error.
 */
void logMessage(LogLevel level, const __FlashStringHelper *message) { processLog(level, String(message)); }

void logMessagef(LogLevel level, const char *format, ...) {
  char buffer[MAX_LOG_LINE_LENGTH];
  va_list args;
  va_start(args, format);
  vsnprintf_P(buffer, sizeof(buffer), format, args);
  va_end(args);

  processLog(level, String(buffer));
}

void logMessageEmptyLine(uint8 count) {
  for (uint8 i = 0; i < count; i++) {
    logMessage(LOG_LEVEL_EMPTYLINE, "");
  }
}

/**
 * Prints detailed hardware and memory information to verify
 * settings from sketch.json (160MHz, MMU, Flash Speed).
 */
#include <umm_malloc/umm_heap_select.h>
#include <umm_malloc/umm_malloc.h>
void printHardwareInfo() {
  logMessage(LOG_LEVEL_DEBUG, F("--- HARDWARE DIAGNOSTICS ---"));

  // 1. CPU Frequency
  uint32_t cpuFreq = ESP.getCpuFreqMHz();
  logMessagef(LOG_LEVEL_DEBUG, PSTR("CPU Frequency:  %d MHz"), cpuFreq);

  // 2. Flash Chip Info
  uint32_t flashSpeed = ESP.getFlashChipSpeed();
  FlashMode_t flashMode = ESP.getFlashChipMode();

  logMessagef(LOG_LEVEL_DEBUG, PSTR("Flash Speed:    %d MHz"), flashSpeed / 1000000);

  String flashModeStr = "";
  switch (flashMode) {
  case FM_QIO:
    flashModeStr = F("QIO (Fastest)");
    break;
  case FM_QOUT:
    flashModeStr = F("QOUT");
    break;
  case FM_DIO:
    flashModeStr = F("DIO");
    break;
  case FM_DOUT:
    flashModeStr = F("DOUT (Safe)");
    break;
  default:
    flashModeStr = F("Unknown");
    break;
  }
  logMessagef(LOG_LEVEL_DEBUG, PSTR("Flash Mode:     %s"), flashModeStr.c_str());

  // 3. Memory & MMU status
  // While there isn't a simple 'getMMU()' function, we can check
  // the available Heap and IRAM to see the effects of the 48KB/16KB split.
  logMessagef(LOG_LEVEL_DEBUG, PSTR("Free Heap:      %d bytes"), ESP.getFreeHeap());
  logMessagef(LOG_LEVEL_DEBUG, PSTR("Max Free Block: %d bytes"), ESP.getMaxFreeBlockSize());

  // Całkowita dostępna pamięć (suma obu heapów)
  logMessagef(LOG_LEVEL_DEBUG, PSTR("Total Free Heap: %d bytes"), ESP.getFreeHeap());

  // Największy ciągły blok (pokaże Ci, w którym heapie jest więcej miejsca)
  logMessagef(LOG_LEVEL_DEBUG, PSTR("Max Alloc Block: %d bytes"), ESP.getMaxFreeBlockSize());

  // Specyficzne info o "Second Heap"
  // (Działa tylko jeśli MMU jest ustawione na 2nd Heap)

  umm_info(NULL, 0); // To wypisze szczegółowe statystyki wszystkich bloków pamięci

  // 4. Sketch Info
  logMessagef(LOG_LEVEL_DEBUG, PSTR("Sketch Size:    %d bytes"), ESP.getSketchSize());
  logMessagef(LOG_LEVEL_DEBUG, PSTR("Free Space:     %d bytes"), ESP.getFreeSketchSpace());

  // 5. Reset Reason (Very useful for debugging Leslie motor interference!)
  logMessagef(LOG_LEVEL_DEBUG, PSTR("Reset Reason:   %s"), ESP.getResetReason().c_str());

  logMessage(LOG_LEVEL_DEBUG, F("----------------------------"));
}

void printSerialLogLevel() {
  logMessagef(LOG_LEVEL_ALWAYS,
      "Serial Debug Level is now %d [%s]",
      (int)configuration.system.serialLogLevel,
      getLogLevelName(configuration.system.serialLogLevel));
}