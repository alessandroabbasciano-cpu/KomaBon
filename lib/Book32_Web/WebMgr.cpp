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
#include "../Book32_Update/GitHubMgr.h"
#include "../KomaBon_Core/AppMgr.h"
#include "../KomaBon_Core/DisplayMgr.h"

const char* WebMgr::devicePassword() {
    static char pw[BOOK32_CRED_LEN] = {0};
    if (pw[0] == '\0') {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        deriveDevicePassword(mac, pw, sizeof(pw));
    }
    return pw;
}

// CRITICAL FIX: Restored the Singleton instance getter
WebMgr& WebMgr::getInstance() {
    static WebMgr instance;
    return instance;
}

// Initialize the async server and the WebSocket endpoint
WebMgr::WebMgr() : server(new AsyncWebServer(80)), ws(new AsyncWebSocket("/ws")) {}

// Dispatch system logs to the Web UI live console
void WebMgr::sendLog(const String& msg) {
    Serial.println(msg);
    if (ws) {
        ws->textAll(msg);
    }
}

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

    bool ebookOK = EbookFS_begin();
    if (ebookOK) {
        Serial.printf("EbookFS OK: %u / %u bytes used\n", EbookFS_usedBytes(), EbookFS_totalBytes());

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

void WebMgr::init() {
    if (_initialized) return;
    if (!_endpointsConfigured) {
        setupEndpoints();
        server->addHandler(ws); // Register WebSocket to the server instance
        _endpointsConfigured = true;
    }
    server->begin();
    _initialized = true;
    Serial.println("Web Server Started");

    if (MDNS.begin("book32")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS: http://book32.local/");
    }
}

void WebMgr::stop() {
    if (!_initialized) return;
    MDNS.end();
    server->end();
    _initialized = false;
    Serial.println("Web Server Stopped");
}

void WebMgr::update() {
    if (_pendingRotation != -1) {
        int rot = _pendingRotation;
        _pendingRotation = -1;
        DisplayMgr::getInstance().setRotation(rot);
        App* current = AppMgr::getInstance().getCurrentApp();
        if (current) current->forceRedraw();
    }

    if (_pendingReaderFontSize != 0) {
        int pt = _pendingReaderFontSize;
        _pendingReaderFontSize = 0;
        for (auto* app : AppMgr::getInstance().getApps()) {
            if (strcmp(app->getName(), "eReader") == 0) {
                app->applyFontSize(pt);
                break;
            }
        }
    }

    if (_pendingReaderFontFamily != -1) {
        int fam = _pendingReaderFontFamily;
        _pendingReaderFontFamily = -1;
        for (auto* app : AppMgr::getInstance().getApps()) {
            if (strcmp(app->getName(), "eReader") == 0) {
                app->applyFontFamily(fam);
                break;
            }
        }
    }

    if (_pendingAppSwitch >= 0) {
        int index = _pendingAppSwitch;
        _pendingAppSwitch = -1;
        AppMgr::getInstance().switchTo(index);
    }

    if (_otaPending) {
        _otaPending = false;
        Serial.println("Scheduling OTA update in separate task...");
        stop();
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