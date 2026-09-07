#include "WebMgr.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "../KomaBon_Core/KomaBonFS.h"
#include "../KomaBon_Core/AppMgr.h"
#include "../KomaBon_Core/BatteryMgr.h"
#include "../Book32_Update/GitHubMgr.h"
#include <SD.h>

// Helper function to stream entire FS tree dynamically
static void streamFsTree(AsyncResponseStream* out, fs::FS& fs, const String& dir, uint8_t depth, bool& first,
                         size_t& totalSize, size_t& count) {
    File root = fs.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return;
    }

    std::vector<String> subdirs;
    File f = root.openNextFile();
    while (f) {
        String path = f.path();
        if (f.isDirectory()) {
            if (depth) subdirs.push_back(path);
        } else {
            size_t sz = f.size();
            totalSize += sz;
            count++;
            if (!first) out->print(",");
            first = false;
            // Basic inline escaping for paths
            String safePath = path;
            safePath.replace("\"", "\\\"");
            safePath.replace("\\", "\\\\");
            out->printf("{\"path\":\"%s\",\"size\":%u}", safePath.c_str(), (unsigned)sz);
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();

    // Recursion closes directory handles to save memory limits on ESP32 VFS
    for (const String& sub : subdirs) {
        streamFsTree(out, fs, sub, depth - 1, first, totalSize, count);
    }
}

void setupSystemEndpoints(AsyncWebServer* server) {

    // Silence browser favicon errors
    server->on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) { request->send(204); });

    server->on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(512);

        unsigned long totalSeconds = millis() / 1000;
        unsigned long hours = totalSeconds / 3600;
        unsigned long minutes = (totalSeconds % 3600) / 60;
        unsigned long seconds = totalSeconds % 60;
        char uptimeStr[20];
        snprintf(uptimeStr, sizeof(uptimeStr), "%luh %lum %lus", hours, minutes, seconds);
        doc["uptime"] = uptimeStr;
        doc["uptimeSeconds"] = totalSeconds;

        doc["rssi"] = WiFi.RSSI();
        doc["battery"] = BatteryMgr::getInstance().getPercentage();
        doc["voltage"] = BatteryMgr::getInstance().getVoltage();
        doc["charging"] = BatteryMgr::getInstance().isCharging();
        doc["version"] = SYSTEM_VERSION;

        doc["freeSpace"] = EbookFS_totalBytes() - EbookFS_usedBytes();
        doc["totalSpace"] = EbookFS_totalBytes();
        doc["usedSpace"] = EbookFS_usedBytes();
        doc["systemFree"] = SystemFS.totalBytes() - SystemFS.usedBytes();

        serializeJson(doc, *response);
        request->send(response);
    });

    server->on("/api/fs", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");

        struct Target {
            const char* key;
            fs::FS* fs;
            size_t used;
            size_t total;
        };
        Target targets[] = {
            {"ebooks", &EbookFS, EbookFS_usedBytes(), EbookFS_totalBytes()},
            {"system", &SystemFS, SystemFS.usedBytes(), SystemFS.totalBytes()},
        };

        response->print("{");
        bool firstTarget = true;
        for (Target& t : targets) {
            if (!firstTarget) response->print(",");
            firstTarget = false;
            response->printf("\"%s\":{\"total\":%u,\"used\":%u,\"files\":[", t.key, (unsigned)t.total,
                             (unsigned)t.used);
            bool first = true;
            size_t accounted = 0;
            size_t count = 0;
            streamFsTree(response, *t.fs, "/", 4, first, accounted, count);
            response->printf("],\"accounted\":%u,\"fileCount\":%u}", (unsigned)accounted, (unsigned)count);
        }
        response->print("}");

        request->send(response);
    });

    server->on("/api/check_update", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(1024);

        UpdateInfo info = GitHubMgr::getInstance().checkUpdate(SYSTEM_VERSION);

        doc["hasUpdate"] = info.available;
        doc["latest"] = info.version;
        doc["current"] = SYSTEM_VERSION;
        doc["hasFirmware"] = info.hasFirmware;
        doc["hasFilesystem"] = info.hasFilesystem;
        doc["release_notes"] = info.notes;

        serializeJson(doc, *response);
        request->send(response);
    });

    server->on("/api/update/all", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "Update scheduled");
        WebMgr::getInstance()._otaPending = true;
    });

    server->on("/api/app/switch", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("name")) {
            request->send(400, "application/json", "{\"error\":\"App name required\"}");
            return;
        }

        String appName = request->getParam("name")->value();
        AppMgr& appMgr = AppMgr::getInstance();

        int appIndex = -1;
        int idx = 0;
        for (auto* app : appMgr.getApps()) {
            if (appName.equalsIgnoreCase(app->getName())) {
                appIndex = idx;
                break;
            }
            idx++;
        }

        if (appIndex >= 0) {
            WebMgr::getInstance()._pendingAppSwitch = appIndex;
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        } else {
            request->send(404, "application/json", "{\"error\":\"App not found\"}");
        }
    });

    server->on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(512);

        bool sta = WiFi.status() == WL_CONNECTED;
        doc["sta_connected"] = sta;
        doc["sta_ssid"] = sta ? WiFi.SSID() : String("");
        doc["sta_ip"] = sta ? WiFi.localIP().toString() : String("");
        doc["rssi"] = sta ? WiFi.RSSI() : 0;

        wifi_mode_t mode = WiFi.getMode();
        bool ap = (mode == WIFI_AP || mode == WIFI_AP_STA);
        doc["ap_active"] = ap;
        doc["ap_ssid"] = ap ? WiFi.softAPSSID() : String("");
        doc["ap_ip"] = ap ? WiFi.softAPIP().toString() : String("");

        serializeJson(doc, *response);
        request->send(response);
    });

    server->on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) {
            request->send(202, "application/json", "{\"status\":\"scanning\"}");
            return;
        }
        if (n == WIFI_SCAN_FAILED) {
            WiFi.scanNetworks(true);
            request->send(202, "application/json", "{\"status\":\"scanning\"}");
            return;
        }

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(4096);
        JsonArray arr = doc.createNestedArray("networks");
        for (int i = 0; i < n && i < 20; i++) {
            JsonObject net = arr.createNestedObject();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
            net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        serializeJson(doc, *response);
        request->send(response);
        WiFi.scanDelete();
    });

    AsyncCallbackJsonWebHandler* wifiConnectHandler = new AsyncCallbackJsonWebHandler(
        "/api/wifi/connect", [](AsyncWebServerRequest* request, JsonVariant& json) {
            String ssid = json["ssid"].as<String>();
            String password = json["password"] | "";

            if (ssid.length() == 0) {
                request->send(400, "application/json", "{\"error\":\"SSID is required\"}");
                return;
            }

            wifi_mode_t mode = WiFi.getMode();
            if (mode == WIFI_AP)
                WiFi.mode(WIFI_AP_STA);
            else if (mode == WIFI_OFF)
                WiFi.mode(WIFI_STA);

            WiFi.begin(ssid.c_str(), password.c_str());
            request->send(200, "application/json", "{\"status\":\"connecting\"}");
        });
    server->addHandler(wifiConnectHandler);

    server->on("/joy_cal.json", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (SystemFS.exists("/joy_cal.json")) {
            request->send(SystemFS, "/joy_cal.json", "application/json");
        } else {
            request->send(404, "text/plain", "File not found");
        }
    });

    server->on("/api/debug", HTTP_POST, [](AsyncWebServerRequest* request) {
        WebMgr::getInstance()._debugKeepWifi = !WebMgr::getInstance()._debugKeepWifi;
        request->send(200, "text/plain", WebMgr::getInstance()._debugKeepWifi ? "1" : "0");
    });
}