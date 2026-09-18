#include "GitHubMgr.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "../../include/Config.h"
#include "../KomaBon_Core/SemVer.h"
#include "../KomaBon_Core/OtaDigest.h"
#include "../KomaBon_Core/OtaEd25519PublicKey.h"
#include <mbedtls/sha256.h>
#include <Ed25519.h>
#include "../KomaBon_Core/DisplayMgr.h"
#include "../KomaBon_Core/FontMgr.h"
#include <GxEPD2_BW.h>

static const unsigned long OTA_STALL_TIMEOUT_MS = 15000;

static void drawOTAProgress(int progress, const char* title, const char* status) {
    auto& display = DisplayMgr::getInstance().getDisplay();
    auto& fontMgr = FontMgr::getInstance();

    int screenW = display.width();
    int screenH = display.height();

    int barWidth = screenW - 100;
    int barHeight = 30;
    int barX = 50;
    int barY = screenH / 2;
    int fillWidth = (barWidth * progress) / 100;

    display.setPartialWindow(0, 0, screenW, screenH);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        fontMgr.drawTextCentered(display, title, barY - 60, FONT_SIZE_TITLE, GxEPD_BLACK);
        fontMgr.drawTextCentered(display, status, barY - 25, FONT_SIZE_BODY, GxEPD_BLACK);

        display.drawRect(barX, barY, barWidth, barHeight, GxEPD_BLACK);
        display.drawRect(barX + 1, barY + 1, barWidth - 2, barHeight - 2, GxEPD_BLACK);

        if (fillWidth > 4) {
            display.fillRect(barX + 2, barY + 2, fillWidth - 4, barHeight - 4, GxEPD_BLACK);
        }

        char percentText[16];
        snprintf(percentText, sizeof(percentText), "%d%%", progress);
        fontMgr.drawTextCentered(display, percentText, barY + barHeight + 40, FONT_SIZE_TITLE, GxEPD_BLACK);

    } while (display.nextPage());
}

GitHubMgr::GitHubMgr() {}

GitHubMgr& GitHubMgr::getInstance() {
    static GitHubMgr instance;
    return instance;
}

void GitHubMgr::init() {}

UpdateInfo GitHubMgr::checkUpdate(const char* currentVersion) {
    UpdateInfo info = {false, "", "", "", "", false, false, "", "", "", ""};

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, cannot check for updates");
        return info;
    }

    HTTPClient http;
    String apiURL = String("https://api.github.com/repos/") + GITHUB_REPO + "/releases/latest";

    Serial.printf("Checking: %s\n", apiURL.c_str());

    http.begin(apiURL);
    http.setUserAgent("KomaBon-ESP32");
    http.setTimeout(10000);

    int httpCode = http.GET();
    Serial.printf("HTTP Response: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
        StaticJsonDocument<192> filter;
        filter["tag_name"] = true;
        filter["body"] = true;
        filter["assets"][0]["name"] = true;
        filter["assets"][0]["browser_download_url"] = true;

        DynamicJsonDocument doc(8192);
        DeserializationError err =
            deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

        if (err) {
            Serial.printf("JSON parse error: %s\n", err.c_str());
            http.end();
            return info;
        }

        const char* tagName = doc["tag_name"];
        info.version = tagName ? tagName : "";
        info.notes = doc["body"].as<String>();

        String currentV = String(currentVersion);
        String latestV = info.version;

        if (semverIsNewer(latestV, currentV)) {
            info.available = true;
            Serial.println("Update IS available");

            JsonArray assets = doc["assets"];
            for (JsonObject asset : assets) {
                String name = asset["name"].as<String>();
                String url = asset["browser_download_url"].as<String>();

                if (name == "firmware.bin" || name.endsWith("_firmware.bin")) {
                    info.firmwareUrl = url;
                    info.hasFirmware = true;
                } else if (name == "littlefs.bin" || name == "filesystem.bin" ||
                           name.endsWith("_littlefs.bin")) {
                    info.filesystemUrl = url;
                    info.hasFilesystem = true;
                }
            }

            if (info.hasFirmware) {
                extractSha256(info.notes, "firmware.bin", info.firmwareSha256);
            }
            if (info.hasFilesystem) {
                if (!extractSha256(info.notes, "littlefs.bin", info.filesystemSha256)) {
                    extractSha256(info.notes, "filesystem.bin", info.filesystemSha256);
                }
            }

            if (info.hasFirmware) {
                extractEd25519Signature(info.notes, "firmware.bin", info.firmwareEd25519Sig);
            }
            if (info.hasFilesystem) {
                if (!extractEd25519Signature(info.notes, "littlefs.bin", info.filesystemEd25519Sig)) {
                    extractEd25519Signature(info.notes, "filesystem.bin", info.filesystemEd25519Sig);
                }
            }
        } else {
            Serial.println("Already up to date");
        }
    }
    http.end();
    return info;
}

bool GitHubMgr::downloadAndFlash(const char* url, int partition, const char* label, bool restartAfter,
                                 int step, int totalSteps, const char* expectedSha256,
                                 const char* expectedEd25519Sig) {
    if (WiFi.status() != WL_CONNECTED) return false;

    // Use the new KOMABON_ macros
    if (!expectedSha256 || strlen(expectedSha256) != KOMABON_SHA256_HEX_LEN) {
        drawOTAProgress(0, "Update Blocked", "No checksum in release");
        delay(3000);
        return false;
    }

    if (!expectedEd25519Sig || strlen(expectedEd25519Sig) != KOMABON_ED25519_SIG_HEX_LEN) {
        drawOTAProgress(0, "Update Blocked", "No signature in release");
        delay(3000);
        return false;
    }

    char title[64];
    if (totalSteps > 1) {
        snprintf(title, sizeof(title), "%s (%d/%d)", label, step, totalSteps);
    } else {
        snprintf(title, sizeof(title), "%s Update", label);
    }

    HTTPClient http;
    http.begin(url);
    http.setUserAgent("KomaBon-ESP32");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);
    http.addHeader("Accept", "application/octet-stream");

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        http.end();
        return false;
    }

    if (!Update.begin(contentLength, partition)) {
        http.end();
        return false;
    }

    drawOTAProgress(0, title, "Downloading...");
    WiFiClient* stream = http.getStreamPtr();

    uint8_t buff[4096];
    size_t written = 0;
    int lastProgress = 0;

    mbedtls_sha256_context shaCtx;
    mbedtls_sha256_init(&shaCtx);
    mbedtls_sha256_starts(&shaCtx, 0);

    unsigned long lastDataMs = millis();

    while (written < (size_t)contentLength) {
        size_t available = stream->available();
        if (available == 0) {
            if (millis() - lastDataMs > OTA_STALL_TIMEOUT_MS) {
                mbedtls_sha256_free(&shaCtx);
                Update.abort();
                http.end();
                return false;
            }
            delay(1);
            continue;
        }
        lastDataMs = millis();

        size_t toRead = std::min(available, sizeof(buff));
        size_t bytesRead = stream->readBytes(buff, toRead);
        if (bytesRead == 0) continue;

        size_t bytesWritten = Update.write(buff, bytesRead);
        if (bytesWritten != bytesRead) {
            mbedtls_sha256_free(&shaCtx);
            Update.abort();
            http.end();
            return false;
        }
        mbedtls_sha256_update(&shaCtx, buff, bytesRead);
        written += bytesWritten;

        int progress = (written * 100) / contentLength;
        if (progress / 5 > lastProgress / 5) {
            drawOTAProgress(progress, title, "Downloading...");
            lastProgress = progress;
        }
        yield();
    }

    if (written != (size_t)contentLength) {
        mbedtls_sha256_free(&shaCtx);
        Update.abort();
        http.end();
        return false;
    }

    drawOTAProgress(100, title, "Verifying...");

    uint8_t digest[32];
    mbedtls_sha256_finish(&shaCtx, digest);
    mbedtls_sha256_free(&shaCtx);

    char actualHex[KOMABON_SHA256_HEX_LEN + 1];
    for (int i = 0; i < 32; i++) {
        snprintf(actualHex + (i * 2), 3, "%02x", digest[i]);
    }

    String actual = String(actualHex);
    String expected = String(expectedSha256);
    if (!sha256Equal(actual, expected)) {
        Update.abort();
        http.end();
        drawOTAProgress(0, "Update Blocked", "Checksum mismatch");
        delay(3000);
        return false;
    }

    uint8_t sigBytes[KOMABON_ED25519_SIG_LEN];
    if (!hexDecode(String(expectedEd25519Sig), (size_t)KOMABON_ED25519_SIG_HEX_LEN, sigBytes)) {
        Update.abort();
        http.end();
        drawOTAProgress(0, "Update Blocked", "Malformed signature");
        delay(3000);
        return false;
    }

    // Updated to use KOMABON_ macro
    if (!Ed25519::verify(sigBytes, KOMABON_OTA_ED25519_PUBLIC_KEY, digest, sizeof(digest))) {
        Update.abort();
        http.end();
        drawOTAProgress(0, "Update Blocked", "Signature invalid");
        delay(3000);
        return false;
    }

    drawOTAProgress(100, title, "Installing...");
    if (!Update.end()) {
        http.end();
        return false;
    }

    drawOTAProgress(100, title, "Complete!");
    delay(500);
    http.end();
    if (restartAfter) {
        ESP.restart();
    }
    return true;
}

bool GitHubMgr::performFirmwareUpdate(const char* url, bool restartAfter, int step, int totalSteps,
                                      const char* expectedSha256, const char* expectedEd25519Sig) {
    return downloadAndFlash(url, U_FLASH, "Firmware", restartAfter, step, totalSteps, expectedSha256,
                            expectedEd25519Sig);
}

bool GitHubMgr::performFilesystemUpdate(const char* url, bool restartAfter, int step, int totalSteps,
                                        const char* expectedSha256, const char* expectedEd25519Sig) {
    return downloadAndFlash(url, U_SPIFFS, "Web Interface", restartAfter, step, totalSteps, expectedSha256,
                            expectedEd25519Sig);
}

void GitHubMgr::triggerUpdate(const char* currentVersion) {
    performFullUpdate(currentVersion);
}

bool GitHubMgr::performFullUpdate(const char* currentVersion) {
    UpdateInfo info = checkUpdate(currentVersion);

    if (!info.available) return false;

    bool firmwareUpdated = false;
    bool filesystemUpdated = false;

    int totalSteps = (info.hasFirmware ? 1 : 0) + (info.hasFilesystem ? 1 : 0);
    int currentStep = 0;

    if (info.hasFirmware) {
        currentStep++;
        firmwareUpdated = performFirmwareUpdate(info.firmwareUrl.c_str(), false, currentStep, totalSteps,
                                                info.firmwareSha256.c_str(), info.firmwareEd25519Sig.c_str());
        if (!firmwareUpdated) return false;
    }

    if (info.hasFilesystem) {
        currentStep++;
        filesystemUpdated =
            performFilesystemUpdate(info.filesystemUrl.c_str(), false, currentStep, totalSteps,
                                    info.filesystemSha256.c_str(), info.filesystemEd25519Sig.c_str());
        if (!filesystemUpdated) {
            if (firmwareUpdated) ESP.restart();
            return false;
        }
    }

    if (firmwareUpdated || filesystemUpdated) {
        drawOTAProgress(100, "Update Complete!", "Restarting...");
        delay(1000);
        ESP.restart();
        return true;
    }
    return false;
}