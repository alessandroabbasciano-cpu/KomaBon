#include "SafeBoot.h"
#include "Config.h"
#include "DisplayMgr.h"
#include "CrashHandler.h"
#include "KomaBonFS.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <LittleFS.h>
#include <SD.h>

bool SafeBoot::checkRequested() {
    pinMode(PIN_BUTTON_BACK, INPUT_PULLUP);
    pinMode(JOY_ADC_PIN, INPUT);
    analogSetAttenuation(ADC_11db);

    int heldCount = 0;
    const int totalSamples = 24; // 24 samples * 50ms = 1200ms
    for (int i = 0; i < totalSamples; i++) {
        bool backPressed = (digitalRead(PIN_BUTTON_BACK) == LOW);
        int joyVal = analogRead(JOY_ADC_PIN);
        bool joyCenterPressed = (joyVal < 600);

        if (backPressed || joyCenterPressed) {
            heldCount++;
        }
        delay(50);
    }

    return (heldCount >= 20); // Held for at least 1.0 second continuously
}

static AsyncWebServer s_rescueServer(80);

static String buildRescueHtml() {
    String html = F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                    "<title>KomaBon Recovery</title>"
                    "<style>"
                    "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;background:#18181b;color:#f4f4f5;padding:20px;max-width:520px;margin:auto}"
                    "h1{color:#f59e0b;font-size:1.5rem;margin-bottom:4px}"
                    ".subtitle{color:#a1a1aa;font-size:0.85rem;margin-bottom:16px}"
                    ".card{background:#27272a;border-radius:8px;padding:16px;margin-bottom:16px;border:1px solid #3f3f46}"
                    "h3{margin-top:0;font-size:1.1rem;color:#e4e4e7}"
                    "button{background:#d97706;color:#fff;border:none;border-radius:6px;padding:10px 16px;font-size:0.95rem;font-weight:600;cursor:pointer;width:100%;margin-top:8px}"
                    "button:hover{opacity:0.9}"
                    "button.danger{background:#dc2626}"
                    "button.success{background:#16a34a}"
                    ".info{font-size:0.9rem;color:#d4d4d8;margin:6px 0}"
                    "pre{background:#09090b;padding:10px;border-radius:6px;overflow-x:auto;font-size:0.8rem;color:#38bdf8;max-height:200px}"
                    "input[type=file]{width:100%;margin-bottom:8px;color:#a1a1aa}"
                    "</style></head><body>"
                    "<h1>🛡️ KomaBon Recovery Console</h1>"
                    "<div class=\"subtitle\">Hardware Safe-Boot Active &bull; Running in RAM</div>"
                    "<div class=\"card\">"
                    "<h3>System Diagnostics</h3>");

    html += "<div class=\"info\">Firmware Version: <strong>" + String(SYSTEM_VERSION) + "</strong></div>";
    html += "<div class=\"info\">Reset Reason: <strong>" + String(CrashHandler::getInstance().getResetReasonString()) + "</strong></div>";
    html += "<div class=\"info\">Free Heap: <strong>" + String(ESP.getFreeHeap()) + " bytes</strong></div>";
    html += "<div class=\"info\">LittleFS: <strong>" + String(SystemFS.totalBytes() > 0 ? "Mounted" : "Not Mounted") + "</strong></div>";
    html += F("</div><div class=\"card\">"
              "<h3>Emergency Actions</h3>"
              "<form method=\"POST\" action=\"/api/rescue/wipe_configs\">"
              "<button type=\"submit\" onclick=\"return confirm('Wipe all JSON configs (calibration, progress, sleep)?')\">Wipe Corrupted JSON Configs</button>"
              "</form>"
              "<form method=\"POST\" action=\"/api/rescue/format_system\" style=\"margin-top:8px\">"
              "<button type=\"submit\" class=\"danger\" onclick=\"return confirm('DANGER: Format internal SystemFS (LittleFS)? All web files and settings will be erased!')\">Format Internal LittleFS</button>"
              "</form>"
              "<form method=\"POST\" action=\"/api/rescue/reboot\" style=\"margin-top:8px\">"
              "<button type=\"submit\" class=\"success\">Reboot Device Normally</button>"
              "</form>"
              "</div>"
              "<div class=\"card\">"
              "<h3>⚡ Emergency Firmware OTA</h3>"
              "<form method=\"POST\" action=\"/api/rescue/ota\" enctype=\"multipart/form-data\">"
              "<input type=\"file\" name=\"firmware\" accept=\".bin\" required>"
              "<button type=\"submit\">Upload & Flash Firmware .bin</button>"
              "</form>"
              "</div>"
              "<div class=\"card\">"
              "<h3>📜 Post-Mortem Crash Log</h3><pre>");

    if (CrashHandler::getInstance().hasCrashLog()) {
        html += CrashHandler::getInstance().getCrashLog();
    } else {
        html += "No abnormal crash log detected.";
    }

    html += F("</pre></div></body></html>");
    return html;
}

void SafeBoot::run() {
    Serial.println("\n\n========================================");
    Serial.println("  [SAFE-BOOT] EMERGENCY RECOVERY MODE  ");
    Serial.println("========================================");

    // Initialize display to notify the user visually
    DisplayMgr& display = DisplayMgr::getInstance();
    display.init();
    display.showBootScreen(100, "RESCUE MODE ACTIVE\n\nSSID: KomaBon-Rescue\nIP: 192.168.4.1\n\nFilesystems & apps bypassed.");

    // Mount SystemFS if possible, but continue even if failed
    SystemFS.begin();

    // Configure SoftAP in isolation
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP("KomaBon-Rescue");
    IPAddress apIP = WiFi.softAPIP();

    Serial.printf("[SAFE-BOOT] SoftAP started. Connect to 'KomaBon-Rescue'. IP: %s\n", apIP.toString().c_str());

    // Root handler
    s_rescueServer.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", buildRescueHtml());
    });

    // Wipe configs
    s_rescueServer.on("/api/rescue/wipe_configs", HTTP_POST, [](AsyncWebServerRequest* request) {
        const char* configs[] = {
            "/joy_cal.json",
            "/progress.json",
            "/reader_progress.json",
            "/book_order.json",
            "/reader_config.json",
            "/sleep_config.json"
        };
        int wiped = 0;
        for (const char* path : configs) {
            if (SystemFS.exists(path)) {
                SystemFS.remove(path);
                wiped++;
            }
        }
        CrashHandler::getInstance().clearCrashLog();

        String resp = "<html><body style=\"font-family:sans-serif;background:#18181b;color:#f4f4f5;padding:20px;\">";
        resp += "<h2>Configuration Files Wiped (" + String(wiped) + " files removed)</h2>";
        resp += "<p>You may now reboot normally to re-run joystick calibration.</p>";
        resp += "<form method=\"POST\" action=\"/api/rescue/reboot\"><button style=\"background:#16a34a;color:#fff;padding:10px 20px;border:none;border-radius:6px;cursor:pointer;\">Reboot Now</button></form>";
        resp += "</body></html>";
        request->send(200, "text/html", resp);
    });

    // Format LittleFS
    s_rescueServer.on("/api/rescue/format_system", HTTP_POST, [](AsyncWebServerRequest* request) {
        SystemFS.end();
        bool formatted = LittleFS.format();
        SystemFS.begin();

        String resp = "<html><body style=\"font-family:sans-serif;background:#18181b;color:#f4f4f5;padding:20px;\">";
        resp += formatted ? "<h2>SystemFS Formatted Successfully</h2>" : "<h2>SystemFS Format Failed</h2>";
        resp += "<form method=\"POST\" action=\"/api/rescue/reboot\"><button style=\"background:#16a34a;color:#fff;padding:10px 20px;border:none;border-radius:6px;cursor:pointer;\">Reboot Now</button></form>";
        resp += "</body></html>";
        request->send(200, "text/html", resp);
    });

    // Reboot
    s_rescueServer.on("/api/rescue/reboot", HTTP_POST, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "Rebooting...");
        delay(500);
        ESP.restart();
    });

    // Emergency OTA Handler
    s_rescueServer.on("/api/rescue/ota", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            bool updateOk = !Update.hasError();
            AsyncWebServerResponse* response = request->beginResponse(200, "text/html",
                updateOk ? "<h2>Firmware Flashed Successfully! Rebooting...</h2>"
                         : "<h2>Firmware Flash Failed. Please retry.</h2>");
            response->addHeader("Connection", "close");
            request->send(response);
            if (updateOk) {
                delay(1000);
                ESP.restart();
            }
        },
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            if (!index) {
                Serial.printf("[SAFE-BOOT] OTA Update Start: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                    Update.printError(Serial);
                }
            }
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.println("[SAFE-BOOT] OTA Update Success.");
                } else {
                    Update.printError(Serial);
                }
            }
        }
    );

    s_rescueServer.begin();
    Serial.println("[SAFE-BOOT] Recovery Web Server listening on port 80.");

    // Indefinite loop for rescue mode
    while (true) {
        delay(100);
    }
}
