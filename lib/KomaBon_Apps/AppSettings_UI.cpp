#include "AppSettings.h"
#include "DisplayMgr.h"
#include "../KomaBon_Core/FontMgr.h"
#include "../KomaBon_Core/BatteryMgr.h"
#include "../KomaBon_Core/KomaBonFS.h"
#include "../KomaBon_Web/WebMgr.h"
#include "../../include/Config.h"
#include <WiFi.h>

static const char* ROW_LABELS[] = {"Font size",     "Font family", "Orientation", "Refresh screen",
                                   "Sleep timeout", "Network",     "System",      "Joystick",
                                   "Save",          "Discard"};

static const char* FONT_FAMILY_NAMES[] = {"FreeSans",       "Merriweather", "Literata",
                                          "Source Serif 4", "Gelasio",      "Open Sans"};

static const int LIST_START_Y = 130;
static const int ROW_HEIGHT = 52;

struct SettingsDirtyRect {
    int x;
    int y;
    int w;
    int h;
};

static SettingsDirtyRect settingsRowRect(int index, int screenW) {
    int y = LIST_START_Y + index * ROW_HEIGHT;
    return {12, y - 30, screenW - 24, ROW_HEIGHT - 8};
}

static SettingsDirtyRect unionRect(SettingsDirtyRect a, SettingsDirtyRect b) {
    int x1 = std::min(a.x, b.x);
    int y1 = std::min(a.y, b.y);
    int x2 = std::max(a.x + a.w, b.x + b.w);
    int y2 = std::max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

String AppSettings::valueForRow(int index) const {
    switch (index) {
        case ROW_FONT_SIZE:
            return String(_reader.fontSize) + " pt";
        case ROW_FONT_FAMILY:
            return String(FONT_FAMILY_NAMES[SettingsStore::clampFontFamily(_reader.fontFamily)]);
        case ROW_ROTATION: {
            const char* rotNames[] = {"0 deg", "90 deg", "180 deg", "270 deg"};
            int rot = _display.rotation;
            if (rot < 0 || rot > 3) rot = 3;
            return String(rotNames[rot]);
        }
        case ROW_REFRESH:
            return String(_reader.refreshFrequency) + " pages";
        case ROW_SLEEP:
            return _sleep.timeout == 0 ? String("Off") : String(_sleep.timeout) + " min";
        case ROW_NETWORK:
        case ROW_SYSTEM:
        case ROW_JOYSTICK:
            return ">";
        default:
            return "";
    }
}

void AppSettings::drawHeader(const char* title) {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();

    BatteryMgr::getInstance().drawStatusBar(display, 0, 0);

    font.drawText(display, title, 20, 45, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
    display.drawLine(20, 62, display.width() - 20, 62, GxEPD_BLACK);

    if (_dirty) {
        font.drawText(display, "* unsaved changes", 20, 90, FONT_SIZE_SMALL, GxEPD_BLACK);
    }
}

void AppSettings::drawFooter(const char* hint) {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
    int h = display.height();

    if (_statusMessage.length() > 0) {
        String msg = FontMgr::utf8ToLatin1(_statusMessage);
        font.drawTextCentered(display, msg.c_str(), h - 50, FONT_SIZE_SMALL, GxEPD_BLACK);
    }
    font.drawTextCentered(display, hint, h - 22, FONT_SIZE_SMALL, GxEPD_BLACK);
}

void AppSettings::drawMainScreen() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();

    BatteryMgr::getInstance().drawStatusBar(display, 0, 0);

    int w = display.width();

    drawHeader("Settings");

    for (int i = 0; i < ROW_COUNT; i++) {
        int y = LIST_START_Y + i * ROW_HEIGHT;
        display.setTextColor(GxEPD_BLACK);

        if (i == _selectedIndex) {
            display.drawRect(12, y - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            display.drawRect(13, y - 29, w - 26, ROW_HEIGHT - 10, GxEPD_BLACK);
        }

        String label = String(ROW_LABELS[i]);
        if (rowChanged(i)) label = "*" + label;
        font.drawText(display, label.c_str(), 26, y, FONT_SIZE_BODY, GxEPD_BLACK);

        String value = valueForRow(i);
        if (value.length() > 0) {
            int vw = font.getTextWidth(value.c_str(), FONT_SIZE_BODY);
            font.drawText(display, value.c_str(), w - 26 - vw, y, FONT_SIZE_BODY, GxEPD_BLACK);
        }
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("Up/Down: move  |  Center: ok  |  Hold Left: exit");
}

void AppSettings::drawFontScreen() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
    int w = display.width();

    drawHeader("Font family");

    for (int i = 0; i < 6; i++) {
        int y = LIST_START_Y + i * ROW_HEIGHT;
        display.setTextColor(GxEPD_BLACK);

        if (i == _subSelectedIndex) {
            display.drawRect(12, y - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            display.drawRect(13, y - 29, w - 26, ROW_HEIGHT - 10, GxEPD_BLACK);
        }

        String label = String(FONT_FAMILY_NAMES[i]);
        if (i == _reader.fontFamily) label += "  (current)";

        font.drawText(display, label.c_str(), 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("Up/Down: move  |  Center: ok  |  Hold Left: back");
}

void AppSettings::drawNetworkScreen() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();

    BatteryMgr::getInstance().drawStatusBar(display, 0, 0);

    drawHeader("Network");

    bool connected = WiFi.status() == WL_CONNECTED;
    bool wifiIsOn = (WiFi.getMode() != WIFI_OFF);
    int y = LIST_START_Y;

    font.drawText(display, "Status:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    font.drawText(display, connected ? "Connected" : (wifiIsOn ? "AP Mode / No Link" : "Disconnected"), 200,
                  y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    font.drawText(display, "SSID:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    String ssid = connected ? WiFi.SSID() : String(AP_SSID);
    font.drawText(display, FontMgr::utf8ToLatin1(ssid).c_str(), 200, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    font.drawText(display, "Pass:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    font.drawText(display, WebMgr::devicePassword(), 200, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    font.drawText(display, "IP:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    font.drawText(display, connected ? WiFi.localIP().toString().c_str() : (wifiIsOn ? "192.168.4.1" : "-"),
                  200, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    font.drawText(display, "MAC:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    font.drawText(display, WiFi.macAddress().c_str(), 200, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    if (connected) {
        font.drawText(display, "Signal:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
        String rssi = String(WiFi.RSSI()) + " dBm";
        font.drawText(display, rssi.c_str(), 200, y, FONT_SIZE_BODY, GxEPD_BLACK);
    }

    drawFooter("Hold Left: back");
}

void AppSettings::drawSystemScreen() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();

    BatteryMgr::getInstance().drawStatusBar(display, 0, 0);

    int w = display.width();

    drawHeader("System");

    int y = LIST_START_Y;
    font.drawText(display, "Version:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    font.drawText(display, SYSTEM_VERSION, 220, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    font.drawText(display, "Battery:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    BatteryStatus bat = BatteryMgr::getInstance().getStatus();
    String batStr = String(bat.percentage) + "%  (" + String(bat.voltage, 2) + " V)";
    font.drawText(display, batStr.c_str(), 220, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    font.drawText(display, "Books:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    size_t usedKb = KomaBonStorage::getUsedBytes() / 1024;
    size_t totalKb = KomaBonStorage::getTotalBytes() / 1024;
    String fsStr = String((unsigned long)usedKb) + " / " + String((unsigned long)totalKb) + " KB";
    font.drawText(display, fsStr.c_str(), 220, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT + 20;

    const char* actions[3] = {"Check for updates", "Restart", "Forget network"};
    for (int i = 0; i < 3; i++) {
        int ay = y + i * ROW_HEIGHT;

        display.setTextColor(GxEPD_BLACK);

        if (i == _subSelectedIndex) {
            display.drawRect(12, ay - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            display.drawRect(13, ay - 29, w - 26, ROW_HEIGHT - 10, GxEPD_BLACK);
        }

        font.drawText(display, actions[i], 26, ay, FONT_SIZE_BODY, GxEPD_BLACK);
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("Up/Down: move  |  Center: execute  |  Hold Left: back");
}

void AppSettings::drawConfirmScreen() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
    int w = display.width();

    drawHeader("Unsaved changes");

    font.drawTextCentered(display, "You have changes that you haven't saved.", LIST_START_Y, FONT_SIZE_BODY,
                          GxEPD_BLACK);

    const char* options[3] = {"Save and exit", "Discard and exit", "Cancel"};
    int y = LIST_START_Y + 70;
    for (int i = 0; i < 3; i++) {
        int oy = y + i * ROW_HEIGHT;
        display.setTextColor(GxEPD_BLACK);

        if (i == _subSelectedIndex) {
            display.drawRect(12, oy - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            display.drawRect(13, oy - 29, w - 26, ROW_HEIGHT - 10, GxEPD_BLACK);
        }

        font.drawText(display, options[i], 26, oy, FONT_SIZE_BODY, GxEPD_BLACK);
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("Up/Down: move  |  Center: confirm  |  Hold Left: back");
}

void AppSettings::drawConfirmForgetWifiScreen() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
    int w = display.width();

    drawHeader("Forget network?");

    font.drawTextCentered(display, "The device will disconnect from the current network", LIST_START_Y,
                          FONT_SIZE_BODY, GxEPD_BLACK);
    font.drawTextCentered(display, "and restart in setup mode.", LIST_START_Y + 30, FONT_SIZE_BODY,
                          GxEPD_BLACK);

    const char* options[2] = {"Forget and restart", "Cancel"};
    int y = LIST_START_Y + 100;
    for (int i = 0; i < 2; i++) {
        int oy = y + i * ROW_HEIGHT;
        display.setTextColor(GxEPD_BLACK);

        if (i == _subSelectedIndex) {
            display.drawRect(12, oy - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            display.drawRect(13, oy - 29, w - 26, ROW_HEIGHT - 10, GxEPD_BLACK);
        }

        font.drawText(display, options[i], 26, oy, FONT_SIZE_BODY, GxEPD_BLACK);
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("Up/Down: move  |  Center: confirm  |  Hold Left: back");
}

void AppSettings::drawJoyCalScreen() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();

    BatteryMgr::getInstance().drawStatusBar(display, 0, 0);

    drawHeader("Joystick Setup");

    int y = LIST_START_Y;
    font.drawTextCentered(display, "Hardware Calibration", y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += 50;

    const char* instructions[] = {"Press and HOLD the CENTER button...",
                                  "Press and HOLD UP...",
                                  "Press and HOLD DOWN...",
                                  "Press and HOLD LEFT...",
                                  "Press and HOLD RIGHT...",
                                  "Calibration Complete!"};

    font.drawTextCentered(display, instructions[_joyCalStep], y, FONT_SIZE_BODY, GxEPD_BLACK);

    if (_joyCalStep < 5) {
        font.drawTextCentered(display, "(Keep holding until screen updates)", y + 40, FONT_SIZE_SMALL,
                              GxEPD_BLACK);
    } else {
        font.drawTextCentered(display, "Settings saved.", y + 40, FONT_SIZE_SMALL, GxEPD_BLACK);
    }

    display.setTextColor(GxEPD_BLACK);

    if (_joyCalStep < 5) {
        drawFooter("KEY3: abort");
    } else {
        drawFooter("Center: exit to menu");
    }
}

void AppSettings::drawOtaModal() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();

    int w = display.width();
    int h = display.height();

    int mw = 400;
    int mh = 200;
    int mx = (w - mw) / 2;
    int my = (h - mh) / 2;

    display.fillRect(mx, my, mw, mh, GxEPD_WHITE);
    display.drawRect(mx, my, mw, mh, GxEPD_BLACK);
    display.drawRect(mx + 1, my + 1, mw - 2, mh - 2, GxEPD_BLACK);

    if (_otaChecking) {
        font.drawTextCentered(display, "Checking for updates...", my + 100, FONT_SIZE_BODY, GxEPD_BLACK);
    } else if (!_otaUpdateAvailable) {
        font.drawTextCentered(display, "No updates found.", my + 60, FONT_SIZE_BODY, GxEPD_BLACK);

        int btnW = 120;
        int bx = mx + (mw - btnW) / 2;
        int by = my + 110;
        display.drawRect(bx, by, btnW, 46, GxEPD_BLACK);
        display.drawRect(bx + 1, by + 1, btnW - 2, 44, GxEPD_BLACK);
        display.drawRect(bx + 2, by + 2, btnW - 4, 42, GxEPD_BLACK);
        font.drawTextCentered(display, "OK", by + 30, FONT_SIZE_BODY, GxEPD_BLACK);
    } else {
        font.drawTextCentered(display, "Update found!", my + 45, FONT_SIZE_BODY, GxEPD_BLACK);
        font.drawTextCentered(display, "Start download?", my + 75, FONT_SIZE_BODY, GxEPD_BLACK);

        int btnW = 120;
        int btnY = my + 120;
        int yesX = mx + 40;
        int noX = mx + mw - 40 - btnW;

        display.drawRect(yesX, btnY, btnW, 46, GxEPD_BLACK);
        font.drawTextCentered(display, "Yes", btnY + 30, FONT_SIZE_BODY, GxEPD_BLACK);

        display.drawRect(noX, btnY, btnW, 46, GxEPD_BLACK);
        font.drawTextCentered(display, "No", btnY + 30, FONT_SIZE_BODY, GxEPD_BLACK);

        int selX = (_otaModalOption == 0) ? yesX : noX;
        display.drawRect(selX + 1, btnY + 1, btnW - 2, 44, GxEPD_BLACK);
        display.drawRect(selX + 2, btnY + 2, btnW - 4, 42, GxEPD_BLACK);
    }
}

void AppSettings::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();

    if (_selectionOnlyRedraw) {
        if (_screen == SCREEN_MAIN) {
            int prevIndex = _previousSelectedIndex;
            int currIndex = _selectedIndex;
            int screenW = display.width();
            SettingsDirtyRect dirty =
                unionRect(settingsRowRect(prevIndex, screenW), settingsRowRect(currIndex, screenW));
            display.setPartialWindow(dirty.x, dirty.y, dirty.w, dirty.h);
        } else if (_screen == SCREEN_OTA_MODAL) {
            int w = display.width();
            int h = display.height();
            display.setPartialWindow((w - 400) / 2, (h - 200) / 2, 400, 200);
        } else {
            int h = display.height();
            display.setPartialWindow(0, 100, display.width(), h - 100);
        }
    } else {
        display.setFullWindow();
    }

    _selectionOnlyRedraw = false;

    _previousSelectedIndex = _selectedIndex;
    _previousSubSelectedIndex = _subSelectedIndex;

    display.firstPage();
    do {
        if (_screen != SCREEN_OTA_MODAL) {
            display.fillScreen(GxEPD_WHITE);
            display.setTextColor(GxEPD_BLACK);
        }

        switch (_screen) {
            case SCREEN_NETWORK:
                drawNetworkScreen();
                break;
            case SCREEN_SYSTEM:
                drawSystemScreen();
                break;
            case SCREEN_CONFIRM:
                drawConfirmScreen();
                break;
            case SCREEN_CONFIRM_FORGET_WIFI:
                drawConfirmForgetWifiScreen();
                break;
            case SCREEN_JOYCAL:
                drawJoyCalScreen();
                break;
            case SCREEN_OTA_MODAL:
                drawSystemScreen();
                drawOtaModal();
                break;
            case SCREEN_MAIN:
            default:
                drawMainScreen();
                break;
        }
    } while (display.nextPage());
}