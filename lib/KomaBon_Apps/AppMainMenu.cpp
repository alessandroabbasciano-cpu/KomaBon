#include "AppMainMenu.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "WebMgr.h"
#include "../KomaBon_Core/BatteryMgr.h"
#include "../KomaBon_Core/InputMgr.h"
#include "../KomaBon_Core/FontMgr.h"
#include "../Book32_Web/WebMgr.h"
#include "../KomaBon_Core/DeviceCred.h"
#include "../../include/Config.h"
#include "../../include/NetworkState.h"
#include <WiFi.h>
#include "icon_update.h"
#include "../Book32_Update/GitHubMgr.h"

struct MenuDirtyRect {
    int x;
    int y;
    int w;
    int h;
};

static MenuDirtyRect menuItemRect(int index, int screenW) {
    const int ICON_SIZE = 160;
    const int COLS = 2;
    const int ROW_HEIGHT = 240;
    const int START_Y = 180;
    int colWidth = screenW / COLS;
    int idx = index - 1;
    int col = idx % COLS;
    int row = idx / COLS;
    int x = col * colWidth + (colWidth - ICON_SIZE) / 2;
    int y = START_Y + row * ROW_HEIGHT;
    return {x - 14, y - 14, ICON_SIZE + 15, ICON_SIZE + 40};
}

static MenuDirtyRect unionRect(MenuDirtyRect a, MenuDirtyRect b) {
    int x1 = min(a.x, b.x);
    int y1 = min(a.y, b.y);
    int x2 = max(a.x + a.w, b.x + b.w);
    int y2 = max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

static bool isReaderActive() {
    App* current = AppMgr::getInstance().getCurrentApp();
    return current && strcmp(current->getName(), "eReader") == 0;
}

String AppMainMenu::getWifiFooterText() const {
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        if (ip != INADDR_NONE) {
            return ip.toString();
        }
    }
    if (_hotspotActive) {
        return String("Wi-Fi: ") + AP_SSID + " / " + WebMgr::devicePassword() + "  ->  192.168.4.1";
    }
    return "WiFi offline (Strict On-Demand)";
}

void AppMainMenu::startHotspot() {
    if (_hotspotActive) return;
    if (isReaderActive()) return;

    WebMgr::getInstance().sendLog("Main menu: starting KomaBon management hotspot (offline)");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, WebMgr::devicePassword());
    delay(100);
    WebMgr::getInstance().startNetwork();
    _hotspotActive = true;

    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _footerOnlyRedraw = !_firstDraw;
    _needsRedraw = true;
}

void AppMainMenu::stopHotspot() {
    if (!_hotspotActive) return;

    WebMgr::getInstance().sendLog("Main menu: stopping management hotspot");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    _hotspotActive = false;
}

void AppMainMenu::start() {
    selectedIndex = 1;
    _needsRedraw = true;
    _firstDraw = true;
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _previousSelectedIndex = selectedIndex;
    _lastWifiConnected = WiFi.status() == WL_CONNECTED;
    _lastIp = _lastWifiConnected ? WiFi.localIP().toString() : "";
    _lastWifiFooterText = "";
    _lastBatteryPoll = millis();
    _lastBatteryStatus = BatteryMgr::getInstance().refreshNow();
    InputMgr::getInstance().setCallback(std::bind(&AppMainMenu::handleInput, this, std::placeholders::_1));

    // Wi-Fi is strictly off on menu entry to protect battery life.
}

void AppMainMenu::stop() {
    stopHotspot();
}

void AppMainMenu::forceRedraw() {
    _firstDraw = true;
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _footerOnlyRedraw = false;
    _needsRedraw = true;
}

void AppMainMenu::handleInput(InputAction action) {
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();

    WebMgr::getInstance().sendLogf("AppMainMenu::handleInput - action: %d\n", action);

    int maxSelectable = apps.size() - 1 + (_updateAvailable ? 1 : 0);

    if (action == INPUT_NEXT || action == INPUT_RIGHT) {
        selectedIndex++;
        if (selectedIndex > maxSelectable) selectedIndex = 1;
        if (selectedIndex == 0) selectedIndex = 1;
        _selectionOnlyRedraw = !_firstDraw;
        _needsRedraw = true;
    } else if (action == INPUT_PREV || action == INPUT_LEFT) {
        selectedIndex--;
        if (selectedIndex < 1) selectedIndex = maxSelectable;
        _selectionOnlyRedraw = !_firstDraw;
        _needsRedraw = true;
    } else if (action == INPUT_SELECT) {
        if (_updateAvailable && selectedIndex == (int)apps.size()) {
            WebMgr::getInstance().sendLog("AppMainMenu: Launching OTA task...");
            xTaskCreatePinnedToCore(
                [](void* param) {
                    GitHubMgr::getInstance().triggerUpdate(SYSTEM_VERSION);
                    vTaskDelete(NULL);
                },
                "OTA_Menu_Task", 16384, nullptr, 1, nullptr, 1);
        } else if (selectedIndex > 0 && selectedIndex < (int)apps.size()) {
            appMgr.switchTo(selectedIndex);
        }
    } else if (action == INPUT_GO_TO_MAIN_MENU) {
        WebMgr::getInstance().sendLog("AppMainMenu: INPUT_GO_TO_MAIN_MENU - already at main menu");
    }
}

void AppMainMenu::update() {
    unsigned long now = millis();

    if (now - _lastNetworkPoll >= 1000) {
        _lastNetworkPoll = now;

        bool connected = WiFi.status() == WL_CONNECTED;
        String ip = connected ? WiFi.localIP().toString() : "";

        String footerText = getWifiFooterText();
        if (connected != _lastWifiConnected || ip != _lastIp || footerText != _lastWifiFooterText) {
            _lastWifiConnected = connected;
            _lastIp = ip;
            _selectionOnlyRedraw = false;
            _batteryOnlyRedraw = false;
            _footerOnlyRedraw = !_firstDraw;
            _needsRedraw = true;
        }
    }

    if (now - _lastBatteryPoll >= 10000) {
        _lastBatteryPoll = now;
        BatteryStatus status = BatteryMgr::getInstance().refreshNow();
        bool changed = status.charging != _lastBatteryStatus.charging ||
                       status.percentage != _lastBatteryStatus.percentage ||
                       fabsf(status.voltage - _lastBatteryStatus.voltage) >= 0.03f;
        if (changed) {
            _lastBatteryStatus = status;
            _selectionOnlyRedraw = false;
            _batteryOnlyRedraw = !_firstDraw;
            _needsRedraw = true;
        }
    }
}

void AppMainMenu::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();

    int16_t screenW = display.width();
    int16_t screenH = display.height();

    bool updateAvailable;
    String updateVersion;
    {
        Book32Guard guard(_updateMutex);
        updateAvailable = _updateAvailable;
        updateVersion = _updateVersion;
    }

    const int ICON_SIZE = 160;
    const int COLS = 2;
    const int ROW_HEIGHT = 240;
    const int START_Y = 180;

    if (_firstDraw) {
        display.setFullWindow();
        _firstDraw = false;
    } else if (_selectionOnlyRedraw) {
        MenuDirtyRect dirty =
            unionRect(menuItemRect(_previousSelectedIndex, screenW), menuItemRect(selectedIndex, screenW));
        dirty.x = max(0, dirty.x);
        dirty.y = max(0, dirty.y);
        if (dirty.x + dirty.w > screenW) dirty.w = screenW - dirty.x;
        if (dirty.y + dirty.h > screenH) dirty.h = screenH - dirty.y;
        display.setPartialWindow(dirty.x, dirty.y, dirty.w, dirty.h);
    } else if (_batteryOnlyRedraw) {
        display.setPartialWindow(screenW - 150, 0, 150, 42);
    } else if (_footerOnlyRedraw) {
        display.setPartialWindow(0, screenH - 70, screenW, 70);
    } else {
        display.setPartialWindow(0, 0, screenW, screenH);
    }
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _footerOnlyRedraw = false;
    _previousSelectedIndex = selectedIndex;

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        fontMgr.drawText(display, "KomaBon", 15, 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        int komaBonWidth = fontMgr.getTextWidth("KomaBon", FONT_SIZE_SUBTITLE);
        char versionStr[16];
        snprintf(versionStr, sizeof(versionStr), " v%s", SYSTEM_VERSION);
        fontMgr.drawText(display, versionStr, 15 + komaBonWidth, 35, FONT_SIZE_SMALL, GxEPD_BLACK);

        BatteryMgr::getInstance().drawStatusBar(display, screenW - 105, 10);
        int colWidth = screenW / COLS;

        for (size_t i = 0; i < apps.size(); i++) {
            if (i == 0) continue;

            App* app = apps[i];
            int idx = i - 1;
            int col = idx % COLS;
            int row = idx / COLS;

            int x = col * colWidth + (colWidth - ICON_SIZE) / 2;
            int y = START_Y + row * ROW_HEIGHT;

            if ((int)i == selectedIndex) {
                display.fillRect(x, y + ICON_SIZE + 20, ICON_SIZE, 6, GxEPD_BLACK);
            }

            const uint8_t* icon = app->getIconImage();
            if (icon) {
                display.drawBitmap(x, y, icon, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);
            } else {
                display.drawRect(x, y, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);
            }

            const char* name = app->getName();
            int nameWidth = fontMgr.getTextWidth(name, FONT_SIZE_MENU);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, name, nameX, y + ICON_SIZE + 15, FONT_SIZE_MENU, GxEPD_BLACK);
        }

        if (updateAvailable) {
            int i = apps.size();
            int idx = i - 1;
            int col = idx % COLS;
            int row = idx / COLS;

            int x = col * colWidth + (colWidth - ICON_SIZE) / 2;
            int y = START_Y + row * ROW_HEIGHT;

            if ((int)i == selectedIndex) {
                display.fillRect(x, y + ICON_SIZE + 20, ICON_SIZE, 6, GxEPD_BLACK);
            }

            display.drawBitmap(x, y, icon_update_160x160, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);

            String updateText = "Update " + updateVersion;
            int nameWidth = fontMgr.getTextWidth(updateText.c_str(), FONT_SIZE_MENU);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, updateText.c_str(), nameX, y + ICON_SIZE + 15, FONT_SIZE_MENU,
                             GxEPD_BLACK);
        }

        fontMgr.drawTextCentered(display, "Joy: Move  |  Center: Select", screenH - 45, FONT_SIZE_SMALL,
                                 GxEPD_BLACK);
        String ipStr = getWifiFooterText();
        fontMgr.drawTextCentered(display, ipStr.c_str(), screenH - 20, FONT_SIZE_SMALL, GxEPD_BLACK);
        _lastWifiFooterText = ipStr;

    } while (display.nextPage());
}