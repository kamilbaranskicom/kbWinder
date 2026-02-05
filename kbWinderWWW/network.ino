/**
 * @file network.ino
 * @brief Asynchronous network management for kbWinder.
 */
#include "ESP8266WiFi.h"
#include <ESPAsyncWebServer.h>
struct AsyncWebServerRequest; // Forward declaration
#include <lwip/netif.h>

#include "configuration.h"
#include "network.h"

AsyncWebServer *server = nullptr;
AsyncWebSocket *ws = nullptr;
DNSServer *dnsServer = nullptr;

unsigned long connectionStartTime = 0;
bool isStationConnecting = false;
bool networkInitialized = false;
uint32_t rebootRequestedAt = 0;
bool isRebootPending = false;

static unsigned long lastMinimalBroadcast = 0;
static unsigned long lastFullBroadcast = 0;

const byte DNS_PORT = 53;

String macAddress;

void disableWiFi() { WiFi.mode(WIFI_OFF); }

/**
 * @brief Initializes the network hardware and starts connection process.
 * Sets up Access Point immediately and starts background Station connection.
 */
void initializeNetwork() {
  logMessage(LOG_LEVEL_INFO, F("Initializing network..."));
  initializeWiFi(true);
  initializeDNSserver();
  initializeMDNS();
  initializeNBS();
  initializeSSDP();
#ifdef PUSHOTA
  initializePushOTA();
#endif
}

void initializeDNSserver() {
  if (!configuration.system.dnsServerEnabled) {
    logMessage(LOG_LEVEL_DEBUG, F("DNS disabled"));
    return;
  }

  // Start Captive Portal only if AP is active
  if (WiFi.getMode() & WIFI_AP == 0) {
    logMessage(LOG_LEVEL_DEBUG, F("DNS disabled due to WiFi mode"));
    return;
  }

  if (dnsServer == nullptr) {
    dnsServer = new DNSServer();
  }

  // "*" means redirect ALL domains to our AP IP
  dnsServer->start(DNS_PORT, "*", configuration.network.accessPointIp);
  logMessage(LOG_LEVEL_INFO, F("DNS Captive Portal started (all requests redirected to AP IP)"));
}

void initializeMDNS() {
  if (!configuration.system.mdnsEnabled) {
    logMessage(LOG_LEVEL_DEBUG, F("mDNS disabled"));
    return;
  }

  if (MDNS.begin(configuration.system.hostName)) {
    MDNS.addService("http", "tcp", 80);
    logMessage(LOG_LEVEL_INFO, "mDNS responder started: http://" + String(configuration.system.hostName) + ".local");
  }
}

void initializeNBS() {
  if (!configuration.system.nbsEnabled) {
    logMessage(LOG_LEVEL_DEBUG, F("NETBIOS disabled"));
    return;
  }

  if (NBNS.begin(configuration.system.hostName)) {
    logMessage(LOG_LEVEL_INFO, F("NETBIOS started."));
  }
}

#ifdef PUSHOTA
void initializePushOTA() {
  if (!configuration.system.pushOTAEnabled) {
    logMessage(LOG_LEVEL_DEBUG, F("Push OTA disabled"));
    return;
  }

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.setHostname(configuration.system.hostName);

  ArduinoOTA.onStart([]() { logMessage(LOG_LEVEL_INFO, F("Start")); });
  ArduinoOTA.onEnd([]() { logMessage(LOG_LEVEL_INFO, F("\nEnd")); });
  ArduinoOTA.onProgress(
      [](unsigned int progress, unsigned int total) { logMessagef(LOG_LEVEL_INFO, "Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error) {
    logMessagef(LOG_LEVEL_ERROR, "Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      logMessage(LOG_LEVEL_ERROR, F("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR)
      logMessage(LOG_LEVEL_ERROR, F("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR)
      logMessage(LOG_LEVEL_ERROR, F("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR)
      logMessage(LOG_LEVEL_ERROR, F("Receive Failed"));
    else if (error == OTA_END_ERROR)
      logMessage(LOG_LEVEL_ERROR, F("End Failed"));
  });

  ArduinoOTA.begin();
  logMessage(LOG_LEVEL_INFO, F("Push OTA enabled"));
}
#endif

void initializeWiFi(bool firsttime) {
  if (firsttime) {
    WiFi.softAPdisconnect(true);
    WiFi.persistent(false);
  }

  macAddress = WiFi.macAddress();
  logMessage(LOG_LEVEL_INFO, F("ESP Board MAC Address: ") + macAddress);
  macAddress.replace(":", "");
  logMessage(LOG_LEVEL_NOTICE, (String) "(" + macAddress + ")");

  // 0. Turn off power saving - better WWW speed
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.hostname(configuration.system.hostName);

  // 1. Set wifi mode
  if (configuration.network.wifiMode == 1) { // Always AP
    WiFi.mode(WIFI_AP);
    logMessage(LOG_LEVEL_NOTICE, F("WiFi Mode: Access Point Only"));
  } else if (configuration.network.wifiMode == 2) { // Always STA
    WiFi.mode(WIFI_STA);
    logMessage(LOG_LEVEL_NOTICE, F("WiFi Mode: Station Only"));
  } else {
    WiFi.mode(WIFI_AP_STA); // STA with AP fallback (default)
    logMessage(LOG_LEVEL_NOTICE, F("WiFi Mode: Dual (AP + Station)"));
  }

  // 2. Configure and start Access Point (The "Service" network)
  // This starts immediately so the user can always access settings
  // 2. Configure and start Access Point (The "Service" network)
  if (configuration.network.wifiMode != 2) {
    // Sprawdzamy, czy AP już działa z takimi samymi parametrami
    bool needsUpdate = false;

    if (!(WiFi.getMode() & WIFI_AP)) {
      needsUpdate = true; // AP w ogóle nie jest włączone
    } else if (WiFi.softAPSSID() != configuration.network.accessPointSsid) {
      needsUpdate = true; // Zmieniła się nazwa sieci
    } else if (WiFi.softAPIP() != configuration.network.accessPointIp) {
      needsUpdate = true; // Zmieniło się IP
      // } else if (WiFi.softAPSubnetMask() != configuration.network.accessPointMask) {
      //   needsUpdate = true; // Zmieniła się maska
    }

    if (needsUpdate) {
      logMessage(LOG_LEVEL_NOTICE, F("AP: Config changed or not active. Re-initializing..."));

      WiFi.softAPConfig(configuration.network.accessPointIp, configuration.network.accessPointIp, configuration.network.accessPointMask);

      WiFi.softAP(configuration.network.accessPointSsid, configuration.network.accessPointPassword);
      logMessagef(LOG_LEVEL_NOTICE, "Access Point started with SSID: %s", configuration.network.accessPointSsid);
      logMessagef(LOG_LEVEL_DEBUG,
          PSTR("Config AP IP: %s, mask: %s"),
          configuration.network.accessPointIp.toString().c_str(),
          configuration.network.accessPointMask.toString().c_str());
      logMessagef(LOG_LEVEL_NOTICE, "Access Point started with SSID: %s", configuration.network.accessPointSsid);
      logMessage(LOG_LEVEL_NOTICE, "Access Point started at IP: " + WiFi.softAPIP().toString());

    } else {
      logMessage(LOG_LEVEL_DEBUG, F("AP: Already running with correct settings. Skipping re-init to keep connection alive."));
    }
  }

  // 3. Start Station connection (Connecting to home/studio router)
  if (configuration.network.wifiMode == 0 || configuration.network.wifiMode == 2) {
    if (strlen(configuration.network.stationSsid) > 0) {
      logMessage(LOG_LEVEL_NOTICE, "Connecting to WiFi as STA: " + String(configuration.network.stationSsid));

      if (!configuration.network.stationDhcpEnabled) {
        WiFi.config(configuration.network.stationStaticIp,
            configuration.network.stationStaticGateway,
            configuration.network.stationStaticMask,
            IPAddress(8, 8, 8, 8));
        logMessagef(LOG_LEVEL_INFO,
            "Using STA IP %s / mask %s / gw %s.",
            configuration.network.stationStaticIp,
            configuration.network.stationStaticMask,
            configuration.network.stationStaticGateway);
      } else {
        logMessage(LOG_LEVEL_INFO, F("Using DHCP for STA."));
      }

      WiFi.begin(configuration.network.stationSsid, configuration.network.stationPassword);
      connectionStartTime = millis();
      isStationConnecting = true;
    } else {
      logMessage(LOG_LEVEL_NOTICE, F("Skipping connection to WiFi as STA: ssid not configured"));
    }
  }
}

/**
 * @brief Non-blocking network task handler. Must be called in main loop().
 */
void processNetworkTasks() {
  // Handle Web Server requests <-- not needed, as AsyncWebServer deos it by himself.
  // server.handleClient();

#ifdef PUSHOTA
  processPushOTA();
#endif

  processDNSServer();
  processMDNS();
  processWiFiConnection();
  handleStatusBroadcasts();
}

void processWiFiConnection() {
  // Jeśli jesteśmy w trybie "Tylko AP" lub nie mamy wpisanego SSID - nic nie rób
  if (configuration.network.wifiMode == 1 || strlen(configuration.network.stationSsid) == 0) {
    return;
  }

  if (isStationConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      isStationConnecting = false;
      logMessagef(LOG_LEVEL_NOTICE, "Connected to WiFi %s! IP: %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      logMessagef(LOG_LEVEL_INFO, "STA GW: %s, Mask: %s", WiFi.gatewayIP().toString().c_str(), WiFi.subnetMask().toString().c_str());
      debugNetworkInterfaces();
      pendingIpNotify = true;
      // BROADCAST NEW IP via WebSocket immediately
      sendUnifiedStatus(nullptr, nullptr, true, true);
      WiFi.setSleepMode(WIFI_NONE_SLEEP);
      WiFi.setOutputPower(20.5); // Maksymalna moc nadawania
      blink(2);
    } else if (millis() - connectionStartTime > 15000) { // 15s timeout
      isStationConnecting = false;
      logMessage(LOG_LEVEL_WARNING, F("WiFi Connection failed (Timeout)."));
    }
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    // Only log "lost" if we were actually connected before (e.g., uptime > 30s)
    if (millis() > 30000) {
      logMessage(LOG_LEVEL_INFO, F("WiFi connection lost. Retrying..."));
    }

    WiFi.begin(configuration.network.stationSsid, configuration.network.stationPassword);
    connectionStartTime = millis();
    isStationConnecting = true;
  }
}

void applyWiFiConfiguration() {
  logMessage(LOG_LEVEL_NOTICE, F("Applying new WiFi configuration..."));

  // 1. Update Hostname
  WiFi.hostname(configuration.system.hostName);

  // 2. Handle Station (STA)
  if (strlen(configuration.network.stationSsid) > 0) {
    logMessage(LOG_LEVEL_INFO, F("Starting connection to: ") + String(configuration.network.stationSsid));

    if (!configuration.network.stationDhcpEnabled) {
      WiFi.config(
          configuration.network.stationStaticIp, configuration.network.stationStaticGateway, configuration.network.stationStaticMask);
    }

    WiFi.begin(configuration.network.stationSsid, configuration.network.stationPassword);
    connectionStartTime = millis();
    isStationConnecting = true;
  } else {
    WiFi.disconnect(true); // Power off STA if SSID is empty
    logMessage(LOG_LEVEL_INFO, F("Station mode disabled (empty SSID)."));
  }

  // 3. AP is usually already running, but we can update its settings if needed
  // softAPConfig/softAP can be called again without breaking current WS connections
  WiFi.softAP(configuration.network.accessPointSsid, configuration.network.accessPointPassword);
}

#ifdef PUSHOTA
void processPushOTA() {
  // Handle OTA updates
  if (!configuration.system.pushOTAEnabled)
    return;

  ArduinoOTA.handle();
}
#endif

void processDNSServer() {
  if ((dnsServer == nullptr) || (!configuration.system.dnsServerEnabled))
    return;

  // DNS Task - handle redirection
  if (WiFi.getMode() & WIFI_AP) {
    dnsServer->processNextRequest();
  }
}

void processMDNS() {
  if (!configuration.system.mdnsEnabled)
    return;

  // Handle mDNS
  MDNS.update();
}

void debugNetworkInterfaces() {
  logMessage(LOG_LEVEL_DEBUG, F("--- lwIP Interface Debug ---"));

  struct netif *netif;
  // netif_list to globalna lista interfejsów w lwIP
  for (netif = netif_list; netif != NULL; netif = netif->next) {
    char name[3] = {netif->name[0], netif->name[1], '\0'};

    // W Twojej wersji lwIP pola .addr są dostępne bezpośrednio
    String ip = IPAddress(netif->ip_addr.addr).toString();
    String gw = IPAddress(netif->gw.addr).toString();
    String mask = IPAddress(netif->netmask.addr).toString();

    logMessagef(LOG_LEVEL_DEBUG, "IF: %s%d | IP: %s | GW: %s | Mask: %s", name, netif->num, ip.c_str(), gw.c_str(), mask.c_str());

    if (netif == netif_default) {
      logMessagef(LOG_LEVEL_DEBUG, "DEFAULT INTERFACE: %s%d", name, netif->num);
    }
  }
  logMessage(LOG_LEVEL_DEBUG, "---------------------------");
  logMessagef(LOG_LEVEL_DEBUG,
      PSTR("Network: IP: %s, GW: %s, DNS: %s"),
      WiFi.localIP().toString().c_str(),
      WiFi.gatewayIP().toString().c_str(),
      WiFi.dnsIP().toString().c_str());
}

void initializeSSDP() {
  if (!configuration.system.ssdpEnabled) {
    logMessage(LOG_LEVEL_DEBUG, F("SSDP disabled"));
    return;
  }

  logMessage(LOG_LEVEL_INFO, F("Starting SSDP responder..."));

  SSDP.setSchemaURL("description.xml");
  SSDP.setHTTPPort(80);
  SSDP.setName(configuration.system.hostName);

  // WiFi.macAddress() returns String, SSDP needs const char*
  SSDP.setSerialNumber(WiFi.macAddress().c_str());

  SSDP.setURL("index.html"); // Strona, która otworzy się po dwukrotnym kliknięciu
  SSDP.setModelName(softwareInfo.name);

  // FIX: Convert uint8_t version to String then to const char*
  SSDP.setModelNumber(String(configuration.system.version).c_str());

  SSDP.setManufacturer(softwareInfo.author);
  SSDP.setManufacturerURL(softwareInfo.productUrl);
  SSDP.setDeviceType("upnp:rootdevice"); // Standardowy typ urządzenia

  SSDP.begin();
  logMessage(LOG_LEVEL_NOTICE, F("SSDP responder is active."));
}
