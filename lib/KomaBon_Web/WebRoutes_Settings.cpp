#include "WebMgr.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <SD.h>
#include "../KomaBon_Core/KomaBonFS.h"
#include "../KomaBon_Core/SettingsStore.h"
#include "../KomaBon_Core/BatteryMgr.h"

struct ScreensaverUploadState {
    AsyncWebServerRequest* owner = nullptr;
    File file;
    size_t bytesWritten = 0;
    bool ok = false;
    String error;

    void reset() {
        if (file) file.close();
        owner = nullptr;
        bytesWritten = 0;
        ok = false;
        error = "";
    }
};

static ScreensaverUploadState g_screensaverUpload;

void setupSettingsEndpoints(AsyncWebServer* server) {

    server->on("/api/settings/reader", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(256);

        ReaderSettings s = SettingsStore::getInstance().loadReader();
        doc["refreshFrequency"] = s.refreshFrequency;
        doc["fontSize"] = s.fontSize;
        doc["fontFamily"] = s.fontFamily;
        doc["margin"] = s.margin;

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
                if (json.containsKey("margin")) {
                    s.margin = SettingsStore::clampMargin(json["margin"].as<int>());
                    WebMgr::getInstance()._pendingReaderMargin = s.margin;
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
        doc["screenMode"] = s.screenMode;
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
                if (json.containsKey("screenMode")) {
                    s.screenMode = json["screenMode"].as<int>();
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

    server->on("/api/settings/screensaver", HTTP_GET, [](AsyncWebServerRequest* request) {
        bool hasCustom = false;
        size_t fileSize = 0;
        fs::FS* targetFS = nullptr;
        String filePath = "";

        if (EbookFS.exists("/screensaver.raw")) {
            hasCustom = true;
            targetFS = &EbookFS;
            filePath = "/screensaver.raw";
        } else if (EbookFS.exists("/screensavers/sleep.raw")) {
            hasCustom = true;
            targetFS = &EbookFS;
            filePath = "/screensavers/sleep.raw";
        } else if (SystemFS.exists("/screensaver.raw")) {
            hasCustom = true;
            targetFS = &SystemFS;
            filePath = "/screensaver.raw";
        }

        if (hasCustom && targetFS) {
            File f = targetFS->open(filePath, "r");
            if (f) {
                fileSize = f.size();
                f.close();
            }
        }

        if (request->hasParam("raw") && hasCustom && targetFS) {
            request->send(*targetFS, filePath, "application/octet-stream");
            return;
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(256);
        doc["exists"] = hasCustom;
        doc["size"] = fileSize;
        doc["path"] = filePath;
        doc["screenMode"] = SettingsStore::getInstance().loadSleep().screenMode;
        serializeJson(doc, *response);
        request->send(response);
    });

    server->on("/api/settings/screensaver", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        bool removed = false;
        if (EbookFS.exists("/screensaver.raw")) {
            EbookFS.remove("/screensaver.raw");
            removed = true;
        }
        if (SystemFS.exists("/screensaver.raw")) {
            SystemFS.remove("/screensaver.raw");
            removed = true;
        }

        SettingsStore::Transaction tx;
        SettingsStore& store = SettingsStore::getInstance();
        SleepSettings s = store.loadSleep();
        if (s.screenMode == SLEEP_SCREEN_CUSTOM) {
            s.screenMode = SLEEP_SCREEN_COVER;
            store.saveSleep(s);
            BatteryMgr::getInstance().loadSleepSettings();
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(128);
        doc["ok"] = true;
        doc["removed"] = removed;
        serializeJson(doc, *response);
        request->send(response);
    });

    server->on(
        "/api/settings/screensaver/upload", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            if (g_screensaverUpload.owner != request) {
                request->send(400, "application/json", "{\"ok\":false,\"error\":\"no file provided\"}");
                return;
            }

            if (g_screensaverUpload.ok) {
                request->send(200, "application/json",
                              "{\"ok\":true,\"message\":\"Screensaver installed successfully\"}");
            } else {
                String errMsg =
                    g_screensaverUpload.error.length() ? g_screensaverUpload.error : "Upload failed";
                request->send(400, "application/json", "{\"ok\":false,\"error\":\"" + errMsg + "\"}");
            }
            g_screensaverUpload.reset();
        },
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len,
           bool final) {
            if (index == 0) {
                if (g_screensaverUpload.owner != nullptr && g_screensaverUpload.owner != request) {
                    return;
                }
                g_screensaverUpload.reset();
                g_screensaverUpload.owner = request;

                request->onDisconnect([request]() {
                    if (g_screensaverUpload.owner == request) {
                        if (g_screensaverUpload.file) g_screensaverUpload.file.close();
                        if (EbookFS.exists("/screensaver.raw.tmp")) EbookFS.remove("/screensaver.raw.tmp");
                        g_screensaverUpload.reset();
                    }
                });

                if (EbookFS.exists("/screensaver.raw.tmp")) {
                    EbookFS.remove("/screensaver.raw.tmp");
                }

                g_screensaverUpload.file = EbookFS.open("/screensaver.raw.tmp", FILE_WRITE);
                if (!g_screensaverUpload.file) {
                    g_screensaverUpload.error = "Failed to open temporary file on storage";
                    return;
                }
            }

            if (g_screensaverUpload.owner != request || !g_screensaverUpload.file) return;

            if (len > 0) {
                size_t written = g_screensaverUpload.file.write(data, len);
                if (written == len) {
                    g_screensaverUpload.bytesWritten += written;
                } else {
                    g_screensaverUpload.error = "Storage write failure";
                    g_screensaverUpload.file.close();
                    return;
                }
            }

            if (final) {
                g_screensaverUpload.file.flush();
                g_screensaverUpload.file.close();

                if (g_screensaverUpload.bytesWritten == 48000 || g_screensaverUpload.bytesWritten == 48004) {
                    if (EbookFS.exists("/screensaver.raw")) {
                        EbookFS.remove("/screensaver.raw");
                    }
                    if (EbookFS.rename("/screensaver.raw.tmp", "/screensaver.raw")) {
                        g_screensaverUpload.ok = true;

                        SettingsStore::Transaction tx;
                        SettingsStore& store = SettingsStore::getInstance();
                        SleepSettings s = store.loadSleep();
                        s.screenMode = SLEEP_SCREEN_CUSTOM;
                        store.saveSleep(s);
                        BatteryMgr::getInstance().loadSleepSettings();
                    } else {
                        g_screensaverUpload.error = "Failed to save /screensaver.raw";
                        EbookFS.remove("/screensaver.raw.tmp");
                    }
                } else {
                    g_screensaverUpload.error = "Invalid screensaver size (" +
                                                String(g_screensaverUpload.bytesWritten) +
                                                " bytes, expected 48000 or 48004)";
                    EbookFS.remove("/screensaver.raw.tmp");
                }
            }
        });
}
