/**
 * @brief Initialize WebSockets
 */
void initializeWebSockets() {
  // Rejestracja handlera WebSocket
  if (server != nullptr && ws != nullptr) {
    ws->onEvent(onWsEvent);
    server->addHandler(ws);

    logMessage(LOG_LEVEL_INFO, F("WebSocket: Server initialized at /ws"));
  } else {
    logMessage(LOG_LEVEL_ERROR, F("WebSocket: Failed to initialize."));
  }
}

/**
 * @brief Handler zdarzeń WebSocket
 */
void onWsEvent(AsyncWebSocket *wsInstance, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    logMessagef(LOG_LEVEL_DEBUG, "WebSocket: Client #%u connected", client->id());
    // Po połączeniu wyślij aktualny status, żeby UI od razu się zsynchronizowało
    sendUnifiedStatus(nullptr, client, false, false);
  } else if (type == WS_EVT_DISCONNECT) {
    logMessagef(LOG_LEVEL_DEBUG, "WebSocket: Client #%u disconnected", client->id());
  }
}

void handleStatusBroadcasts() {
  unsigned long now = millis();

  // 1. Co 1 sekundę - status minimalny (prędkość, RSSI, LEDy pedałów)
  if (now - lastMinimalBroadcast >= 1000) {
    lastMinimalBroadcast = now;
    // Broadcast = true, forceFull = false (minimalny)
    // Ale jeśli pendingIpNotify jest true, funkcja sama przełączy na Full!
    sendUnifiedStatus(nullptr, nullptr, true, false);
  }

  // 2. Co 60 sekund - pełne odświeżenie (IP, SSID, konfiguracja) na wszelki wypadek
  if (now - lastFullBroadcast >= 60000) {
    lastFullBroadcast = now;
    sendUnifiedStatus(nullptr, nullptr, true, true);
  }
}

void broadcastLog(LogLevel level, const String &message) {
  if (ws == nullptr || ws->count() == 0)
    return;

  StaticJsonDocument<MAX_LOG_LINE_LENGTH + 1024> doc;
  doc[F("type")] = F("log");
  doc[F("level")] = getLogLevelName(level); // Zwraca __FlashStringHelper*
  doc[F("message")] = message;

  char buffer[MAX_LOG_LINE_LENGTH + 1024];

  size_t len = serializeJsonSmart(doc, buffer, sizeof(buffer));

  // zamiast tego:
  // ws->textAll(buffer, len);
  // to co poniżej:
  //  Iterujemy ręcznie po klientach zamiast używać ślepego textAll
  for (auto const &client : ws->getClients()) {
    if (client->status() == WS_CONNECTED) {

      // TO JEST KLUCZ: Czekamy, aż konkretny klient będzie mógł przyjąć dane
      unsigned long startWait = millis();
      while (!client->canSend()) {
        yield(); // Pozwól ESP obsłużyć WiFi w tle

        // Safety timeout - jeśli klient "zamarzł", nie blokuj maszyny na wieki
        if (millis() - startWait > 150) {
          // Serial.println("WS: Client buffer timeout - dropping frame");
          break;
        }
      }

      // Wysyłamy tylko do tego klienta, wiedząc że ma miejsce w kolejce
      client->text(buffer, len);
    }
  }
}

/**
 * @brief Wypełnia obiekt JSON wspólnymi metrykami systemu.
 * Dzięki przekazaniu referencji do JsonObject, możemy użyć tego
 * zarówno dla małych StaticJsonDocument jak i dużych DynamicJsonDocument.
 */
void fillSystemStatus(JsonObject &root, bool full) {
  root[F("type")] = "status";
  root[F("freeHeap")] = ESP.getFreeHeap();
  root[F("uptime")] = millis() / 1000;
  root[F("wifiRSSI")] = WiFi.RSSI();
  root[F("webDebugEnabled")] = configuration.system.webDebugEnabled;

  if (configuration.system.webDebugEnabled) {
    root[F("heapFragmentation")] = ESP.getHeapFragmentation();
    root[F("sketchSize")] = ESP.getSketchSize();
    root[F("cpuFreqMHz")] = ESP.getCpuFreqMHz();
    full = true;
  }

  // Create array for all pedal controllers
  JsonArray pedals = root.createNestedArray(F("pedals"));

  /*
  for (int i = 0; i < LogicConfiguration::MAX_PEDAL_CONTROLLERS; i++) {
    auto &pedalConfig = configuration.logic.pedalControllers[i];
    JsonObject pObj = pedals.createNestedObject();

    // Standard status for all pedals
    pObj[F("id")] = i;

    // Raw hardware state (useful for "LED" indicators in UI)
    bool tip = !digitalRead(pedalConfig.hardwarePinPrimary);
    bool ring = (pedalConfig.hardwarePinSecondary > 0) ? !digitalRead(pedalConfig.hardwarePinSecondary) : false;

    pObj[F("tip")] = tip;
    pObj[F("ring")] = ring;
    pObj[F("detectedType")] = (int)activePedalType[i];

    if (full) {
      pObj[F("enabled")] = pedalConfig.enabled;
      // Detailed debug info per pedal
      if (configuration.system.webDebugEnabled) {
        pObj[F("activeName")] = getPedalName(activePedalType[i]); // String name for easier log reading
        pObj[F("lastReq")] = speedNames[lastHardwareRequestedSpeed[i]];
        pObj[F("pending")] = speedNames[pendingSpeed[i]];
        pObj[F("isPressed")] = physicalWasPressed[i];
        pObj[F("lastChange")] = lastStateChangeTime[i];
        pObj[F("mode")] = (int)pedalConfig.controlMode;
      }
    }
  }
  */

  if ((full) || (pendingIpNotify)) {
    if (configuration.system.webDebugEnabled) {
      // root[F("lastExcludingStop")] = speedNames[lastSpeedExcludingStop];
    }
    if (configuration.system.mdnsEnabled) {
      root[F("hostname")] = String(configuration.system.hostName) + ".local";
    }
    root[F("staSSID")] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "";
    root[F("ipSTA")] = WiFi.localIP() ? WiFi.localIP().toString() : "0.0.0.0";
    root[F("apSSID")] = WiFi.softAPSSID() ? WiFi.softAPSSID() : "";
    root[F("ipAP")] = WiFi.softAPIP() ? WiFi.softAPIP().toString() : "0.0.0.0";

    // RESET the flag immediately after it's packed into the JSON
    if (pendingIpNotify) {
      root[F("justConnected")] = pendingIpNotify;
      // flagę kasujemy w sendUnifiedStatus() po faktycznym wysłaniu do wszystkich
    }
  }
}

/**
 * @brief Wysyła status do HTTP, konkretnego klienta WS lub wszystkich przez WS.
 * @param request Jeśli podany, wyśle odpowiedź HTTP.
 * @param client Jeśli podany, wyśle do danego klienta WS
 * @param broadcast Jeśli true, roześle status do wszystkich przez WS.
 * @param forceFull Jeśli true, wymusi pełny status niezależnie od kontekstu.
 */
void sendUnifiedStatus(
    AsyncWebServerRequest *request = nullptr, AsyncWebSocketClient *client = nullptr, bool broadcast = false, bool forceFull = false) {
  // Decydujemy czy wysyłamy pełny status
  // 1. Jeśli to żądanie HTTP (/api/status) -> FULL
  // 2. Jeśli podłączył się nowy klient WS -> FULL
  // 3. Jeśli mamy flagę pendingIpNotify -> FULL
  // 4. Jeśli wymuszono (forceFull) -> FULL
  bool sendFull = (request != nullptr) || (client != nullptr) || pendingIpNotify || forceFull;

  DynamicJsonDocument doc(sendFull ? 4096 : 1024);
  JsonObject root = doc.to<JsonObject>();

  fillSystemStatus(root, sendFull);

  String output;
  serializeJsonSmart(doc, output);

  // 1. Odpowiedź na żądanie HTTP
  if (request != nullptr) {
    request->send(200, FPSTR(APPLICATION_JSON), output);
  }

  // 2. Wysyłka do konkretnego klienta WebSocket
  if (client != nullptr) {
    client->text(output);
  }

  // 3. Rozgłoszenie do wszystkich (broadcast)
  if (broadcast && ws != nullptr) {
    ws->textAll(output);
    // SKASOWANIE FLAGI dopiero tutaj - po rozesłaniu do wszystkich
    if (sendFull && pendingIpNotify) {
      pendingIpNotify = false;
    }
  }
}