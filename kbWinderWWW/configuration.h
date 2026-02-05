/**
 * @file configuration.h
 * @brief Global configuration structures and JSON serialization logic for kbWinder.
 * @author Kamil Baranski
 * @date 2026-01-23
 */

#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include "debug.h"
#include "filesystem.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <IPAddress.h>
#include <LittleFS.h>
#include <type_traits>

/** @name Protected Files
 * List of files that are critical for system operation and cannot be deleted via API.
 */
///@{
const char *PROTECTED_FILES[] = {
    "/configuration.json", "/index.html", "/setup.html", "/favicon.svg", "/kbWinder.js", "/kbWinder.css", "/variables.h"};

const size_t PROTECTED_FILES_COUNT = sizeof(PROTECTED_FILES) / sizeof(PROTECTED_FILES[0]);

/**
 * @brief Checks if a given path is on the protected files list.
 * @param path The file path to check.
 * @return true if protected, false otherwise.
 */
bool isFileProtected(const String &path) {
  for (size_t i = 0; i < PROTECTED_FILES_COUNT; i++) {
    if (path == PROTECTED_FILES[i])
      return true;
  }
  return false;
}
///@}

// -----------------------------------------------------------------------------
// JSON Serialization Utilities
// -----------------------------------------------------------------------------

/**
 * @class TailSpy
 * @brief A helper class for Print objects (File, Serial) to track the last character written.
 * Used to verify if a JSON document was fully and correctly serialized.
 */
class TailSpy : public Print {
public:
  Print &base;
  char lastChar = 0;
  TailSpy(Print &p) : base(p) {}

  size_t write(uint8_t c) override {
    if (c > 31)
      lastChar = (char)c; // Store last printable character
    return base.write(c);
  }

  size_t write(const uint8_t *buffer, size_t size) override {
    if (size > 0) {
      for (int i = size - 1; i >= 0; i--) {
        if (buffer[i] > 31) { // Find the last printable character
          lastChar = (char)buffer[i];
          break;
        }
      }
    }
    return base.write(buffer, size);
  }
};

/** @brief Simple verification of valid JSON termination (bracket or brace). */
inline bool isJsonTailValid(char lastChar) { return (lastChar == '}' || lastChar == ']'); }

/**
 * @brief Universal Smart Serialization function.
 * Tries Pretty Print first; if it fails or is truncated, falls back to compact JSON.
 * @note Fallback is only executed for String and File types which can be reset.
 */
template <typename T> size_t serializeJsonSmart(const JsonDocument &doc, T &output) {
  size_t expected = measureJsonPretty(doc);
  size_t written = 0;
  char lastByte = 0;

  // 1. Attempt Pretty Print
  if constexpr (std::is_base_of<Print, typename std::remove_reference<T>::type>::value) {
    TailSpy spy(output);
    written = serializeJsonPretty(doc, spy);
    lastByte = spy.lastChar;
  } else if constexpr (std::is_same<T, String>::value) {
    written = serializeJsonPretty(doc, output);
    if (written >= 1)
      lastByte = output[written - 1];
  }

  // 2. Success Verification
  if (written == expected && isJsonTailValid(lastByte)) {
    logMessagef(LOG_LEVEL_VERBOSE, "SmartJSON: Pretty OK (%d/%d, last='%c')", written, expected, lastByte);
    return written;
  }

  // 3. Error Handling (Fallback)
  // Fallback is only safe for String/File where we can undo the partial write.
  if constexpr (std::is_same<T, String>::value || std::is_same<T, File>::value) {
    logMessagef(LOG_LEVEL_WARNING, "SmartJSON: Pretty failed (%d/%d). Falling back to compact.", written, expected);

    if constexpr (std::is_same<T, String>::value) {
      output = "";
    } else if constexpr (std::is_same<T, File>::value) {
      output.seek(0, SeekSet);
      output.truncate(0);
    }
    return serializeJson(doc, output);
  } else {
    // For Streams (WebResponse/Serial), we cannot undo, so we just log the error.
    logMessagef(LOG_LEVEL_ERROR, "SmartJSON: Pretty incomplete on stream! Sent %d/%d bytes.", written, expected);
    return written;
  }
}

/** @brief Overload for char buffers. */
inline size_t serializeJsonSmart(const JsonDocument &doc, char *output, size_t size) {
  size_t expectedPretty = measureJsonPretty(doc);
  if (size > expectedPretty) {
    size_t written = serializeJsonPretty(doc, output, size);
    if (written >= 2 && isJsonTailValid(output[written - 1]))
      return written;
  }
  return serializeJson(doc, output, size);
}

// -----------------------------------------------------------------------------
// Software Info Structure
// -----------------------------------------------------------------------------

/**
 * @struct SoftwareInfo
 * @brief Metadata about the current firmware build for UI display.
 */
struct SoftwareInfo {
  const __FlashStringHelper *name;
  const __FlashStringHelper *version;
  const __FlashStringHelper *date;
  const __FlashStringHelper *author;
  const __FlashStringHelper *authorUrl;
  const __FlashStringHelper *productUrl;

  void toJson(JsonObject root) const {
    root[F("name")] = name;
    root[F("version")] = version;
    root[F("date")] = date;
    root[F("author")] = author;
    root[F("authorUrl")] = authorUrl;
    root[F("productUrl")] = productUrl;
  }
};

const char PASSWORD_PLACEHOLDER[] PROGMEM = "********";
const char PATH_404_HTML[] PROGMEM = "/404.html";
const char TEXT_PLAIN[] PROGMEM = "text/plain";
const char TEXT_HTML[] PROGMEM = "text/html";
const char APPLICATION_JSON[] PROGMEM = "application/json";

// -----------------------------------------------------------------------------
// Network Configuration Structure
// -----------------------------------------------------------------------------

/**
 * @struct NetworkConfiguration
 * @brief Settings for WiFi Station and Access Point modes.
 */
struct NetworkConfiguration {
  // Station (Client) Mode Settings
  char stationSsid[32];
  char stationPassword[64];
  bool stationDhcpEnabled;
  IPAddress stationStaticIp;
  IPAddress stationStaticMask;
  IPAddress stationStaticGateway;

  // Access Point (Service) Mode Settings
  char accessPointSsid[32];
  char accessPointPassword[64];
  IPAddress accessPointIp;
  IPAddress accessPointMask;
  IPAddress accessPointGateway;

  uint8_t wifiMode; // 0: STA+AP fallback, 1: Always AP, 2: Always STA
  uint16_t stationConnectTimeoutSeconds;

  void resetToFactoryDefaults() {
    strlcpy(stationSsid, "", sizeof(stationSsid));
    strlcpy(stationPassword, "", sizeof(stationPassword));
    stationDhcpEnabled = true;
    stationStaticIp = IPAddress(192, 168, 1, 100);
    stationStaticMask = IPAddress(255, 255, 255, 0);
    stationStaticGateway = IPAddress(192, 168, 1, 1);

    uint8_t mac[6];
    WiFi.macAddress(mac);
    sprintf(accessPointSsid, "kbWinder-%02x%02x-Setup", mac[4], mac[5]);

    strlcpy(accessPointPassword, "kbWinder123", sizeof(accessPointPassword));
    accessPointIp = IPAddress(192, 168, 4, 1);
    accessPointMask = IPAddress(255, 255, 255, 0);
    accessPointGateway = IPAddress(192, 168, 4, 1);

    wifiMode = 0;
    stationConnectTimeoutSeconds = 15;
  }

  void toJson(JsonObject root, bool isPublic, bool maskPasswords = false) const {
    if (!isPublic) {
      root["stationSsid"] = stationSsid;
      root["stationPassword"] =
          (maskPasswords && strlen(stationPassword) > 0) ? FPSTR(PASSWORD_PLACEHOLDER) : (const __FlashStringHelper *)stationPassword;
      root["stationDhcpEnabled"] = stationDhcpEnabled;
      root["stationStaticIp"] = stationStaticIp.toString();
      root["stationStaticMask"] = stationStaticMask.toString();
      root["stationStaticGateway"] = stationStaticGateway.toString();
      root["accessPointSsid"] = accessPointSsid;
      root["accessPointPassword"] = (maskPasswords && strlen(accessPointPassword) > 0) ? FPSTR(PASSWORD_PLACEHOLDER)
                                                                                       : (const __FlashStringHelper *)accessPointPassword;
      root["accessPointIp"] = accessPointIp.toString();
      root["accessPointMask"] = accessPointMask.toString();
      root["wifiMode"] = wifiMode;
      root["stationConnectTimeoutSeconds"] = stationConnectTimeoutSeconds;
    }
  }

  void fromJson(JsonObjectConst src, bool isInternal = false) {
    if (isInternal || !isSystemLocked()) {
      if (src["stationSsid"])
        strlcpy(stationSsid, src["stationSsid"], sizeof(stationSsid));
      const char *staPwd = src["stationPassword"];
      if (staPwd && strcmp_P(staPwd, (const char *)PASSWORD_PLACEHOLDER) != 0) {
        strlcpy(stationPassword, staPwd, sizeof(stationPassword));
      }
      stationDhcpEnabled = src["stationDhcpEnabled"] | stationDhcpEnabled;
      if (src["stationStaticIp"])
        stationStaticIp.fromString(src["stationStaticIp"].as<const char *>());
      if (src["stationStaticMask"])
        stationStaticMask.fromString(src["stationStaticMask"].as<const char *>());
      if (src["stationStaticGateway"])
        stationStaticGateway.fromString(src["stationStaticGateway"].as<const char *>());
      const char *apPwd = src["accessPointPassword"];
      if (apPwd && strcmp_P(apPwd, (const char *)PASSWORD_PLACEHOLDER) != 0) {
        strlcpy(accessPointPassword, apPwd, sizeof(accessPointPassword));
      }
      if (src["accessPointIp"])
        accessPointIp.fromString(src["accessPointIp"].as<const char *>());
      if (src["accessPointMask"])
        accessPointMask.fromString(src["accessPointMask"].as<const char *>());
      wifiMode = src["wifiMode"] | wifiMode;
      stationConnectTimeoutSeconds = src["stationConnectTimeoutSeconds"] | stationConnectTimeoutSeconds;
    }
  }
};

// -----------------------------------------------------------------------------
// Security Configuration Structure
// -----------------------------------------------------------------------------

/**
 * @struct SecurityConfiguration
 * @brief Credentials for Web UI access.
 */
struct SecurityConfiguration {
  bool authenticationEnabled;
  char adminUsername[32];
  char adminPassword[32];

  void resetToFactoryDefaults() {
    authenticationEnabled = false;
    strlcpy(adminUsername, "admin", sizeof(adminUsername));
    strlcpy(adminPassword, "admin", sizeof(adminPassword));
  }

  void toJson(JsonObject root, bool isPublic, bool maskPasswords = false) const {
    if (!isPublic) {
      root["authenticationEnabled"] = authenticationEnabled;
      root["adminUsername"] = adminUsername;
      root["adminPassword"] =
          (maskPasswords && strlen(adminPassword) > 0) ? FPSTR(PASSWORD_PLACEHOLDER) : (const __FlashStringHelper *)adminPassword;
    }
  }

  void fromJson(JsonObjectConst src, bool isInternal = false) {
    if (isInternal || !isSystemLocked()) {
      authenticationEnabled = src["authenticationEnabled"] | authenticationEnabled;
      if (src["adminUsername"])
        strlcpy(adminUsername, src["adminUsername"], sizeof(adminUsername));
      const char *admPwd = src["adminPassword"];
      if (admPwd && strcmp_P(admPwd, (const char *)PASSWORD_PLACEHOLDER) != 0) {
        strlcpy(adminPassword, admPwd, sizeof(adminPassword));
      }
    }
  }
};

// -----------------------------------------------------------------------------
// System Configuration Structure
// -----------------------------------------------------------------------------

/**
 * @struct SystemConfiguration
 * @brief Main system flags, log levels, and module toggles.
 */
struct SystemConfiguration {
  char hostName[32];
  uint8_t version;

  LogLevel serialLogLevel;
  LogLevel webLogLevel;
  bool webStatusUpdate;
  bool webSystemLogEnabled;
  bool webDebugEnabled;

  bool pushOTAEnabled;
  bool mdnsEnabled;
  bool nbsEnabled;
  bool ssdpEnabled;
  bool dnsServerEnabled;
  bool midiEnabled;
  bool hardwareInputsEnabled;

  String webStaticFilesPath;

  void resetToFactoryDefaults() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    sprintf(hostName, "kbwinder-%02x%02x", mac[4], mac[5]);
    version = 1;
    serialLogLevel = LOG_LEVEL_DEBUG;
    webLogLevel = LOG_LEVEL_INFO;
    webStatusUpdate = true;
    webSystemLogEnabled = true;
    webDebugEnabled = false;
    pushOTAEnabled = false;
    mdnsEnabled = true;
    nbsEnabled = false;
    ssdpEnabled = true;
    dnsServerEnabled = true;
    webStaticFilesPath = "";
  }

  void toJson(JsonObject root, bool isPublic) const {
    root["hostName"] = hostName;
    root["version"] = version;
    if (!isPublic) {
      root["serialLogLevel"] = (int8_t)serialLogLevel;
      root["webLogLevel"] = (int8_t)webLogLevel;
      root["webStatusUpdate"] = webStatusUpdate;
      root["webSystemLogEnabled"] = webSystemLogEnabled;
      root["webDebugEnabled"] = webDebugEnabled;
      root["pushOTAEnabled"] = pushOTAEnabled;
      root["mdnsEnabled"] = mdnsEnabled;
      root["nbsEnabled"] = nbsEnabled;
      root["ssdpEnabled"] = ssdpEnabled;
      root["dnsServerEnabled"] = dnsServerEnabled;
      root["webStaticFilePath"] = webStaticFilesPath;
    }
  }

  void fromJson(JsonObjectConst src, bool isInternal = false) {
    if (!isSystemLocked()) {
      if (src["hostName"])
        strlcpy(hostName, src["hostName"], sizeof(hostName));
      version = src["version"] | version;
    }
    if (src.containsKey("serialLogLevel"))
      serialLogLevel = (LogLevel)src["serialLogLevel"].as<int>();
    if (src.containsKey("webLogLevel"))
      webLogLevel = (LogLevel)src["webLogLevel"].as<int>();
    webStatusUpdate = src["webStatusUpdate"] | webStatusUpdate;
    webSystemLogEnabled = src["webSystemLogEnabled"] | webSystemLogEnabled;
    webDebugEnabled = src["webDebugEnabled"] | webDebugEnabled;
    if (!isSystemLocked()) {
      pushOTAEnabled = src["pushOTAEnabled"] | pushOTAEnabled;
      mdnsEnabled = src["mdnsEnabled"] | mdnsEnabled;
      nbsEnabled = src["nbsEnabled"] | nbsEnabled;
      ssdpEnabled = src["ssdpEnabled"] | ssdpEnabled;
      dnsServerEnabled = src["dnsServerEnabled"] | dnsServerEnabled;
    }
    webStaticFilesPath = src["webStaticFilesPath"] | webStaticFilesPath;
  }
};

// -----------------------------------------------------------------------------
// UI Configuration Structure
// -----------------------------------------------------------------------------

#define SLOW_STOP_FAST 0
#define STOP_SLOW_FAST 1

/**
 * @struct UiConfiguration
 * @brief Settings for Web UI layout.
 */
struct UiConfiguration {
  static const int MAX_SECTIONS = 12;
  static const int MAX_SECTION_NAME_LEN = 16;

  char sectionOrder[MAX_SECTIONS][MAX_SECTION_NAME_LEN];
  int sectionCount;

  void resetToFactoryDefaults() {
    sectionCount = 9;
    const char *defaults[] = {"manual", "system", "log", "status", "network", "midi", "hardware", "misc", "save"};
    for (int i = 0; i < sectionCount; i++) {
      strlcpy(sectionOrder[i], defaults[i], MAX_SECTION_NAME_LEN);
    }
  }

  void toJson(JsonObject root, bool isPublic) const {
    JsonArray arr = root.createNestedArray("sectionOrder");
    for (int i = 0; i < sectionCount; i++)
      arr.add(sectionOrder[i]);
  }

  void fromJson(JsonObjectConst src, bool isInternal = false) {
    if (isInternal || !isSystemLocked()) {
      JsonArrayConst arr = src["sectionOrder"];
      if (!arr.isNull()) {
        int i = 0;
        for (JsonVariantConst v : arr) {
          if (i < MAX_SECTIONS) {
            strlcpy(sectionOrder[i], v.as<const char *>(), MAX_SECTION_NAME_LEN);
            i++;
          }
        }
        sectionCount = i;
      }
    }
  }
};

// -----------------------------------------------------------------------------
// Main Configuration Structure
// -----------------------------------------------------------------------------

const uint16_t CONFIG_JSON_SIZE = 4096;

/**
 * @struct MainConfiguration
 * @brief Root configuration aggregator with file I/O logic.
 */
struct MainConfiguration {
  SystemConfiguration system;
  NetworkConfiguration network;
  SecurityConfiguration security;
  UiConfiguration ui;

  void resetToFactoryDefaults() {
    system.resetToFactoryDefaults();
    network.resetToFactoryDefaults();
    security.resetToFactoryDefaults();
    ui.resetToFactoryDefaults();
  }

  void toJson(JsonObject root, bool isPublic = false, bool maskPasswords = false) const {
    system.toJson(root.createNestedObject("system"), isPublic);
    network.toJson(root.createNestedObject("network"), isPublic, maskPasswords);
    security.toJson(root.createNestedObject("security"), isPublic, maskPasswords);
    ui.toJson(root.createNestedObject("ui"), isPublic);
  }

  bool fromJson(JsonObjectConst src, bool isInternal = false) {
    if (src["system"])
      system.fromJson(src["system"], isInternal);
    if (src["network"])
      network.fromJson(src["network"], isInternal);
    if (src["security"])
      security.fromJson(src["security"], isInternal);
    if (src["ui"])
      ui.fromJson(src["ui"], isInternal);
    logMessage(LOG_LEVEL_DEBUG, F("Config: Logic buffer updated.")); // TODO: validation
    return true;
  }

  bool saveToFile(const char *filename = "/configuration.json") {
    logMessagef(LOG_LEVEL_INFO, "Config: Saving to %s...", filename);
    String tempFilename = String(filename) + ".tmp";
    File file = LittleFS.open(tempFilename, "w");
    if (!file) {
      logMessagef(LOG_LEVEL_ERROR, "Config: Failed to open temp file %s", tempFilename.c_str());
      return false;
    }

    DynamicJsonDocument doc(CONFIG_JSON_SIZE);
    this->toJson(doc.to<JsonObject>(), false, false); // false = don't remove private parameters, false = don't mask passwords
    size_t bytesWritten = serializeJsonSmart(doc, file);
    file.close();

    if (bytesWritten < 3) {
      logMessage(LOG_LEVEL_ERROR, "Config: Data too small, aborting.");
      LittleFS.remove(tempFilename);
      return false;
    }

    if (LittleFS.exists(filename))
      LittleFS.remove(filename);

    if (LittleFS.rename(tempFilename, filename)) {
      logMessagef(LOG_LEVEL_NOTICE, "Config: Saved (%d bytes). Creating backup...", bytesWritten);
      rotateAndCreateBackup(filename);
      return true;
    } else {
      logMessage(LOG_LEVEL_ERROR, F("Config: Rename failed! This is unexpected."));
    }
    return false;
  }

  bool loadFromFile(const char *filename = "/configuration.json") {
    logMessagef(LOG_LEVEL_INFO, "Config: Loading from %s...", filename);
    if (!LittleFS.exists(filename)) {
      logMessagef(LOG_LEVEL_WARNING, "Config: %s not found!", filename);
      return false;
    }
    File file = LittleFS.open(filename, "r");
    if (!file) {
      logMessagef(LOG_LEVEL_ERROR, "Config: Failed to open %s", filename);
      return false;
    }

    DynamicJsonDocument doc(CONFIG_JSON_SIZE);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
      logMessagef(LOG_LEVEL_ERROR, "Config: JSON Error: %s", error.c_str());
      return false;
    }
    return this->fromJson(doc.as<JsonObject>(), true); // ignore demo.lock
  }
};

extern MainConfiguration configuration;

#endif // CONFIGURATION_H