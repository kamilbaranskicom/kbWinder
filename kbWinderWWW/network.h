#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <AsyncWebSocket.h>
#include <DNSServer.h>
#include <ESP8266NetBIOS.h>
#include <ESP8266SSDP.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <functional>

#ifdef PUSHOTA
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#endif

#include <ArduinoJson.h>

bool pendingIpNotify = false; // Flag to indicate a fresh connection event

extern AsyncWebServer *server;
extern AsyncWebSocket *ws;
extern DNSServer *dnsServer;
extern bool pendingUpdateRequest;
extern bool networkInitialized;
extern uint32_t rebootRequestedAt;
extern bool isRebootPending;

typedef std::function<void(AsyncWebServerRequest *)> ArHandler;

ArHandler withAuth(ArHandler handler);

void handleStaticFile(AsyncWebServerRequest *request, String overridePath = "");
void handleControlCommandAsync(AsyncWebServerRequest *request, String command);
void handleGetConfigurationAsync(AsyncWebServerRequest *request);
void handleSaveConfigurationAsync(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void handleWiFiScanAsync(AsyncWebServerRequest *request);
void handleGetStatusAsync(AsyncWebServerRequest *request);
void handleNotFoundAsync(AsyncWebServerRequest *request);
void handleRebootAsync(AsyncWebServerRequest *request);
void onWsEvent(AsyncWebSocket *wsInstance, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

void initializeNetwork();
void processNetworkTasks();
void initializeWebServer();
void registerRoutes();

void sendUnifiedStatus(AsyncWebServerRequest *request, AsyncWebSocketClient *client, bool broadcast, bool forceFull);

#endif // NETWORK_H