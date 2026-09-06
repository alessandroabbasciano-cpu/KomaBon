#include "WebMgr.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include "../KomaBon_Core/SettingsStore.h"
#include "../KomaBon_Core/BatteryMgr.h"

void setupSettingsEndpoints(AsyncWebServer* server) {

    server->on("/api/settings/reader", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(256);

        ReaderSettings s = SettingsStore::getInstance().loadReader();
        doc["refreshFrequency"] = s.refreshFrequency;
        doc["fontSize"] = s.fontSize;
        doc["fontFamily"] = s.fontFamily;

        serializeJson(doc, *response);
        request->send(response);
    });

    AsyncCallbackJsonWebHandler* readerSettingsHandler = new AsyncCallbackJsonWebHandler(
        "/api/settings/reader", [](AsyncWebServerRequest* request, JsonVariant& json) {
            bool saved;
            {
                SettingsStore::Transaction tx;
                SettingsStore& store = SettingsStore::getInstance();
                ReaderSettings s = store.loadReader();

                if (json.containsKey("refreshFrequency")) {
                    s.refreshFrequency = json["refreshFrequency"].as<int>();
                }
                if (json.containsKey("fontSize")) {
                    s.fontSize = SettingsStore::clampFontSize(json["fontSize"].as<int>());
                    WebMgr::getInstance()._pendingReaderFontSize = s.fontSize;
                }
                if (json.containsKey("fontFamily")) {
                    s.fontFamily = SettingsStore::clampFontFamily(json["fontFamily"].as<int>());
                    WebMgr::getInstance()._pendingReaderFontFamily = s.fontFamily;
                }

                saved = store.saveReader(s);
            }

            if (saved) {
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(500, "application/json",
                              "{\"status\":\"error\",\"message\":\"Failed to save\"}");
            }
        });
    server->addHandler(readerSettingsHandler);

    server->on("/api/settings/display", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(128);

        doc["rotation"] = SettingsStore::getInstance().loadDisplay().rotation;

        serializeJson(doc, *response);
        request->send(response);
    });

    AsyncCallbackJsonWebHandler* displaySettingsHandler = new AsyncCallbackJsonWebHandler(
        "/api/settings/display", [](AsyncWebServerRequest* request, JsonVariant& json) {
            DisplaySettings s;
            s.rotation = SettingsStore::clampRotation(json["rotation"] | 3);

            if (SettingsStore::getInstance().saveDisplay(s)) {
                WebMgr::getInstance()._pendingRotation = s.rotation;
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(500, "application/json",
                              "{\"status\":\"error\",\"message\":\"Failed to save\"}");
            }
        });
    server->addHandler(displaySettingsHandler);

    server->on("/api/settings/sleep", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(512);

        SleepSettings s = SettingsStore::getInstance().loadSleep();
        doc["sleepTimeout"] = s.timeout;
        doc["sleepMessage"] = s.message;

        serializeJson(doc, *response);
        request->send(response);
    });

    AsyncCallbackJsonWebHandler* sleepSettingsHandler = new AsyncCallbackJsonWebHandler(
        "/api/settings/sleep", [](AsyncWebServerRequest* request, JsonVariant& json) {
            bool saved;
            {
                SettingsStore::Transaction tx;
                SettingsStore& store = SettingsStore::getInstance();
                SleepSettings s = store.loadSleep();

                if (json.containsKey("sleepTimeout")) {
                    s.timeout = json["sleepTimeout"].as<int>();
                }
                if (json.containsKey("sleepMessage")) {
                    s.message = json["sleepMessage"].as<String>();
                }

                saved = store.saveSleep(s);
            }

            if (saved) {
                BatteryMgr::getInstance().loadSleepSettings();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(500, "application/json",
                              "{\"status\":\"error\",\"message\":\"Failed to save\"}");
            }
        });
    server->addHandler(sleepSettingsHandler);
}