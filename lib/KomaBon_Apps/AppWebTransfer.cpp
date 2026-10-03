#include "AppWebTransfer.h"
#include "icon_web.h"
#include "../KomaBon_Core/DisplayMgr.h"
#include "../KomaBon_Core/FontMgr.h"
#include "../KomaBon_Core/AppMgr.h"
#include "../KomaBon_Core/BatteryMgr.h"
#include "../KomaBon_Web/WebMgr.h"
#include "../../include/Config.h"
#include <WiFi.h>
#include <qrcode.h>

AppWebTransfer::AppWebTransfer() {
    _needsRedraw = true;
    _state = WebTransferState::Init;
    _stateTimer = 0;
}

const uint8_t* AppWebTransfer::getIconImage() {
    return icon_web_160x160;
}

void AppWebTransfer::start() {
    // Run at full 240 MHz for maximum Wi-Fi throughput and HTTP parsing
    setCpuFrequencyMhz(240);
    _state = WebTransferState::Init;
    _needsRedraw = true;
    InputMgr::getInstance().setCallback(std::bind(&AppWebTransfer::handleInput, this, std::placeholders::_1));
    Serial.println("AppWebTransfer: UI initialized (240MHz). Waiting for E-ink charge pump to power off...");
}

void AppWebTransfer::stop() {
    Serial.println("AppWebTransfer: Clean shutdown of network services.");

    WebMgr::getInstance().stopNetwork();

    _state = WebTransferState::Init;
    InputMgr::getInstance().clearCallback();
}

void AppWebTransfer::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;

    if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU || action == INPUT_LEFT) {
        AppMgr::getInstance().switchTo(0);
    }
}

void AppWebTransfer::update() {
    if (_state == WebTransferState::WaitingForInitScreen) {
        if (millis() - _stateTimer > 4000) {
            _state = WebTransferState::StartingRadio;
        }
    } else if (_state == WebTransferState::StartingRadio) {
        Serial.println("AppWebTransfer: Booting up Wi-Fi radio...");

        WebMgr::getInstance().connectWiFi();

        WiFi.setTxPower(WIFI_POWER_5dBm);
        Serial.println("AppWebTransfer: Wi-Fi TX throttled. Safe to update E-ink...");

        _state = WebTransferState::WaitingForReadyScreen;
        _needsRedraw = true;
        _stateTimer = millis();
    } else if (_state == WebTransferState::WaitingForReadyScreen) {
        if (millis() - _stateTimer > 4500) {
            WiFi.setTxPower(WIFI_POWER_19_5dBm);
            Serial.println("AppWebTransfer: E-ink pump OFF. Wi-Fi TX power restored to MAX.");

            WebMgr::getInstance().startServer();

            _state = WebTransferState::Ready;
        }
    }
}

void AppWebTransfer::forceRedraw() {
    if (_state == WebTransferState::Ready || _state == WebTransferState::WaitingForReadyScreen) {
        return;
    }
    _needsRedraw = true;
}

void AppWebTransfer::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    display.setFullWindow();
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        BatteryMgr::getInstance().drawStatusBar(display, 0, 0);

        fontMgr.drawTextCentered(display, "Web File Transfer", 60, FONT_SIZE_SUBTITLE, GxEPD_BLACK);

        if (_state == WebTransferState::Init || _state == WebTransferState::WaitingForInitScreen ||
            _state == WebTransferState::StartingRadio) {
            drawConnecting();
        } else if (_state == WebTransferState::WaitingForReadyScreen || _state == WebTransferState::Ready) {
            drawReady();
        }

        fontMgr.drawTextCentered(display, "Push LEFT to close and disable Wi-Fi", display.height() - 40,
                                 FONT_SIZE_SMALL, GxEPD_BLACK);

    } while (display.nextPage());

    // Innesco del primo timer post-rendering
    if (_state == WebTransferState::Init) {
        _state = WebTransferState::WaitingForInitScreen;
        _stateTimer = millis();
    }
}

void AppWebTransfer::drawConnecting() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    fontMgr.drawTextCentered(display, "Initializing Network Hardware...", display.height() / 2,
                             FONT_SIZE_BODY, GxEPD_BLACK);
}

void AppWebTransfer::drawReady() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    bool isSta = (WiFi.status() == WL_CONNECTED);
    String ipStr = isSta ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    if (ipStr == "0.0.0.0" || ipStr.length() == 0) {
        ipStr = isSta ? "192.168.1.1" : "192.168.4.1";
    }
    String url = "http://" + ipStr + "/";

    int startY = 100;
    int lineSpacing = 30;

    if (isSta) {
        String netStr = "Network: " + WiFi.SSID();
        fontMgr.drawTextCentered(display, netStr.c_str(), startY, FONT_SIZE_BODY, GxEPD_BLACK);

        String ipLine = "IP: " + ipStr;
        fontMgr.drawTextCentered(display, ipLine.c_str(), startY + lineSpacing, FONT_SIZE_BODY, GxEPD_BLACK);

        fontMgr.drawTextCentered(display, "(or http://komabon.local/)", startY + (lineSpacing * 2),
                                 FONT_SIZE_SMALL, GxEPD_BLACK);
    } else {
        fontMgr.drawTextCentered(display, "Hotspot Mode (No Wi-Fi)", startY, FONT_SIZE_BODY, GxEPD_BLACK);

        String ssidStr = String("Wi-Fi SSID: ") + String(AP_SSID);
        fontMgr.drawTextCentered(display, ssidStr.c_str(), startY + lineSpacing, FONT_SIZE_BODY, GxEPD_BLACK);

        String passStr = String("Password: ") + String(WebMgr::devicePassword());
        fontMgr.drawTextCentered(display, passStr.c_str(), startY + (lineSpacing * 2), FONT_SIZE_BODY,
                                 GxEPD_BLACK);

        String ipLine = "IP: " + ipStr;
        fontMgr.drawTextCentered(display, ipLine.c_str(), startY + (lineSpacing * 3), FONT_SIZE_SMALL,
                                 GxEPD_BLACK);
    }

    int qrTopY = isSta ? 250 : 275;
    drawQRCode(display, url.c_str(), qrTopY, 7);

    // Bounding card bottom is: qrTopY + 203 + 14 = qrTopY + 217
    // Spacing between card bottom and text baseline is increased for equal visual balance
    int textBelowY = qrTopY + 217 + 60;
    fontMgr.drawTextCentered(display, "Scan with phone camera to open Web UI", textBelowY, FONT_SIZE_BODY,
                             GxEPD_BLACK);
    fontMgr.drawTextCentered(display, "Upload books, screensavers & settings", textBelowY + 28,
                             FONT_SIZE_SMALL, GxEPD_BLACK);
}

void AppWebTransfer::drawQRCode(KomaBonDisplay& display, const char* text, int topY, int scale) {
    QRCode qrcode;
    uint8_t version = 3;
    size_t len = strlen(text);
    if (len > 32) version = 4;
    if (len > 46) version = 5;

    uint16_t bufSize = qrcode_getBufferSize(version);
    uint8_t qrcodeData[bufSize];
    if (qrcode_initText(&qrcode, qrcodeData, version, ECC_LOW, text) != 0) {
        Serial.println("AppWebTransfer: QR code init failed");
        return;
    }

    int qrSize = qrcode.size;
    int totalPx = qrSize * scale;
    int startX = (display.width() - totalPx) / 2;
    int startY = topY;
    int quietPx = 14;

    // Rounded background card with clean border
    display.fillRoundRect(startX - quietPx, startY - quietPx, totalPx + (quietPx * 2),
                          totalPx + (quietPx * 2), 8, GxEPD_WHITE);
    display.drawRoundRect(startX - quietPx, startY - quietPx, totalPx + (quietPx * 2),
                          totalPx + (quietPx * 2), 8, GxEPD_BLACK);

    // Draw black QR modules
    for (uint8_t y = 0; y < qrSize; y++) {
        for (uint8_t x = 0; x < qrSize; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                display.fillRect(startX + (x * scale), startY + (y * scale), scale, scale, GxEPD_BLACK);
            }
        }
    }
}
