#include "WebMgr.h"
#include "../KomaBon_Core/SettingsStore.h"
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <esp_partition.h>
#include "../KomaBon_Core/KomaBonFS.h"
#include "../KomaBon_Core/FileExt.h"
#include "../KomaBon_Core/DeviceCred.h"
#include "../KomaBon_OTA/GitHubMgr.h"
#include "../KomaBon_Core/AppMgr.h"
#include "../KomaBon_Core/DisplayMgr.h"
#include <SD.h>
#include <stdarg.h>
#include "../../include/NetworkState.h"

const char* WebMgr::devicePassword() {
    static char pw[BOOK32_CRED_LEN] = {0};
    if (pw[0] == '\0') {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        deriveDevicePassword(mac, pw, sizeof(pw));
    }
    return pw;
}

// Singleton instance retrieval
WebMgr& WebMgr::getInstance() {
    static WebMgr instance;
    return instance;
}

// Constructor: initialize async server
WebMgr::WebMgr() : server(new AsyncWebServer(80)) {}

static void listFiles(fs::FS& fs, const char* dirname, uint8_t levels) {
#if BOOK32_VERBOSE_BOOT_LOG
    Serial.printf("Listing directory: %s\n", dirname);
    File root = fs.open(dirname);
    if (!root || !root.isDirectory()) return;

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.printf("  DIR : %s\n", file.name());
            if (levels) listFiles(fs, file.path(), levels - 1);
        } else {
            Serial.printf("  FILE: %s  SIZE: %d\n", file.name(), file.size());
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
#endif
}

void WebMgr::mountFilesystems() {
    Serial.println("=== Mounting Filesystems ===");

    bool sysOK = SystemFS.begin(true, "/littlefs", 10, "spiffs");
    if (sysOK) {
        Serial.printf("SystemFS OK: %u / %u bytes used\n", SystemFS.usedBytes(), SystemFS.totalBytes());
    } else {
        Serial.println("WARNING: SystemFS mount FAILED!");
    }

    bool ebookOK = KomaBonStorage::mountEbooks();
    if (ebookOK) {
        Serial.printf("EbookFS OK: %u / %u bytes used\n", KomaBonStorage::getUsedBytes(),
                      KomaBonStorage::getTotalBytes());

        // Clean up interrupted .part uploads on boot
        std::vector<String> stale;
        File root = EbookFS.open("/");
        if (root && root.isDirectory()) {
            File f = root.openNextFile();
            while (f) {
                String n = f.name();
                if (hasExtensionCI(n, ".part")) stale.push_back(n);
                f.close();
                f = root.openNextFile();
            }
            root.close();
        }
        for (const String& n : stale) {
            Serial.printf("Removing incomplete upload: %s\n", n.c_str());
            EbookFS.remove("/" + n);
        }
    } else {
        Serial.println("ERROR: EbookFS mount failed!");
    }
    Serial.println("============================\n");
}

void WebMgr::connectWiFi() {
    if (_initialized) return;

    Serial.println("=== Starting Wi-Fi Radio (Phase 1) ===");
    gNetworkStartupInProgress = true;

    WiFi.mode(WIFI_STA);
    WiFi.begin();

    Serial.println("Trying STA mode...");
    unsigned long startAttempt = millis();

    while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt < 10000)) {
        delay(100);
        yield();
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("STA failed. Switching to AP mode.");
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, devicePassword());
        Serial.printf("AP Started. SSID: %s, IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    } else {
        Serial.printf("STA Connected. IP: %s\n", WiFi.localIP().toString().c_str());
    }
}

void WebMgr::startServer() {
    if (_initialized) return;

    Serial.println("=== Starting HTTP Server (Phase 2) ===");

    if (!_endpointsConfigured) {
        setupEndpoints();
        _endpointsConfigured = true;
    }

    server->begin();
    _initialized = true;
    resetIdleTimer();

    if (MDNS.begin(DEVICE_NAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS: http://book32.local/");
    }

    gNetworkStartupInProgress = false;
}

void WebMgr::stopNetwork() {
    if (!_initialized) return;

    Serial.println("=== Stopping Network & Killing Radio ===");

    MDNS.end();

    server->end();

    unsigned long waitStart = millis();
    while (millis() - waitStart < 200) {
        delay(10);
        yield();
    }

    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    _initialized = false;
}

void WebMgr::resetIdleTimer() {
    _lastActivityTime = millis();
}

void WebMgr::update() {
    // Wi-Fi Watchdog routine
    if (_initialized && !_debugKeepWifi) {
        if (millis() - _lastActivityTime > WIFI_TIMEOUT_MS) {
            Serial.println("Inactivity timeout reached. Shutting down Wi-Fi.");
            stopNetwork();

            // Force GUI refresh to remove Wi-Fi status icon
            App* current = AppMgr::getInstance().getCurrentApp();
            if (current) current->forceRedraw();
        }
    }

    if (_otaPending) {
        _otaPending = false;
        Serial.println("Scheduling OTA update in separate task...");
        // Ensure network is not killed before OTA starts
        resetIdleTimer();

        delay(100);
        xTaskCreatePinnedToCore(
            [](void* param) {
                Serial.println("OTA task started");
                GitHubMgr::getInstance().performFullUpdate(SYSTEM_VERSION);
                Serial.println("OTA task complete, restarting...");
                vTaskDelay(100 / portTICK_PERIOD_MS);
                ESP.restart();
            },
            "OTA_Task", 16384, nullptr, 1, nullptr, 1);
    }
}

void WebMgr::setupEndpoints() {
    // Delegate route setups to modular files
    setupSystemEndpoints(server);
    setupBookEndpoints(server);
    setupSettingsEndpoints(server);

    // Serve static Web UI
    if (SystemFS.exists("/index.html")) {
        Serial.println("Serving web UI from SystemFS");
        server->serveStatic("/", SystemFS, "/").setDefaultFile("index.html");
    } else if (EbookFS.exists("/index.html")) {
        Serial.println("Serving web UI from EbookFS");
        server->serveStatic("/", EbookFS, "/").setDefaultFile("index.html");
    }
}