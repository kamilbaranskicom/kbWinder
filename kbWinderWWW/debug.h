/**
 * @file debug.h
 * @brief Professional logging system with levels and web access.
 */

#ifndef DEBUG_H
#define DEBUG_H

#define BOARD_NODEMCU // Let's use this for future-proofing

//#ifdef BOARD_NODEMCU
//#define DEBUG_UART Serial1
#define DEBUG_UART Serial
#define DEBUG_UART_DELAY 0
//#define SERIAL_NEEDS_TO_BE_SWAPPED
//#else
// Fallback for other boards
//#define DEBUG_UART Serial
// if we are on SoftwareSerial this slowers the log output until system is fully booted
//#define DEBUG_UART_DELAY 10
// #define SERIAL_NEEDS_TO_BE_SWAPPED
//#endif

/**
 * @enum LogLevel
 * @brief Severity levels for system messages.
 */
enum LogLevel {
  LOG_LEVEL_EMPTYLINE = 0,
  LOG_LEVEL_SENDCMD = 1, // command to nano!
  LOG_LEVEL_NANO = 2,    // command from nano!
  LOG_LEVEL_ALWAYS = 3,  // Always sent
  LOG_LEVEL_ERROR = 4,   // System crashes, failed updates
  LOG_LEVEL_WARNING = 5, // Retries, minor config errors
  LOG_LEVEL_NOTICE = 6,  // Milestones (OTA Start, Boot Finish)
  LOG_LEVEL_INFO = 7,    // General flow
  LOG_LEVEL_DEBUG = 8,   // Logic details (IP addresses, specific values)
  LOG_LEVEL_VERBOSE = 9, // High-frequency spam (OTA %, buffer chunks)
  LOG_LEVEL_NOTHING = 10 // skip always
};

const char s_EMPTYLINE[] PROGMEM = "";
const char s_ALWAYS[] PROGMEM = "*******";
const char s_ERROR[] PROGMEM = "ERROR";
const char s_WARNING[] PROGMEM = "WARNING";
const char s_NOTICE[] PROGMEM = "NOTICE";
const char s_INFO[] PROGMEM = "INFO";
const char s_DEBUG[] PROGMEM = "DEBUG";
const char s_VERBOSE[] PROGMEM = "VERBOSE";
const char s_NOTHING[] PROGMEM = "NOTHING";
const char s_UNKNOWN[] PROGMEM = "???????";
const char S_SENDCMD[] PROGMEM = "SENDCMD";
const char S_NANO[] PROGMEM = "NANO";

/**
 * @brief Returns a pointer to flash to the human-readable name for the LogLevel.
 */
const __FlashStringHelper *getLogLevelName(LogLevel level) {
  switch (level) {
  case LOG_LEVEL_EMPTYLINE:
    return (const __FlashStringHelper *)s_EMPTYLINE;
  case LOG_LEVEL_ALWAYS:
    return (const __FlashStringHelper *)s_ALWAYS;
  case LOG_LEVEL_ERROR:
    return (const __FlashStringHelper *)s_ERROR;
  case LOG_LEVEL_WARNING:
    return (const __FlashStringHelper *)s_WARNING;
  case LOG_LEVEL_NOTICE:
    return (const __FlashStringHelper *)s_NOTICE;
  case LOG_LEVEL_INFO:
    return (const __FlashStringHelper *)s_INFO;
  case LOG_LEVEL_DEBUG:
    return (const __FlashStringHelper *)s_DEBUG;
  case LOG_LEVEL_VERBOSE:
    return (const __FlashStringHelper *)s_VERBOSE;
  case LOG_LEVEL_NOTHING:
    return (const __FlashStringHelper *)s_NOTHING;
  case LOG_LEVEL_SENDCMD:
    return (const __FlashStringHelper *)S_SENDCMD;
  case LOG_LEVEL_NANO:
    return (const __FlashStringHelper *)S_NANO;
  default:
    return (const __FlashStringHelper *)s_UNKNOWN;
  }
}

/**
 * @brief Zwraca kod ANSI dla koloru odpowiadającego danemu poziomowi logowania.
 */
const char *getLogLevelAnsiColor(LogLevel level) {
  switch (level) {
  case LOG_LEVEL_ERROR:
    return "\x1B[31m"; // Red (ANSI 31)
  case LOG_LEVEL_WARNING:
    return "\x1B[33m"; // Yellow (ANSI 33)
  case LOG_LEVEL_NOTICE:
    return "\x1B[37m"; // White (ANSI 37)
  case LOG_LEVEL_INFO:
    return "\x1B[32m"; // Green (ANSI 32)
  case LOG_LEVEL_DEBUG:
    return "\x1B[90m"; // Dark Gray (ANSI 90)
  case LOG_LEVEL_VERBOSE:
    return "\x1B[37m"; // Light Gray
  case LOG_LEVEL_ALWAYS:
    return "\x1B[35m"; // Magenta
  case LOG_LEVEL_SENDCMD:
    return "\x1B[35m"; // Magenta too.
  case LOG_LEVEL_NANO:
    return "\x1B[32m"; // Green (ANSI 32)
  default:
    return "\x1B[0m"; // Reset
  }
}

#define MAX_LOG_LINE_LENGTH 200

void initializeSerial(bool firstTime = false);
/**
 * @brief Global logging function with multiple overloads.
 */
void logMessage(LogLevel level, const String &message);
void logMessage(LogLevel level, const __FlashStringHelper *message);

/**
 * @brief Formatted logging using printf-style syntax.
 * @param level Severity level.
 * @param format Format string (e.g., "Value: %d").
 */
void logMessagef(LogLevel level, const char *format, ...);

#endif // DEBUG_H