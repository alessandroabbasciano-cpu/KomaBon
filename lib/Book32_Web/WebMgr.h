#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "Config.h"

class AsyncWebServer;
class AsyncWebServerRequest;
class AsyncWebSocket;

class WebMgr {
  public:
    static WebMgr& getInstance();

    void mountFilesystems();

    // Core network lifecycle
    void startNetwork();
    void stopNetwork();
    void resetIdleTimer();

    void update();
    bool isInitialized() const {
        return _initialized;
    }

    static const char* devicePassword();
    void sendLog(const String& msg);
    void sendLogf(const char* format, ...) __attribute__((format(printf, 2, 3)));
    void broadcastSerial(const uint8_t* buffer, size_t size);
    bool isConsoleActive() const;

    volatile bool _otaPending = false;
    volatile bool _debugKeepWifi = false;
    volatile int _pendingRotation = -1;
    volatile int _pendingReaderFontSize = 0;
    volatile int _pendingReaderFontFamily = -1;
    volatile int _pendingAppSwitch = -1;

    AsyncWebSocket* ws;

  private:
    WebMgr();
    AsyncWebServer* server;
    bool _initialized = false;
    bool _endpointsConfigured = false;

    // Power management and watchdog
    unsigned long _lastActivityTime = 0;
    const unsigned long WIFI_TIMEOUT_MS = 300000; // 5 minutes inactivity kill switch

    void setupEndpoints();
};

void setupSystemEndpoints(AsyncWebServer* server);
void setupBookEndpoints(AsyncWebServer* server);
void setupSettingsEndpoints(AsyncWebServer* server);