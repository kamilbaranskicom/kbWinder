/*
 * @file webserver.ino
 * @brief Asynchronous Web Server and API handlers for kbWinder.
 */

void initializeWebServer() {
  if (server == nullptr)
    server = new AsyncWebServer(80);
  if (ws == nullptr)
    ws = new AsyncWebSocket("/ws");

  initializeWebSockets();

  logMessage(LOG_LEVEL_INFO, F("Initializing HTTP: Async Web Server..."));

  // Registering all URL handlers
  registerRoutes();

  // Start the server
  server->begin();

  logMessage(LOG_LEVEL_NOTICE, F("HTTP: Async Web Server started and listening."));
}

/**
 * @brief Wrapper for authentication logic.
 * If authentication is enabled, checks credentials before executing the handler.
 */
ArHandler withAuth(ArHandler handler) {
  return [handler](AsyncWebServerRequest *request) {
    if (configuration.security.authenticationEnabled) {
      if (!request->authenticate(configuration.security.adminUsername, configuration.security.adminPassword)) {
        return request->requestAuthentication();
      }
    }
    handler(request);
  };
}

/**
 * @brief Wrapper for Demo Mode logic.
 * Blocks access if the system is locked.
 */
ArHandler withLock(ArHandler handler) {
  return [handler](AsyncWebServerRequest *request) {
    if (isSystemLocked()) {
      request->send(403, FPSTR(APPLICATION_JSON), "{\"error\":\"Demo Mode Active\"}");
      return;
    }
    handler(request);
  };
}

/**
 * @brief Maps URL paths to their respective handler functions.
 * Keeps the API structure organized in one place.
 */
void registerRoutes() {
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Origin"), F("*"));
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Methods"), F("GET, POST, PUT, OPTIONS"));
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Headers"), F("Content-Type, Authorization"));

  // helpers
  auto fileHandler = [](AsyncWebServerRequest *request) { handleStaticFile(request); };

  // --- 1. Static Files (public) ---
  server->on("/", HTTP_GET, fileHandler);
  server->on("/index.html", HTTP_GET, fileHandler);
  server->on("/index", HTTP_GET, fileHandler);
  server->on("/setup.html", HTTP_GET, fileHandler);
  server->on("/setup", HTTP_GET, fileHandler);
  server->on("/about.html", HTTP_GET, fileHandler);
  server->on("/about", HTTP_GET, fileHandler);
  server->on("/kbWinder.js", HTTP_GET, fileHandler);
  server->on("/kbWinder.css", HTTP_GET, fileHandler);
  server->on("/variables.h", HTTP_GET, fileHandler);
  server->on("/favicon.svg", HTTP_GET, fileHandler);
  server->on("/ccNames.json", HTTP_GET, fileHandler);
  server->on("/manifest.json", HTTP_GET, fileHandler);
  // TODO: REMOVE THE configuration.json ENTRY for safety
  server->on("/configuration.json", HTTP_GET, withLock(withAuth(fileHandler)));

  // --- 2. Captive Portal & SSDP ---
  // Redirects for internet-sensing devices
  server->on("/generate_204", HTTP_GET, fileHandler);        // Android
  server->on("/fwlink", HTTP_GET, fileHandler);              // Windows
  server->on("/hotspot-detect.html", HTTP_GET, fileHandler); // iOS/macOS
  server->on("/ncsi.txt", HTTP_GET, fileHandler);            // "Microsoft NCSI"
  server->on("/description.xml", HTTP_GET, withLock([](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("text/xml");
    SSDP.schema(*response);
    request->send(response);
  }));

  // --- 3. Control API ---
  server->on("/api/cmd", HTTP_GET, [](AsyncWebServerRequest *request) { handleCommandAsync(request); });

  // --- 4. Data API ---
  server->on("/api/status", HTTP_GET, handleGetStatusAsync);
  server->on("/api/ui-config", HTTP_GET, handleGetConfigurationAsync);     // handleGetConfigurationAsync will not send the
  server->on("/api/configuration", HTTP_GET, handleGetConfigurationAsync); // sensitive data if user is not authorized
  server->on(
      "/api/configuration",
      HTTP_POST,
      [](AsyncWebServerRequest *request) {
    if (configuration.security.authenticationEnabled && !request->authenticate("admin", configuration.security.adminPassword)) {
      return request->requestAuthentication();
    }
      },
      NULL,
      handleSaveConfigurationAsync);
  // server->on("/api/wifi-scan", HTTP_GET, withAuth(withLock(handleWiFiScanAsync)));
  server->on("/api/wifi-scan-start", HTTP_GET, withAuth(withLock(handleWiFiScanStart)));
  server->on("/api/wifi-scan-results", HTTP_GET, withAuth(withLock(handleWiFiScanResults)));
  server->on("/api/reboot", HTTP_GET | HTTP_POST, withAuth(withLock(handleRebootAsync)));
  server->on("/api/restart", HTTP_GET | HTTP_POST, withAuth(withLock(handleRebootAsync)));
  server->on("/api/update", HTTP_GET | HTTP_POST, withAuth(withLock([](AsyncWebServerRequest *request) {
    pendingUpdateRequest = true;
    request->send(200, FPSTR(TEXT_PLAIN), F("OK: Update process initiated. Check logs."));
  })));

  // --- 5. Debug ---
  server->on("/api/netdebug", HTTP_GET, withLock(withAuth([](AsyncWebServerRequest *request) {
    String msg = "Request from: " + request->client()->remoteIP().toString();
    msg += "\nLocal IP: " + WiFi.localIP().toString();
    logMessage(LOG_LEVEL_DEBUG, msg);
    request->send(200, FPSTR(TEXT_PLAIN), msg);
  })));

  /**
   * @brief Allows downloading any file from LittleFS via browser.
   * Usage: http://192.168.x.x/download?file=/config.json
   */
  server->on("/download", HTTP_GET, withAuth([](AsyncWebServerRequest *request) {
    if (request->hasParam("file")) {
      String fileName = request->getParam("file")->value();
      if (LittleFS.exists(fileName)) {
        // "true" as 4th argument forces the browser to download instead of showing
        request->send(LittleFS, fileName, "application/octet-stream", true);
        return;
      }
    }
    request->send(404, "text/plain", "File not found");
  }));
  server->on("/api/list-files", HTTP_GET, withLock(withAuth(handleApiListFiles)));
  server->on("/api/delete-file", HTTP_GET, withLock(withAuth(handleApiFileDelete)));
  // --- Not Found ---
  server->onNotFound(handleNotFoundAsync);
}

void handleNotFoundAsync(AsyncWebServerRequest *request) {
  logMessage(LOG_LEVEL_WARNING, "handleNotFoundAsync - " + request->url());

  // Jeśli jesteśmy w trybie AP, przekieruj na główną (Captive Portal)
  if (WiFi.getMode() & WIFI_AP) {
    // Jeśli to nie jest plik (brak kropki), przekieruj (Captive Portal)
    if (request->url().indexOf('.') == -1) {
      request->redirect("/");
      return;
    }
  }

  handleStaticFile(request, FPSTR(PATH_404_HTML));
}

/**
 * @brief Handler API zwracający listę plików oraz statystyki systemu plików.
 */
void handleApiListFiles(AsyncWebServerRequest *request) {
  // 1. Alokacja dokumentu (4KB wystarczy na ok. 30-40 plików w liście)
  DynamicJsonDocument doc(4096);

  // 2. Pobieranie statystyk systemu plików
  FSInfo fs_info;
  if (LittleFS.info(fs_info)) {
    doc["totalBytes"] = fs_info.totalBytes;
    doc["usedBytes"] = fs_info.usedBytes;
    doc["freeBytes"] = fs_info.totalBytes - fs_info.usedBytes;
    doc["usagePercent"] = (fs_info.usedBytes * 100) / fs_info.totalBytes;
  }

  // 3. Budowanie listy plików (rekurencyjnie)
  JsonArray files = doc.createNestedArray("files");
  walkFS("/", &files);

  // 4. Inteligentna serializacja i wysyłka
  String output;
  serializeJsonSmart(doc, output);
  request->send(200, FPSTR(APPLICATION_JSON), output);
}

/**
 * @brief Handler API do bezpiecznego usuwania plików z LittleFS.
 * Przyjmuje parametr "path" w URL.
 */
void handleApiFileDelete(AsyncWebServerRequest *request) {
  if (!request->hasArg("path")) {
    request->send(400, FPSTR(APPLICATION_JSON), "{\"success\":false,\"error\":\"Missing path\"}");
    return;
  }

  String path = request->arg("path");

  // 1. Sprawdzenie ochrony pliku (używamy naszej tablicy)
  if (isFileProtected(path)) {
    logMessagef(LOG_LEVEL_WARNING, "FS: Access denied for deleting: %s", path.c_str());
    request->send(403, FPSTR(APPLICATION_JSON), "{\"success\":false,\"error\":\"File is protected\"}");
    return;
  }

  // 2. Próba usunięcia
  DynamicJsonDocument doc(256);
  if (LittleFS.exists(path)) {
    if (LittleFS.remove(path)) {
      logMessagef(LOG_LEVEL_NOTICE, "FS: File deleted: %s", path.c_str());
      doc["success"] = true;
    } else {
      doc["success"] = false;
      doc["error"] = "Delete operation failed";
    }
  } else {
    doc["success"] = false;
    doc["error"] = "File not found";
  }

  String output;
  serializeJsonSmart(doc, output);
  request->send(200, FPSTR(APPLICATION_JSON), output);
}

/**
 * @brief Unified handler for manual control via URL path (Async version).
 * @param command The captured string from the URI (e.g., "fast", "slow",
 * "stop").
 */
void handleCommandAsync(AsyncWebServerRequest *request) {
  if (!request->hasParam("cmd")) {
    logMessage(LOG_LEVEL_ERROR, "API command: no cmd.");
    request->send(500, FPSTR(APPLICATION_JSON), "{\"message\":\"no cmd\"}");
    return;
  }
  // 2. Pobieramy wartość parametru
  String command = request->getParam("cmd")->value();

  logMessagef(LOG_LEVEL_DEBUG, "API Control: Received command '%s'", command.c_str());

  // *** SEND THE COMMAND

  sendCommand(command);

  // Budujemy specyficzną odpowiedź, ale używamy fillSystemStatus
  StaticJsonDocument<256> doc;
  JsonObject root = doc.to<JsonObject>();
  root["status"] = "CommandSent";
  root["command"] = command;
  fillSystemStatus(root, false); // Dodaje actualSpeed, heap, uptime automatycznie

  String response;
  serializeJsonSmart(doc, response);
  request->send(200, FPSTR(APPLICATION_JSON), response);

  // Aktualizacja WS dla reszty świata
  sendUnifiedStatus(nullptr, nullptr, true, false);
}

void sendCommand(String command) { logMessage(LOG_LEVEL_SENDCMD, command); }

/**
 * @brief Serves the complete system configuration and metadata as JSON.
 * Masks sensitive passwords and includes system limits for the UI.
 * Uses AsyncResponseStream for memory safety on large JSON payloads.
 */
void handleGetConfigurationAsync(AsyncWebServerRequest *request) {
  bool isAuthorized = false;

  // 1. Sprawdzamy czy autoryzacja w ogóle jest włączona
  if (!configuration.security.authenticationEnabled) {
    isAuthorized = true;
  }
  // 2. Jeśli jest włączona, sprawdź czy użytkownik właśnie podał poprawne hasło
  // lub czy przeglądarka przesłała już zapisane dane Basic Auth
  else if (request->authenticate("admin", configuration.security.adminPassword)) {
    isAuthorized = true;
  }

  logMessagef(LOG_LEVEL_DEBUG, "API: Serving configuration (Authorized: %s)", isAuthorized ? "YES" : "NO");

  // 3. Optymalizacja RAM: mniejszy dokument dla wersji publicznej
  size_t capacity = isAuthorized ? 6144 : 4096;
  DynamicJsonDocument doc(capacity);
  JsonObject root = doc.to<JsonObject>();

  if (doc.capacity() == 0) {
    logMessage(LOG_LEVEL_ERROR, F("API: JSON allocation failed!"));
    request->send(500, FPSTR(TEXT_PLAIN), F("Internal Server Error: Out of memory"));
    return;
  }

  softwareInfo.toJson(root.createNestedObject(F("info")));
  configuration.toJson(root, !isAuthorized, true); // true = mask Passwords, as we're on the web

  // Help JS identify the current IP (important for captive portal redirects)
  root[F("network")][F("activeLocalIp")] = WiFi.localIP().toString();

  if (isAuthorized) {
    // --- 3. METADATA ---
    // JsonObject meta = root.createNestedObject("meta");
    // meta[F("maxMidiControllers")] = LogicConfiguration::MAX_MIDI_CONTROLLERS;
  }

  logMessagef(LOG_LEVEL_DEBUG, PSTR("JSON Capacity: %d, Used: %d"), doc.capacity(), doc.memoryUsage());

  // We use a stream to avoid allocating a massive temporary String
  AsyncResponseStream *response = request->beginResponseStream(FPSTR(APPLICATION_JSON));
  // Write directly to the response stream
  serializeJsonSmart(doc, *response);
  // Finalize the request
  request->send(response);
}

/**
 * @brief Handles POST request to save configuration.
 * Collects chunks of data, parses JSON, and saves to file.
 */
void handleSaveConfigurationAsync(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  // 1. Inicjalizacja bufora przy pierwszym kawałku
  // Używamy statycznego Stringa, aby zbierać dane z kolejnych wywołań callbacka
  static String bodyBuffer = "";
  if (index == 0) {
    bodyBuffer = "";
    bodyBuffer.reserve(total); // Optymalizacja pamięci
    logMessage(LOG_LEVEL_INFO, "API: Receiving new configuration...");
  }

  // 2. Dopisywanie danych do bufora
  for (size_t i = 0; i < len; i++) {
    bodyBuffer += (char)data[i];
  }

  // 3. Sprawdzenie, czy odebraliśmy już wszystko
  if (index + len == total) {
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, bodyBuffer);

    if (error) {
      logMessagef(LOG_LEVEL_ERROR, "API: JSON Parse failed: %s", error.c_str());
      request->send(400, FPSTR(APPLICATION_JSON), "{\"message\":\"Invalid JSON\"}");
      bodyBuffer = ""; // Czyścimy bufor
      return;
    }

    // 4. Walidacja struktury (szukamy klucza "values")
    JsonObject values = doc["values"];
    if (values.isNull()) {
      logMessage(LOG_LEVEL_ERROR, "API: Missing 'values' object in POST");
      request->send(400, FPSTR(APPLICATION_JSON), "{\"message\":\"Missing 'values' object\"}");
      bodyBuffer = "";
      return;
    }

    printSerialLogLevel();
    logMessage(LOG_LEVEL_VERBOSE, bodyBuffer);

    // 5. Aplikowanie konfiguracji
    if (configuration.fromJson(values)) {
      bool saved = configuration.saveToFile();

      if (saved) {
        logMessage(LOG_LEVEL_NOTICE, F("Config: Applying network settings dynamically..."));
        initializeWiFi(false);

        // Budujemy odpowiedź (zgodnie z Twoim wymogiem: status, message, IP)
        DynamicJsonDocument responseDoc(256);
        responseDoc["status"] = "ok";
        responseDoc["message"] = "Configuration saved. Connecting to WiFi in background...";

        String response;
        serializeJsonSmart(responseDoc, response);
        request->send(200, FPSTR(APPLICATION_JSON), response);

        logMessage(LOG_LEVEL_NOTICE, F("Config: Saved. Restart is needed for network/services changes."));

        // Opcjonalny restart (bezpiecznie po rozłączeniu klienta)
        // request->onDisconnect([](){ ESP.restart(); });
      } else {
        request->send(500, FPSTR(APPLICATION_JSON), "{\"status\":\"error\",\"message\":\"Failed to write to FS\"}");
      }
    } else {
      request->send(500, FPSTR(APPLICATION_JSON), "{\"status\":\"error\",\"message\":\"Validation failed\"}");
    }

    bodyBuffer = ""; // Czyszczenie na samym końcu
  }
}

/**
 * @brief Starts an asynchronous WiFi scan.
 */
void handleWiFiScanStart(AsyncWebServerRequest *request) {
  logMessage(LOG_LEVEL_INFO, "WiFi: Starting async scan...");

  // Start scanning asynchronously (true, false -> async, show hidden)
  // To wywołanie zwraca natychmiast WIFI_SCAN_RUNNING (-1)
  int status = WiFi.scanNetworks(true, false);

  DynamicJsonDocument doc(128);
  if (status == WIFI_SCAN_RUNNING) {
    doc["status"] = "scanning";
    request->send(200, FPSTR(APPLICATION_JSON), "{\"status\":\"scanning\"}");
  } else {
    request->send(500, FPSTR(APPLICATION_JSON), "{\"status\":\"failed\"}");
  }
}

/**
 * @brief Checks scan status and returns results when ready.
 */
void handleWiFiScanResults(AsyncWebServerRequest *request) {
  int n = WiFi.scanComplete();

  if (n == WIFI_SCAN_RUNNING) {
    // Skanowanie w toku
    request->send(200, FPSTR(APPLICATION_JSON), "{\"status\":\"scanning\"}");
    return;
  }

  if ((n == WIFI_SCAN_FAILED) || (n < 0)) {
    // Błąd skanowania lub nie zostało uruchomione
    request->send(200, FPSTR(APPLICATION_JSON), "{\"status\":\"failed\"}");
    return;
  }

  // Jeśli n >= 0, mamy wyniki!
  AsyncResponseStream *response = request->beginResponseStream(FPSTR(APPLICATION_JSON));
  DynamicJsonDocument doc(3072);
  JsonObject root = doc.to<JsonObject>();
  root["status"] = "ready";
  JsonArray networks = root.createNestedArray("networks");

  for (int i = 0; i < n; ++i) {
    JsonObject net = networks.createNestedObject();
    net["ssid"] = WiFi.SSID(i);
    net["rssi"] = WiFi.RSSI(i);
    net["secure"] = (WiFi.encryptionType(i) != AUTH_OPEN);
    net["channel"] = WiFi.channel(i);
  }

  serializeJsonSmart(doc, *response);
  request->send(response);

  // Ważne: usuwamy wyniki z pamięci po wysłaniu
  WiFi.scanDelete();
  logMessagef(LOG_LEVEL_DEBUG, "WiFi: Scan results sent to client. Found %d.", n);
}

/**
 * @brief Performs a graceful reboot.
 * Sends the response first, then triggers restart after a short delay.
 */
void handleRebootAsync(AsyncWebServerRequest *request) {
  logMessage(LOG_LEVEL_NOTICE, "System: Reboot requested via Web");

  handleStaticFile(request, "/reboot.html");

  blink(100); // (non-blocking)

  rebootRequestedAt = millis();
  isRebootPending = true;
}

void processPendingReboot() {
  if (isRebootPending) {
    if (millis() - rebootRequestedAt >= 2000) {
      logMessage(LOG_LEVEL_NOTICE, "System: Restarting NOW");
      delay(100);
      ESP.restart();
    }
  }
}

/**
 * @brief Kompleksowa obsługa plików statycznych z obsługą GZIP i logowaniem.
 */
void handleStaticFile(AsyncWebServerRequest *request, String overridePath) {
  String path = (overridePath != "") ? overridePath : request->url();

  if (path == "/" || path == "" || path == "/fwlink" || path == "/hotspot-detect.html" || path == "/generate_204")
    path = "/index.html";

  String remoteBase = configuration.system.webStaticFilesPath;

  // --- TRYB DEWELOPERSKI: Przekierowanie zasobów (JS, CSS, obrazy) ---
  if (remoteBase.length() > 0 && remoteBase.startsWith(F("http"))) {
    // NIE przekierowujemy plików .html (żeby zostać na adresie .local)
    // Przekierowujemy tylko to, co faktycznie edytujesz
    if (path.endsWith(".js") || path.endsWith(".css") || path.endsWith(".svg") || path.endsWith(".png")) {

      String targetUrl = remoteBase;
      if (!targetUrl.endsWith("/") && !path.startsWith("/"))
        targetUrl += "/";
      targetUrl += path;

      logMessagef(LOG_LEVEL_DEBUG, "DEV: Redirecting asset %s -> %s", path.c_str(), targetUrl.c_str());
      request->redirect(targetUrl);
      return;
    }
  }
  // 3. Logika lokalna (LittleFS)
  logMessagef(LOG_LEVEL_DEBUG, "FS: Requesting local: %s", path.c_str());

  if (!fileSystemInitialized) {
    request->send(500, "text/plain", "Internal Server Error: FS NOT INIT");
    return;
  }

  // Inteligentne szukanie plików (.html, .gz)
  String finalPath = path;
  bool found = LittleFS.exists(finalPath);

  if (!found && !finalPath.endsWith(".html") && !finalPath.endsWith(".css") && !finalPath.endsWith(".js")) {
    if (LittleFS.exists(finalPath + ".html")) {
      finalPath += ".html";
      found = true;
    }
  }

  if (!found && !finalPath.endsWith(".gz")) {
    if (LittleFS.exists(finalPath + ".gz")) {
      finalPath += ".gz";
      found = true;
    }
  }

  // 4. Wysyłanie odpowiedzi
  if (found) {
    if (request->hasArg("download")) {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, finalPath, "application/octet-stream");
      response->addHeader("Content-Disposition", "attachment");
      request->send(response);
    } else {
      AsyncWebServerResponse *response = request->beginResponse(LittleFS, finalPath);
      if (overridePath == PATH_404_HTML)
        response->setCode(404);
      request->send(response);
    }
  } else {
    // Obsługa braku pliku
    if (overridePath == PATH_404_HTML) {
      logMessage(LOG_LEVEL_ERROR, "FS: Critical - 404 file missing!");
      request->send(404, "text/html", "<html><body><h1>404 Not Found</h1></body></html>");
    } else {
      logMessagef(LOG_LEVEL_WARNING, "FS: 404: %s", path.c_str());
      handleNotFoundAsync(request);
    }
  }
}

/**
 * @brief Provides a snapshot of the current system state for the UI.
 */
void handleGetStatusAsync(AsyncWebServerRequest *request) {
  sendUnifiedStatus(request, nullptr, false, true);
  // logMessage(LOG_LEVEL_VERBOSE, F("API: Status snapshot sent"));
}
