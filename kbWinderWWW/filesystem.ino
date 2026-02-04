/**
   filesystem
*/
#include "filesystem.h"

void initializeFileSystem() {
  fileSystemConfig.setAutoFormat(false);
  fileSystem->setConfig(fileSystemConfig);

  // fileSystem->format();

  fileSystemInitialized = fileSystem->begin();

  logMessage(LOG_LEVEL_INFO, fileSystemInitialized ? F("Filesystem initialized.") : F("Filesystem init failed!"));
  if (configuration.system.serialLogLevel >= LOG_LEVEL_VERBOSE)
    listDirRecursive("/");
}

bool isSystemLocked() {
  if (LittleFS.exists(F("/demo.lock"))) {
    logMessage(LOG_LEVEL_DEBUG, F("Demo restriction in effect."));
    return true;
  };
  return false;
}

void checkDemoMode() {
  if (isSystemLocked()) {
    logMessage(LOG_LEVEL_NOTICE, F("[DEMO] Mode restriction is on."));
  } else {
    logMessage(LOG_LEVEL_INFO, F("Demo mode is off, configuration is writable."));
  }
}

/**
 * Enables Demo Mode by creating the lock file.
 */
void setDemoOn() {
  if (isSystemLocked()) {
    logMessage(LOG_LEVEL_NOTICE, F("[DEMO] Mode is already ON."));
    return;
  }

  File f = LittleFS.open("/demo.lock", "w");
  if (f) {
    f.print("locked");
    f.close();
    logMessage(LOG_LEVEL_NOTICE, F("[DEMO] Mode ENABLED. Config is now read-only."));
  } else {
    logMessage(LOG_LEVEL_ERROR, F("[DEMO] Error turning demo mode on."));
  }
}

/**
 * Disables Demo Mode by removing the lock file.
 */
void setDemoOff() {
  if (!isSystemLocked()) {
    logMessage(LOG_LEVEL_NOTICE, F("[DEMO] Mode is already OFF."));
    return;
  }

  if (LittleFS.remove("/demo.lock")) {
    logMessage(LOG_LEVEL_NOTICE, F("[DEMO] Mode DISABLED. Config is now writable."));
  } else {
    logMessage(LOG_LEVEL_ERROR, F("[DEMO] Error turning demo mode off."));
  }
}

/**
 * @brief Kopiuje plik w systemie LittleFS.
 * @param sourcePath Ścieżka źródłowa (np. "/config.json")
 * @param destPath Ścieżka docelowa (np. "/config.bak")
 * @return bool True jeśli kopiowanie zakończyło się sukcesem.
 */
bool copyFile(const String &sourcePath, const String &destPath) {
  if (!LittleFS.exists(sourcePath)) {
    logMessagef(LOG_LEVEL_ERROR, "FS: Copy failed - source %s does not exist", sourcePath.c_str());
    return false;
  }

  File srcFile = LittleFS.open(sourcePath, "r");
  File dstFile = LittleFS.open(destPath, "w");

  if (!srcFile || !dstFile) {
    logMessage(LOG_LEVEL_ERROR, "FS: Failed to open files for copying");
    if (srcFile)
      srcFile.close();
    if (dstFile)
      dstFile.close();
    return false;
  }

  // Używamy bufora, aby przyspieszyć proces i oszczędzać Flash
  uint8_t buffer[512];
  size_t bytesRead;
  size_t totalBytes = 0;

  while ((bytesRead = srcFile.read(buffer, sizeof(buffer))) > 0) {
    dstFile.write(buffer, bytesRead);
    totalBytes += bytesRead;
  }

  srcFile.close();
  dstFile.close();

  logMessagef(LOG_LEVEL_INFO, "FS: Copied %d bytes from %s to %s", totalBytes, sourcePath.c_str(), destPath.c_str());
  return true;
}

/**
 * @brief Zarządza rotacją backupów w folderze /backup.
 * Przesuwa pliki o jeden numer w górę i kopiuje aktualny plik na pozycję 0.
 */
void rotateAndCreateBackup(const char *filename) {
  const int maxBackups = 10;
  const char *backupDir = "/backup";

  // 1. Upewnij się, że folder istnieje
  if (!LittleFS.exists(backupDir)) {
    LittleFS.mkdir(backupDir);
  }

  String baseName = String(filename);
  if (baseName.startsWith("/"))
    baseName = baseName.substring(1);

  // 2. Usuń najstarszy backup (nr 9)
  String oldestBackup = String(backupDir) + "/" + baseName + ".9";
  if (LittleFS.exists(oldestBackup)) {
    LittleFS.remove(oldestBackup);
  }

  // 3. Przesuń pliki: 8 -> 9, 7 -> 8 ... 0 -> 1
  for (int i = maxBackups - 2; i >= 0; i--) {
    String oldName = String(backupDir) + "/" + baseName + "." + String(i);
    String newName = String(backupDir) + "/" + baseName + "." + String(i + 1);
    if (LittleFS.exists(oldName)) {
      LittleFS.rename(oldName, newName);
    }
  }

  // 4. Skopiuj aktualny (właśnie zapisany) plik jako backup.0
  String newestBackup = String(backupDir) + "/" + baseName + ".0";
  copyFile(filename, newestBackup);
}

String formatTimestamp(time_t t) {
  if (t == 0)
    return "N/A";
  struct tm *tmstruct = localtime(&t);
  char buf[25];
  sprintf(buf,
      "%d-%02d-%02d %02d:%02d:%02d",
      (tmstruct->tm_year) + 1900,
      (tmstruct->tm_mon) + 1,
      tmstruct->tm_mday,
      tmstruct->tm_hour,
      tmstruct->tm_min,
      tmstruct->tm_sec);
  return String(buf);
}

/**
 * @brief Rekurencyjnie przeszukuje system plików.
 * @param path Ścieżka do przeszukania.
 * @param jsonArr Opcjonalny wskaźnik do tablicy JSON.
 */
void walkFS(String path, JsonArray *jsonArr = nullptr) {
  Dir dir = LittleFS.openDir(path);

  while (dir.next()) {
    String fileName = dir.fileName();
    String fullPath = path + fileName;

    if (dir.isDirectory()) {
      // Logowanie folderu do konsoli (VERBOSE)
      logMessagef(LOG_LEVEL_VERBOSE, " DIR : %s", fullPath.c_str());

      // Rekurencyjne wejście do podfolderu
      walkFS(fullPath + "/", jsonArr);
    } else {
      // Logowanie pliku do konsoli
      size_t fileSize = dir.fileSize();

      // Opcjonalnie: pobranie czasu ostatniej zmiany (wymaga LittleFS)
      File f = dir.openFile("r");
      time_t lw = f.getLastWrite();
      f.close();

      logMessagef(LOG_LEVEL_VERBOSE, "  FILE: %-25s SIZE: %-7d", fullPath.c_str(), fileSize);

      if (jsonArr) {
        JsonObject fileObj = jsonArr->createNestedObject();
        fileObj["name"] = fullPath;
        fileObj["size"] = fileSize;
        fileObj["lastWrite"] = lw;
      }
    }
  }
}

void listDirRecursive(const char *dirname) {
  logMessagef(LOG_LEVEL_VERBOSE, "Listing FS from: %s", dirname);
  walkFS(dirname);
  logMessage(LOG_LEVEL_VERBOSE, "--- End of listing ---");
}