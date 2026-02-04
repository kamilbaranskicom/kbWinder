
// Plik: update.ino

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

struct UpdateInfo {
  String version;
  String binUrl;
  String fsUrl;
};

// Global variable to track what we are currently downloading
String currentOtaStage = "IDLE";

void processUpdateRequest() {
  if (pendingUpdateRequest) {
    pendingUpdateRequest = false;
    logMessage(LOG_LEVEL_NOTICE, F("SYSTEM: Checking for updates..."));

    // 1. Give the WebServer a moment to actually send the response to the client
    delay(500);
    yield();

    // 2. Optional: Pause heavy tasks to free up RAM/IRAM
    // MIDI.stop(); // If needed to prevent interrupts during flash write

    checkAndRunUpdate();
  }
}

void checkAndRunUpdate() {
  WiFiClient client;
  HTTPClient http;

  ESP.wdtDisable();
  http.setTimeout(5000);

  if (http.begin(client,
          String((const __FlashStringHelper *)softwareInfo.updateUrl) + String((const __FlashStringHelper *)softwareInfo.version))) {
    String ua = F("kbWinder-OTA/");
    ua += String((const __FlashStringHelper *)softwareInfo.version);
    http.setUserAgent(ua);

    yield();
    int httpCode = http.GET();
    yield();
    logMessagef(LOG_LEVEL_DEBUG, "http.GET result: %d", httpCode);
    if (httpCode == HTTP_CODE_OK) {
      DynamicJsonDocument doc(512);
      deserializeJson(doc, http.getString());

      String latestVersion = doc["version"];
      if (latestVersion != softwareInfo.version) {
        logMessagef(LOG_LEVEL_NOTICE, "Current version: %s, available version: %s. Starting update!", softwareInfo.version, latestVersion);
        performUpdate(doc["firmware"], doc["filesystem"]);
      } else {
        logMessage(LOG_LEVEL_NOTICE, F("OTA: No new version available."));
      }
    }
  }
  ESP.wdtEnable(WDTO_8S); // Re-enable watchdog
}

void performUpdate(const String &binUrl, const String &fsUrl) {
  WiFiClient client;
  ESPhttpUpdate.rebootOnUpdate(false);
  ESPhttpUpdate.onProgress(otaProgressCallback);

  logMessage(LOG_LEVEL_NOTICE, F("OTA: Update process started."));

  logMessagef(LOG_LEVEL_DEBUG, "Using url: %s for filesystem.", fsUrl.c_str());
  logMessagef(LOG_LEVEL_DEBUG, "Using url: %s for firmware.", binUrl.c_str());

  if (!runFilesystemUpdate(client, fsUrl))
    return;

  if (!runFirmwareUpdate(client, binUrl))
    return;

  broadcastUpdateStatus("SUCCESS", 100, "Update Complete! Rebooting...");
  delay(2000);
  ESP.restart();
}

void otaProgressCallback(size_t current, size_t total) {
  static int lastPercent = -1;
  int percent = (current * 100) / total;
  if (percent != lastPercent) {
    lastPercent = percent;
    broadcastUpdateStatus(currentOtaStage, percent, "");
    yield();
    ESP.wdtFeed();
  }
}

bool runFilesystemUpdate(WiFiClient &client, const String &url) {
  if (url.length() == 0) {
    logMessage(LOG_LEVEL_DEBUG, "Skipping filesystem update.");
    return true; // Optional: skip if URL empty
  }

  logMessage(LOG_LEVEL_DEBUG, "Updating Filesystem...");
  MainConfiguration backup;
  bool hasBackup = backup.loadFromFile("/configuration.json");

  currentOtaStage = "FS_UPDATE";
  broadcastUpdateStatus("FS_START", 0, "Updating Filesystem...");

  if (ESPhttpUpdate.updateFS(client, url) == HTTP_UPDATE_OK) {
    if (hasBackup) {
      LittleFS.begin();
      backup.saveToFile("/configuration.json");
      LittleFS.end();
    }
    return true;
  }

  broadcastUpdateStatus("ERROR", 0, "FS Error: " + ESPhttpUpdate.getLastErrorString());
  return false;
}

bool runFirmwareUpdate(WiFiClient &client, const String &url) {
  if (url.length() == 0) {
    logMessage(LOG_LEVEL_DEBUG, "Skipping firmware update.");
    return true; // Optional: skip if URL empty
  }

  logMessage(LOG_LEVEL_DEBUG, "Updating firmware...");

  currentOtaStage = "BIN_UPDATE";
  broadcastUpdateStatus("BIN_START", 0, "Updating Firmware...");

  if (ESPhttpUpdate.update(client, url) == HTTP_UPDATE_OK) {
    return true;
  }

  broadcastUpdateStatus("ERROR", 0, "Firmware Error: " + ESPhttpUpdate.getLastErrorString());
  return false;
}

// Add this helper to broadcast status to all WS clients
void broadcastUpdateStatus(const String &stage, int progress, const String &message) {
  StaticJsonDocument<384> doc;
  doc["type"] = "ota";
  doc["stage"] = stage;
  doc["progress"] = progress;
  doc["message"] = message;

  JsonObject root = doc.as<JsonObject>();
  fillSystemStatus(root, false); // Frontend widzi np. jak spada Heap podczas OTA!

  String output;
  serializeJsonSmart(doc, output);

  if (ws != nullptr) {
    ws->textAll(output);
  }
  logMessagef((progress % 10 == 0) ? LOG_LEVEL_DEBUG : LOG_LEVEL_VERBOSE, "OTA [%d%%]: %s", progress, message.c_str());
}