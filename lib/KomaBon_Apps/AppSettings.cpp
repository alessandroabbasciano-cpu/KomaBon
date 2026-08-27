#include "AppSettings.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "icon_settings.h"
#include "../KomaBon_Core/FontMgr.h"
#include "../KomaBon_Core/BatteryMgr.h"
#include "../KomaBon_Core/KomaBonFS.h"
#include "../Book32_Web/WebMgr.h"
#include "../Book32_Update/GitHubMgr.h"
#include "../../include/Config.h"
#include <WiFi.h>
#include <WiFiManager.h>

// --- Row identifiers --------------------------------------------------------
// Kept as an enum so the draw loop, the input handler and the value formatter
// can't drift out of sync when a row is inserted.
enum SettingsRow {
    ROW_FONT_SIZE = 0,
    ROW_FONT_FAMILY,
    ROW_ROTATION,
    ROW_REFRESH,
    ROW_SLEEP,
    ROW_WIFI,
    ROW_NETWORK,
    ROW_SYSTEM,
    ROW_SAVE,
    ROW_DISCARD,
    ROW_COUNT
};

static const char* ROW_LABELS[ROW_COUNT] = {
    "Font size",
    "Font family",
    "Orientation",
    "Refresh screen",
    "Sleep timeout",
    "Wi-Fi",
    "Network",
    "System",
    "Save",
    "Discard"
};

static const char* FONT_FAMILY_NAMES[6] = {"FreeSans",       "Merriweather", "Literata",
                                           "Source Serif 4", "Gelasio",      "Open Sans"};

// Cycle sets. Every value here must survive its SettingsStore clamp, otherwise
// cycling would silently snap back and the row would appear stuck.
static const int FONT_SIZES[] = {9, 12, 18};
static const int REFRESH_FREQS[] = {5, 10, 20, 50};
static const int SLEEP_TIMEOUTS[] = {0, 5, 15, 30, 60};

static int cycleInt(const int* values, int count, int current) {
    for (int i = 0; i < count; i++) {
        if (values[i] == current) return values[(i + 1) % count];
    }
    return values[0];  // Current value not in the set: snap to the first
}

// Layout
static const int LIST_START_Y = 130;
static const int ROW_HEIGHT = 52;

// Dirty-rect for the row highlight on SCREEN_MAIN/SCREEN_FONT, matching the
// fillRect() each draws for its selected row (drawMainScreen, drawFontScreen).
// Both screens lay their rows out identically - one per ROW_HEIGHT starting
// at LIST_START_Y - so a single helper covers both.
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
    int x1 = min(a.x, b.x);
    int y1 = min(a.y, b.y);
    int x2 = max(a.x + a.w, b.x + b.w);
    int y2 = max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

AppSettings::AppSettings()
    : _screen(SCREEN_MAIN), _selectedIndex(0), _subSelectedIndex(0), _needsRedraw(true), _dirty(false),
      _selectionOnlyRedraw(false), _previousSelectedIndex(0), _previousSubSelectedIndex(0), _statusUntil(0),
      _lastNetworkPoll(0) {}

const uint8_t* AppSettings::getIconImage() {
    return icon_settings_160x160;
}

void AppSettings::start() {
    SettingsStore& store = SettingsStore::getInstance();
    _reader = store.loadReader();
    _display = store.loadDisplay();
    _sleep = store.loadSleep();

    _readerSaved = _reader;
    _displaySaved = _display;

    _screen = SCREEN_MAIN;
    _selectedIndex = 0;
    _subSelectedIndex = _reader.fontFamily;
    _dirty = false;
    _selectionOnlyRedraw = false;
    _statusMessage = "";
    _statusUntil = 0;
    _needsRedraw = true;

    InputMgr::getInstance().setCallback(
        std::bind(&AppSettings::handleInput, this, std::placeholders::_1));
}

void AppSettings::stop() {
    // Leaving via the main-menu shortcut shouldn't lose edits silently.
    saveDraftIfDirty();
}

void AppSettings::forceRedraw() {
    _selectionOnlyRedraw = false;
    _needsRedraw = true;
}

void AppSettings::recomputeDirty() {
    _dirty = _reader.fontSize != _readerSaved.fontSize ||
             _reader.fontFamily != _readerSaved.fontFamily ||
             _reader.refreshFrequency != _readerSaved.refreshFrequency ||
             _display.rotation != _displaySaved.rotation ||
             _sleep.timeout != SettingsStore::getInstance().loadSleep().timeout;
}

bool AppSettings::rowChanged(int index) const {
    switch (index) {
        case ROW_FONT_SIZE:   return _reader.fontSize != _readerSaved.fontSize;
        case ROW_FONT_FAMILY: return _reader.fontFamily != _readerSaved.fontFamily;
        case ROW_ROTATION:    return _display.rotation != _displaySaved.rotation;
        case ROW_REFRESH:     return _reader.refreshFrequency != _readerSaved.refreshFrequency;
        default:              return false;
    }
}

void AppSettings::setStatus(const String& msg, unsigned long durationMs) {
    _statusMessage = msg;
    _statusUntil = millis() + durationMs;
    _needsRedraw = true;
}

// --- Wi-Fi ------------------------------------------------------------------
bool AppSettings::isWifiOn() const {
    wifi_mode_t mode = WiFi.getMode();
    return mode != WIFI_OFF && mode != WIFI_MODE_NULL;
}

void AppSettings::toggleWifi() {
    if (isWifiOn()) {
        WebMgr::getInstance().stop();
        WiFi.disconnect(false);
        WiFi.mode(WIFI_OFF);
        setStatus("Wi-Fi off");
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.begin();  // Reuses the credentials stored by WiFiManager
        setStatus("Connecting Wi-Fi...");
    }
}

// Erases the SSID/password WiFiManager stored in the ESP32's WiFi driver NVS
// (WiFiManager::resetSettings() -> WiFi.disconnect(true, true) on ESP32), so
// the next boot's autoConnect() can't silently reconnect and opens the
// KomaBon-Setup portal instead. Only reachable via SCREEN_CONFIRM_FORGET_WIFI,
// so an accidental button press can't trigger it.
void AppSettings::forgetNetwork() {
    saveDraftIfDirty();
    setStatus("Forgetting network...", 1000);
    draw();
    WiFiManager wm;
    wm.resetSettings();
    delay(400);
    ESP.restart();
}

// --- Value formatting -------------------------------------------------------
String AppSettings::valueForRow(int index) const {
    switch (index) {
        case ROW_FONT_SIZE:
            return String(_reader.fontSize) + " pt";
        case ROW_FONT_FAMILY:
            return String(FONT_FAMILY_NAMES[SettingsStore::clampFontFamily(_reader.fontFamily)]);
        case ROW_ROTATION:
            return _display.rotation == 3 ? "Button on left" : "Button on right";
        case ROW_REFRESH:
            return String(_reader.refreshFrequency) + " pages";
        case ROW_SLEEP:
            return _sleep.timeout == 0 ? String("Off")
                                       : String(_sleep.timeout) + " min";
        case ROW_WIFI:
            return isWifiOn() ? "On" : "Off";
        case ROW_NETWORK:
        case ROW_SYSTEM:
            return ">";
        default:
            return "";
    }
}

// --- Input ------------------------------------------------------------------
void AppSettings::cycleValue(int index) {
    switch (index) {
        case ROW_FONT_SIZE:
            _reader.fontSize = cycleInt(FONT_SIZES, 3, _reader.fontSize);
            break;
        case ROW_ROTATION:
            _display.rotation = (_display.rotation == 3) ? 1 : 3;
            break;
        case ROW_REFRESH:
            _reader.refreshFrequency = cycleInt(REFRESH_FREQS, 4, _reader.refreshFrequency);
            break;
        case ROW_SLEEP:
            _sleep.timeout = cycleInt(SLEEP_TIMEOUTS, 5, _sleep.timeout);
            break;
        case ROW_WIFI:
            // Acts immediately: it's a command, not a stored preference.
            toggleWifi();
            return;
        default:
            return;
    }
    recomputeDirty();
    _needsRedraw = true;
}

void AppSettings::activate(int index) {
    switch (index) {
        case ROW_FONT_FAMILY:
            _screen = SCREEN_FONT;
            _subSelectedIndex = SettingsStore::clampFontFamily(_reader.fontFamily);
            break;
        case ROW_NETWORK:
            _screen = SCREEN_NETWORK;
            break;
        case ROW_SYSTEM:
            _screen = SCREEN_SYSTEM;
            _subSelectedIndex = 0;
            break;
        case ROW_SAVE:
            if (applyAndSave()) {
                AppMgr::getInstance().switchTo(0);
                return;
            }
            break;
        case ROW_DISCARD:
            discardChanges();
            AppMgr::getInstance().switchTo(0);
            return;
        default:
            cycleValue(index);
            return;
    }
    _needsRedraw = true;
}

void AppSettings::handleInput(InputAction action) {
    if (_screen == SCREEN_CONFIRM) {
        // 0 = save, 1 = discard, 2 = cancel
        if (action == INPUT_NEXT) {
            _subSelectedIndex = (_subSelectedIndex + 1) % 3;
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _subSelectedIndex = (_subSelectedIndex + 2) % 3;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_subSelectedIndex == 0) {
                if (applyAndSave()) AppMgr::getInstance().switchTo(0);
            } else if (_subSelectedIndex == 1) {
                discardChanges();
                AppMgr::getInstance().switchTo(0);
            } else {
                _screen = SCREEN_MAIN;
                _needsRedraw = true;
            }
        }
        return;
    }

    if (_screen == SCREEN_FONT) {
        if (action == INPUT_NEXT) {
            _selectionOnlyRedraw = true;
            _previousSubSelectedIndex = _subSelectedIndex;
            _subSelectedIndex = (_subSelectedIndex + 1) % 6;
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _selectionOnlyRedraw = true;
            _previousSubSelectedIndex = _subSelectedIndex;
            _subSelectedIndex = (_subSelectedIndex + 5) % 6;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            _selectionOnlyRedraw = false; // leaving the screen: full repaint
            _reader.fontFamily = _subSelectedIndex;
            recomputeDirty();
            _screen = SCREEN_MAIN;
            _needsRedraw = true;
        } else if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU) {
            _selectionOnlyRedraw = false;
            _screen = SCREEN_MAIN;
            _needsRedraw = true;
        }
        return;
    }

    if (_screen == SCREEN_NETWORK) {
        if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU ||
            action == INPUT_SELECT) {
            _screen = SCREEN_MAIN;
            _needsRedraw = true;
        }
        return;
    }

    if (_screen == SCREEN_SYSTEM) {
        // 0 = check for updates, 1 = restart, 2 = forget network
        if (action == INPUT_NEXT) {
            _subSelectedIndex = (_subSelectedIndex + 1) % 3;
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _subSelectedIndex = (_subSelectedIndex + 2) % 3;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_subSelectedIndex == 0) {
                if (WiFi.status() != WL_CONNECTED) {
                    setStatus("No network. Turn on Wi-Fi first.");
                } else {
                    setStatus("Searching...", 1000);
                    draw();  // Paint the notice before the blocking HTTP call
                    UpdateInfo info = GitHubMgr::getInstance().checkUpdate(SYSTEM_VERSION);
                    if (info.available) {
                        // Persist edits first: the update reboots the device.
                        saveDraftIfDirty();
                        GitHubMgr::getInstance().triggerUpdate(SYSTEM_VERSION);
                    } else {
                        setStatus("Already on the latest version.");
                    }
                }
            } else if (_subSelectedIndex == 1) {
                saveDraftIfDirty();
                setStatus("Restarting...", 1000);
                draw();
                delay(400);
                ESP.restart();
            } else {
                // Destructive and hard to undo from the device itself, so it
                // gets its own confirmation instead of running immediately.
                _screen = SCREEN_CONFIRM_FORGET_WIFI;
                _subSelectedIndex = 1; // default to "Cancel"
                _needsRedraw = true;
            }
        } else if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU) {
            _screen = SCREEN_MAIN;
            _needsRedraw = true;
        }
        return;
    }

    if (_screen == SCREEN_CONFIRM_FORGET_WIFI) {
        // 0 = forget, 1 = cancel
        if (action == INPUT_NEXT || action == INPUT_PREV) {
            _subSelectedIndex = 1 - _subSelectedIndex;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_subSelectedIndex == 0) {
                forgetNetwork();
                return;
            }
            _screen = SCREEN_SYSTEM;
            _subSelectedIndex = 2;
            _needsRedraw = true;
        } else if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU) {
            _screen = SCREEN_SYSTEM;
            _subSelectedIndex = 2;
            _needsRedraw = true;
        }
        return;
    }

    // Main screen
    if (action == INPUT_NEXT) {
        _selectionOnlyRedraw = true;
        _previousSelectedIndex = _selectedIndex;
        _selectedIndex = (_selectedIndex + 1) % ROW_COUNT;
        _needsRedraw = true;
    } else if (action == INPUT_PREV) {
        _selectionOnlyRedraw = true;
        _previousSelectedIndex = _selectedIndex;
        _selectedIndex = (_selectedIndex + ROW_COUNT - 1) % ROW_COUNT;
        _needsRedraw = true;
    } else if (action == INPUT_SELECT) {
        // activate() may cycle a value, switch screen or leave the app - all
        // of which touch more than the highlighted row, so always full redraw.
        _selectionOnlyRedraw = false;
        activate(_selectedIndex);
    } else if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU) {
        _selectionOnlyRedraw = false;
        if (_dirty) {
            _screen = SCREEN_CONFIRM;
            _subSelectedIndex = 0;
            _needsRedraw = true;
        } else {
            AppMgr::getInstance().switchTo(0);
        }
    }
}

// --- Save / discard ---------------------------------------------------------
bool AppSettings::applyAndSave() {
    SettingsStore& store = SettingsStore::getInstance();

    bool ok;
    {
        // The three writes count as a single operation against the HTTP handlers,
        // which write the same files from the server task. The transaction
        // closes right after: keeping the lock during the repaint below 
        // (a full e-ink refresh taking seconds) would unnecessarily block the web server.
        SettingsStore::Transaction tx;
        ok = store.saveReader(_reader);
        ok = store.saveDisplay(_display) && ok;
        ok = store.saveSleep(_sleep) && ok;
    }

    if (!ok) {
        // Keep the draft intact so the user's edits aren't thrown away.
        setStatus("Save error. Changes kept.", 4000);
        return false;
    }

    // Rotation first: it repaints every screen.
    if (_display.rotation != _displaySaved.rotation) {
        DisplayMgr::getInstance().setRotation(_display.rotation);
        for (App* app : AppMgr::getInstance().getApps()) {
            if (app) app->forceRedraw();
        }
    }

    // Font changes go through the BaseApp hooks; only the reader reacts.
    if (_reader.fontSize != _readerSaved.fontSize) {
        for (App* app : AppMgr::getInstance().getApps()) {
            if (app) app->applyFontSize(_reader.fontSize);
        }
    }
    if (_reader.fontFamily != _readerSaved.fontFamily) {
        for (App* app : AppMgr::getInstance().getApps()) {
            if (app) app->applyFontFamily(_reader.fontFamily);
        }
    }

    BatteryMgr::getInstance().loadSleepSettings();

    _readerSaved = _reader;
    _displaySaved = _display;
    _dirty = false;
    return true;
}

void AppSettings::discardChanges() {
    _reader = _readerSaved;
    _display = _displaySaved;
    _sleep = SettingsStore::getInstance().loadSleep();
    _dirty = false;
}

void AppSettings::saveDraftIfDirty() {
    if (!_dirty) return;
    Serial.println("AppSettings: flushing unsaved draft before sleep/exit");
    applyAndSave();
}

// --- Update -----------------------------------------------------------------
void AppSettings::update() {
    unsigned long now = millis();

    if (_statusUntil != 0 && now >= _statusUntil) {
        _statusUntil = 0;
        _statusMessage = "";
        _needsRedraw = true;
    }

    // Refresh the Wi-Fi row and the network screen while a connection settles.
    if (now - _lastNetworkPoll >= 2000) {
        _lastNetworkPoll = now;
        if (_screen == SCREEN_NETWORK) {
            _needsRedraw = true;
        }
    }
}

// --- Drawing ----------------------------------------------------------------
void AppSettings::drawHeader(const char* title) {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();

    font.drawText(display, title, 20, 45, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
    display.drawLine(20, 62, display.width() - 20, 62, GxEPD_BLACK);

    if (_dirty) {
        font.drawText(display, "* unsaved changes", 20, 90,
                      FONT_SIZE_SMALL, GxEPD_BLACK);
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
    int w = display.width();

    drawHeader("Settings");

    for (int i = 0; i < ROW_COUNT; i++) {
        int y = LIST_START_Y + i * ROW_HEIGHT;

        if (i == _selectedIndex) {
            display.fillRect(12, y - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            display.setTextColor(GxEPD_WHITE);
        } else {
            display.setTextColor(GxEPD_BLACK);
        }

        uint16_t color = (i == _selectedIndex) ? GxEPD_WHITE : GxEPD_BLACK;

        String label = String(ROW_LABELS[i]);
        if (rowChanged(i)) label = "*" + label;
        font.drawText(display, label.c_str(), 26, y, FONT_SIZE_BODY, color);

        String value = valueForRow(i);
        if (value.length() > 0) {
            int vw = font.getTextWidth(value.c_str(), FONT_SIZE_BODY);
            font.drawText(display, value.c_str(), w - 26 - vw, y, FONT_SIZE_BODY, color);
        }
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("KEY3: next / hold: select  |  KEY1: prev / hold: exit");
}

void AppSettings::drawFontScreen() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
    int w = display.width();

    drawHeader("Font family");

    for (int i = 0; i < 6; i++) {
        int y = LIST_START_Y + i * ROW_HEIGHT;
        uint16_t color = GxEPD_BLACK;

        if (i == _subSelectedIndex) {
            display.fillRect(12, y - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            color = GxEPD_WHITE;
        }

        String label = String(FONT_FAMILY_NAMES[i]);
        if (i == _reader.fontFamily) label += "  (current)";
        font.drawText(display, label.c_str(), 26, y, FONT_SIZE_BODY, color);
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("KEY3 hold: select  |  KEY1 hold: back");
}

void AppSettings::drawNetworkScreen() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();

    drawHeader("Network");

    bool connected = WiFi.status() == WL_CONNECTED;
    int y = LIST_START_Y;

    font.drawText(display, "Status:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    font.drawText(display, connected ? "Connected" : (isWifiOn() ? "Connecting / no link" : "Disconnected"),
                  200, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    font.drawText(display, "SSID:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    String ssid = connected ? WiFi.SSID() : String("-");
    font.drawText(display, FontMgr::utf8ToLatin1(ssid).c_str(), 200, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    font.drawText(display, "IP:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    font.drawText(display, connected ? WiFi.localIP().toString().c_str() : "-",
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

    drawFooter("KEY1 hold: back");
}

void AppSettings::drawSystemScreen() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
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
    size_t usedKb = EbookFS.usedBytes() / 1024;
    size_t totalKb = EbookFS.totalBytes() / 1024;
    String fsStr = String((unsigned long)usedKb) + " / " + String((unsigned long)totalKb) + " KB";
    font.drawText(display, fsStr.c_str(), 220, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT + 20;

    const char* actions[3] = {"Check for updates", "Restart", "Forget network"};
    for (int i = 0; i < 3; i++) {
        int ay = y + i * ROW_HEIGHT;
        uint16_t color = GxEPD_BLACK;
        if (i == _subSelectedIndex) {
            display.fillRect(12, ay - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            color = GxEPD_WHITE;
        }
        font.drawText(display, actions[i], 26, ay, FONT_SIZE_BODY, color);
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("KEY3 hold: execute  |  KEY1 hold: back");
}

void AppSettings::drawConfirmScreen() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
    int w = display.width();

    drawHeader("Unsaved changes");

    font.drawTextCentered(display, "You have changes that you haven't saved.",
                          LIST_START_Y, FONT_SIZE_BODY, GxEPD_BLACK);

    const char* options[3] = {"Save and exit", "Discard and exit", "Cancel"};
    int y = LIST_START_Y + 70;
    for (int i = 0; i < 3; i++) {
        int oy = y + i * ROW_HEIGHT;
        uint16_t color = GxEPD_BLACK;
        if (i == _subSelectedIndex) {
            display.fillRect(12, oy - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            color = GxEPD_WHITE;
        }
        font.drawText(display, options[i], 26, oy, FONT_SIZE_BODY, color);
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("KEY3: next  |  KEY3 hold: confirm");
}

void AppSettings::drawConfirmForgetWifiScreen() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
    int w = display.width();

    drawHeader("Forget network?");

    font.drawTextCentered(display, "The device will disconnect from the current network", LIST_START_Y, FONT_SIZE_BODY,
                          GxEPD_BLACK);
    font.drawTextCentered(display, "and restart in setup mode.", LIST_START_Y + 30, FONT_SIZE_BODY,
                          GxEPD_BLACK);

    const char* options[2] = {"Forget and restart", "Cancel"};
    int y = LIST_START_Y + 100;
    for (int i = 0; i < 2; i++) {
        int oy = y + i * ROW_HEIGHT;
        uint16_t color = GxEPD_BLACK;
        if (i == _subSelectedIndex) {
            display.fillRect(12, oy - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            color = GxEPD_WHITE;
        }
        font.drawText(display, options[i], 26, oy, FONT_SIZE_BODY, color);
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("KEY3: next  |  KEY3 hold: confirm");
}

void AppSettings::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();

    // Moving the highlighted row on SCREEN_MAIN/SCREEN_FONT only touches that
    // row and the one it left, so it doesn't need the full-window e-ink flash
    // that every other redraw here still gets (screen switch, value edit,
    // status message - all of which touch the header/footer or a different
    // layout entirely).
    if (_selectionOnlyRedraw && (_screen == SCREEN_MAIN || _screen == SCREEN_FONT)) {
        int prevIndex = (_screen == SCREEN_MAIN) ? _previousSelectedIndex : _previousSubSelectedIndex;
        int currIndex = (_screen == SCREEN_MAIN) ? _selectedIndex : _subSelectedIndex;
        int screenW = display.width();
        SettingsDirtyRect dirty =
            unionRect(settingsRowRect(prevIndex, screenW), settingsRowRect(currIndex, screenW));
        display.setPartialWindow(dirty.x, dirty.y, dirty.w, dirty.h);
    } else {
        display.setFullWindow();
    }
    _selectionOnlyRedraw = false;

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        switch (_screen) {
            case SCREEN_FONT:
                drawFontScreen();
                break;
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
            case SCREEN_MAIN:
            default:
                drawMainScreen();
                break;
        }
    } while (display.nextPage());
}