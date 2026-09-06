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
    void init();
    void stop();
    void update();
    bool isInitialized() const {
        return _initialized;
    }

    static const char* devicePassword();

    void sendLog(const String& msg);

    volatile bool _otaPending = false;
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

    void setupEndpoints();
};

void setupSystemEndpoints(AsyncWebServer* server);
void setupBookEndpoints(AsyncWebServer* server);
void setupSettingsEndpoints(AsyncWebServer* server);