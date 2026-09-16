#include "AppWebTransfer.h"
#include "icon_web.h"
#include "../KomaBon_Core/DisplayMgr.h"
#include "../KomaBon_Core/FontMgr.h"
#include "../KomaBon_Core/AppMgr.h"
#include "../Book32_Web/WebMgr.h"
#include "../../include/Config.h"
#include <WiFi.h>

AppWebTransfer::AppWebTransfer() {
    _needsRedraw = true;
    _wifiConnecting = false;
    _wifiReady = false;
}

const uint8_t* AppWebTransfer::getIconImage() {
    return icon_web_160x160;
}

void AppWebTransfer::start() {
    _needsRedraw = true;
    _wifiConnecting = true;
    _wifiReady = false;

    InputMgr::getInstance().setCallback(std::bind(&AppWebTransfer::handleInput, this, std::placeholders::_1));

    Serial.println("AppWebTransfer: Booting up Wi-Fi radio...");

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, WebMgr::devicePassword());

    delay(100);

    WebMgr::getInstance().startNetwork();

    _wifiConnecting = false;
    _wifiReady = true;
    _needsRedraw = true;
}

void AppWebTransfer::stop() {
    Serial.println("AppWebTransfer: Hard shut down of Wi-Fi radio to preserve battery.");
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);

    WiFi.mode(WIFI_OFF);

    _wifiReady = false;
    InputMgr::getInstance().clearCallback();
}

void AppWebTransfer::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;

    if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU || action == INPUT_LEFT) {
        AppMgr::getInstance().switchTo(0);
    }
}

void AppWebTransfer::update() {}

void AppWebTransfer::forceRedraw() {
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

        fontMgr.drawTextCentered(display, "Web File Transfer", 60, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        display.drawLine(40, 85, display.width() - 40, 85, GxEPD_BLACK);

        if (_wifiConnecting) {
            drawConnecting();
        } else if (_wifiReady) {
            drawReady();
        }

        fontMgr.drawTextCentered(display, "Push LEFT to close and disable Wi-Fi", display.height() - 40,
                                 FONT_SIZE_SMALL, GxEPD_BLACK);

    } while (display.nextPage());
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

    int startY = 160;
    int lineSpacing = 45;

    fontMgr.drawTextCentered(display, "Server is running. Connect via web browser:", startY, FONT_SIZE_BODY,
                             GxEPD_BLACK);

    String ipStr = "IP Address: 192.168.4.1";

    if (WiFi.status() == WL_CONNECTED) {
        ipStr = "IP Address: " + WiFi.localIP().toString();
    }

    fontMgr.drawTextCentered(display, ipStr.c_str(), startY + lineSpacing, FONT_SIZE_BODY, GxEPD_BLACK);

    // FIX: Properly constructed String objects before concatenation
    String ssidStr = String("Network (SSID): ") + String(AP_SSID);
    String passStr = String("Password: ") + String(WebMgr::devicePassword());

    fontMgr.drawTextCentered(display, ssidStr.c_str(), startY + (lineSpacing * 2), FONT_SIZE_BODY,
                             GxEPD_BLACK);
    fontMgr.drawTextCentered(display, passStr.c_str(), startY + (lineSpacing * 3), FONT_SIZE_BODY,
                             GxEPD_BLACK);
}