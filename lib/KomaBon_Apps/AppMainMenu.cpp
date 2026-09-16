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

// Dynamic bounds calculation for partial E-ink refresh
static MenuDirtyRect menuItemRect(int index, int screenW) {
    if (index == 0) {
        // Widget "Currently Reading" bounds
        return {10, 50, screenW - 20, 110};
    }

    // Vertical List App bounds
    const int ROW_HEIGHT = 75;
    const int START_Y = 175;
    int idx = index - 1;
    int y = START_Y + idx * ROW_HEIGHT;
    return {10, y - 5, screenW - 20, ROW_HEIGHT + 10};
}

static MenuDirtyRect unionRect(MenuDirtyRect a, MenuDirtyRect b) {
    int x1 = std::min(a.x, b.x);
    int y1 = std::min(a.y, b.y);
    int x2 = std::max(a.x + a.w, b.x + b.w);
    int y2 = std::max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

static bool isReaderActive() {
    App* current = AppMgr::getInstance().getCurrentApp();
    return current && strcmp(current->getName(), "eReader") == 0;
}

void AppMainMenu::loadResumeData() {
    String lastKey = ProgressStore::getInstance().lastBook();
    if (lastKey.length() > 0) {
        _hasResume = true;
        _lastBookTitle = lastKey;

        BookProgress prog;
        if (ProgressStore::getInstance().get(lastKey, prog)) {
            _lastBookPage = prog.globalPage;
        }
    } else {
        _hasResume = false;
    }
}

String AppMainMenu::getWifiFooterText() const {
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        if (ip != INADDR_NONE) {
            return String("IP: ") + ip.toString() + " | Web UI Ready";
        }
    }
    if (_hotspotActive) {
        return String("AP: ") + AP_SSID + " | Pwd: " + WebMgr::devicePassword() + " | 192.168.4.1";
    }
    return "Wi-Fi Offline";
}

void AppMainMenu::startHotspot() {
    if (_hotspotActive) return;
    if (isReaderActive()) return;

    Serial.println("Main menu: starting KomaBon management hotspot (offline)");
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

    Serial.println("Main menu: stopping management hotspot");
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    _hotspotActive = false;
}

void AppMainMenu::start() {
    loadResumeData();
    selectedIndex = _hasResume ? 0 : 1;

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

    int minSelectable = _hasResume ? 0 : 1;
    int maxSelectable = apps.size() - 1 + (_updateAvailable ? 1 : 0);

    // FIX: Using strictly defined logical inputs mapping to the physical joystick
    if (action == INPUT_NEXT || action == INPUT_RIGHT) {
        selectedIndex++;
        if (selectedIndex > maxSelectable) selectedIndex = minSelectable;
        _selectionOnlyRedraw = !_firstDraw;
        _needsRedraw = true;
    } else if (action == INPUT_PREV || action == INPUT_LEFT) {
        selectedIndex--;
        if (selectedIndex < minSelectable) selectedIndex = maxSelectable;
        _selectionOnlyRedraw = !_firstDraw;
        _needsRedraw = true;
    } else if (action == INPUT_SELECT) {
        if (selectedIndex == 0 && _hasResume) {
            // Signal the eReader to auto-resume, then switch to it (Index 1)
            ProgressStore::getInstance().setResumeOnBoot(true);
            appMgr.switchTo(1);
        } else if (_updateAvailable && selectedIndex == (int)apps.size()) {
            Serial.println("AppMainMenu: Launching OTA task...");
            xTaskCreatePinnedToCore(
                [](void* param) {
                    GitHubMgr::getInstance().triggerUpdate(SYSTEM_VERSION);
                    vTaskDelete(NULL);
                },
                "OTA_Menu_Task", 16384, nullptr, 1, nullptr, 1);
        } else if (selectedIndex > 0 && selectedIndex < (int)apps.size()) {
            appMgr.switchTo(selectedIndex);
        }
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

    if (_firstDraw) {
        display.setFullWindow();
        _firstDraw = false;
    } else if (_selectionOnlyRedraw) {
        MenuDirtyRect dirty =
            unionRect(menuItemRect(_previousSelectedIndex, screenW), menuItemRect(selectedIndex, screenW));
        dirty.x = std::max(0, dirty.x);
        dirty.y = std::max(0, dirty.y);
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

        // --- 1. HEADER ---
        fontMgr.drawText(display, "KomaBon", 15, 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        int komaBonWidth = fontMgr.getTextWidth("KomaBon", FONT_SIZE_SUBTITLE);
        char versionStr[16];
        snprintf(versionStr, sizeof(versionStr), " v%s", SYSTEM_VERSION);
        fontMgr.drawText(display, versionStr, 15 + komaBonWidth, 35, FONT_SIZE_SMALL, GxEPD_BLACK);

        // Fixed Synchronous Battery Drawing
        BatteryMgr::getInstance().drawStatusBar(display, 0, 0);

        // --- 2. WIDGET: CURRENTLY READING ---
        if (_hasResume) {
            int wx = 15;
            int wy = 55;
            int ww = screenW - 30;
            int wh = 100;

            uint16_t fgColor = (selectedIndex == 0) ? GxEPD_WHITE : GxEPD_BLACK;
            uint16_t bgColor = (selectedIndex == 0) ? GxEPD_BLACK : GxEPD_WHITE;

            display.fillRect(wx, wy, ww, wh, bgColor);
            if (selectedIndex != 0) {
                display.drawRect(wx, wy, ww, wh, GxEPD_BLACK);
                display.drawRect(wx + 1, wy + 1, ww - 2, wh - 2, GxEPD_BLACK);
            }

            fontMgr.drawText(display, "Currently Reading", wx + 20, wy + 35, FONT_SIZE_SMALL, fgColor);

            // Truncate title if too long to fit widget width
            String safeTitle = _lastBookTitle;
            if (safeTitle.length() > 35) safeTitle = safeTitle.substring(0, 32) + "...";
            fontMgr.drawText(display, safeTitle.c_str(), wx + 20, wy + 65, FONT_SIZE_BODY, fgColor);

            String pageStr = "Page " + String(_lastBookPage);
            fontMgr.drawText(display, pageStr.c_str(), wx + 20, wy + 85, FONT_SIZE_SMALL, fgColor);
        }

        // --- 3. VERTICAL LIST APPS ---
        const int ROW_HEIGHT = 75;
        const int START_Y = 175;

        for (size_t i = 1; i < apps.size(); i++) {
            App* app = apps[i];
            int idx = i - 1;
            int y = START_Y + idx * ROW_HEIGHT;
            int x = 25;

            uint16_t fgColor = ((int)i == selectedIndex) ? GxEPD_WHITE : GxEPD_BLACK;
            uint16_t bgColor = ((int)i == selectedIndex) ? GxEPD_BLACK : GxEPD_WHITE;

            // Highlight Background
            if ((int)i == selectedIndex) {
                display.fillRect(15, y - 5, screenW - 30, ROW_HEIGHT, bgColor);
            }

            // Real-Time Nearest-Neighbor Downscaling (160x160 -> 64x64)
            const uint8_t* icon = app->getIconImage();
            int iconSize = 64;

            if (icon) {
                for (int cy = 0; cy < iconSize; cy++) {
                    int srcY = (cy * 160) / iconSize;
                    for (int cx = 0; cx < iconSize; cx++) {
                        int srcX = (cx * 160) / iconSize;
                        int byteIdx = (srcY * 160 + srcX) / 8;
                        int bitIdx = 7 - ((srcY * 160 + srcX) % 8);

                        // Extract bit and draw if it's foreground
                        if (icon[byteIdx] & (1 << bitIdx)) {
                            display.drawPixel(x + cx, y + cy, fgColor);
                        }
                    }
                }
            } else {
                // Fallback placeholder box
                display.drawRect(x, y, iconSize, iconSize, fgColor);
            }

            // App Name Text aligned to the right of the icon
            int textY = y + (iconSize / 2) + 8; // Vertically centered
            fontMgr.drawText(display, app->getName(), x + iconSize + 25, textY, FONT_SIZE_BODY, fgColor);
        }

        // --- 4. OTA UPDATE WIDGET ---
        if (updateAvailable) {
            int i = apps.size();
            int idx = i - 1;
            int y = START_Y + idx * ROW_HEIGHT;
            int x = 25;

            uint16_t fgColor = ((int)i == selectedIndex) ? GxEPD_WHITE : GxEPD_BLACK;
            uint16_t bgColor = ((int)i == selectedIndex) ? GxEPD_BLACK : GxEPD_WHITE;

            if ((int)i == selectedIndex) {
                display.fillRect(15, y - 5, screenW - 30, ROW_HEIGHT, bgColor);
            }

            // Downscale OTA Icon
            int iconSize = 64;
            for (int cy = 0; cy < iconSize; cy++) {
                int srcY = (cy * 160) / iconSize;
                for (int cx = 0; cx < iconSize; cx++) {
                    int srcX = (cx * 160) / iconSize;
                    int byteIdx = (srcY * 160 + srcX) / 8;
                    int bitIdx = 7 - ((srcY * 160 + srcX) % 8);

                    if (icon_update_160x160[byteIdx] & (1 << bitIdx)) {
                        display.drawPixel(x + cx, y + cy, fgColor);
                    }
                }
            }

            String updateText = "Update to " + updateVersion;
            int textY = y + (iconSize / 2) + 8;
            fontMgr.drawText(display, updateText.c_str(), x + iconSize + 25, textY, FONT_SIZE_BODY, fgColor);
        }

        // --- 5. FOOTER ---
        fontMgr.drawTextCentered(display, "Up/Down: Move  |  Center: Select", screenH - 45, FONT_SIZE_SMALL,
                                 GxEPD_BLACK);
        String ipStr = getWifiFooterText();
        fontMgr.drawTextCentered(display, ipStr.c_str(), screenH - 20, FONT_SIZE_SMALL, GxEPD_BLACK);
        _lastWifiFooterText = ipStr;

    } while (display.nextPage());
}