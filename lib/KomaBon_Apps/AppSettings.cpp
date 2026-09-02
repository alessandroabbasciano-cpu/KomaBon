#include "AppSettings.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "icon_settings.h"
#include "../KomaBon_Core/BatteryMgr.h"
#include "../KomaBon_Core/JoystickMgr.h"
#include "../Book32_Web/WebMgr.h"
#include "../Book32_Update/GitHubMgr.h"
#include "../../include/Config.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <KomaBonFS.h>

// Logic arrays
static const int FONT_SIZES[] = {9, 12, 18};
static const int REFRESH_FREQS[] = {5, 10, 20, 50};
static const int SLEEP_TIMEOUTS[] = {0, 5, 15, 30, 60};

static int cycleIntForward(const int* values, int count, int current) {
    for (int i = 0; i < count; i++) {
        if (values[i] == current) return values[(i + 1) % count];
    }
    return values[0];
}

static int cycleIntBackward(const int* values, int count, int current) {
    for (int i = 0; i < count; i++) {
        if (values[i] == current) return values[(i - 1 + count) % count];
    }
    return values[0];
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

    InputMgr::getInstance().setCallback(std::bind(&AppSettings::handleInput, this, std::placeholders::_1));
}

void AppSettings::startCalibrationWizard() {
    start();
    _screen = SCREEN_JOYCAL;
    _joyCalStep = 0;
    _needsRedraw = true;
}

void AppSettings::stop() {
    saveDraftIfDirty();
}

void AppSettings::forceRedraw() {
    _selectionOnlyRedraw = false;
    _needsRedraw = true;
}

void AppSettings::recomputeDirty() {
    _dirty = _reader.fontSize != _readerSaved.fontSize || _reader.fontFamily != _readerSaved.fontFamily ||
             _reader.refreshFrequency != _readerSaved.refreshFrequency ||
             _display.rotation != _displaySaved.rotation ||
             _sleep.timeout != SettingsStore::getInstance().loadSleep().timeout;
}

bool AppSettings::rowChanged(int index) const {
    switch (index) {
        case ROW_FONT_SIZE:
            return _reader.fontSize != _readerSaved.fontSize;
        case ROW_FONT_FAMILY:
            return _reader.fontFamily != _readerSaved.fontFamily;
        case ROW_ROTATION:
            return _display.rotation != _displaySaved.rotation;
        case ROW_REFRESH:
            return _reader.refreshFrequency != _readerSaved.refreshFrequency;
        default:
            return false;
    }
}

void AppSettings::setStatus(const String& msg, unsigned long durationMs) {
    _statusMessage = msg;
    _statusUntil = millis() + durationMs;
    _needsRedraw = true;
}

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
        WiFi.begin();
        setStatus("Connecting Wi-Fi...");
    }
}

void AppSettings::forgetNetwork() {
    saveDraftIfDirty();
    setStatus("Forgetting network...", 1000);
    draw();
    WiFiManager wm;
    wm.resetSettings();
    delay(400);
    ESP.restart();
}

void AppSettings::cycleValue(int index, bool forward) {
    switch (index) {
        case ROW_FONT_SIZE:
            _reader.fontSize = forward ? cycleIntForward(FONT_SIZES, 3, _reader.fontSize)
                                       : cycleIntBackward(FONT_SIZES, 3, _reader.fontSize);
            break;
        case ROW_FONT_FAMILY:
            if (forward) {
                _reader.fontFamily = (_reader.fontFamily + 1) % 6;
            } else {
                _reader.fontFamily = (_reader.fontFamily + 5) % 6;
            }
            break;
        case ROW_ROTATION:
            _display.rotation = (_display.rotation == 3) ? 1 : 3;
            break;
        case ROW_REFRESH:
            _reader.refreshFrequency = forward ? cycleIntForward(REFRESH_FREQS, 4, _reader.refreshFrequency)
                                               : cycleIntBackward(REFRESH_FREQS, 4, _reader.refreshFrequency);
            break;
        case ROW_SLEEP:
            _sleep.timeout = forward ? cycleIntForward(SLEEP_TIMEOUTS, 5, _sleep.timeout)
                                     : cycleIntBackward(SLEEP_TIMEOUTS, 5, _sleep.timeout);
            break;
        case ROW_WIFI:
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
        case ROW_NETWORK:
            _screen = SCREEN_NETWORK;
            break;
        case ROW_SYSTEM:
            _screen = SCREEN_SYSTEM;
            _subSelectedIndex = 0;
            break;
        case ROW_JOYSTICK:
            _screen = SCREEN_JOYCAL;
            _joyCalStep = 0;
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
            cycleValue(index, true); // Fallback to forward cycle
            return;
    }
    _needsRedraw = true;
}

void AppSettings::handleInput(InputAction action) {
    if (_screen == SCREEN_JOYCAL) {
        if (_joyCalStep >= 5) {
            if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU || action == INPUT_SELECT ||
                action == INPUT_NEXT) {
                _screen = SCREEN_MAIN;
                _needsRedraw = true;
            }
        }
        return;
    }

    if (_screen == SCREEN_CONFIRM) {
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

    if (_screen == SCREEN_NETWORK) {
        if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU || action == INPUT_SELECT) {
            _screen = SCREEN_MAIN;
            _needsRedraw = true;
        }
        return;
    }

    if (_screen == SCREEN_SYSTEM) {
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
                    draw();
                    UpdateInfo info = GitHubMgr::getInstance().checkUpdate(SYSTEM_VERSION);
                    if (info.available) {
                        saveDraftIfDirty();
                        Serial.println("AppSettings: Launching OTA task...");
                        xTaskCreatePinnedToCore(
                            [](void* param) {
                                GitHubMgr::getInstance().triggerUpdate(SYSTEM_VERSION);
                                vTaskDelete(NULL);
                            },
                            "OTA_Settings_Task", 16384, nullptr, 1, nullptr, 1);
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
                _screen = SCREEN_CONFIRM_FORGET_WIFI;
                _subSelectedIndex = 1;
                _needsRedraw = true;
            }
        } else if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU) {
            _screen = SCREEN_MAIN;
            _needsRedraw = true;
        }
        return;
    }

    if (_screen == SCREEN_CONFIRM_FORGET_WIFI) {
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

    if (action == INPUT_NEXT) {
        _selectionOnlyRedraw = true;
        _selectedIndex = (_selectedIndex + 1) % ROW_COUNT;
        _needsRedraw = true;
    } else if (action == INPUT_PREV) {
        _selectionOnlyRedraw = true;
        _selectedIndex = (_selectedIndex + ROW_COUNT - 1) % ROW_COUNT;
        _needsRedraw = true;
    } else if (action == INPUT_RIGHT) {
        _selectionOnlyRedraw = true;
        cycleValue(_selectedIndex, true);
    } else if (action == INPUT_LEFT) {
        _selectionOnlyRedraw = true;
        cycleValue(_selectedIndex, false);
    } else if (action == INPUT_SELECT) {
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

bool AppSettings::applyAndSave() {
    SettingsStore& store = SettingsStore::getInstance();

    bool ok;
    {
        SettingsStore::Transaction tx;
        ok = store.saveReader(_reader);
        ok = store.saveDisplay(_display) && ok;
        ok = store.saveSleep(_sleep) && ok;
    }

    if (!ok) {
        setStatus("Save error. Changes kept.", 4000);
        return false;
    }

    if (_display.rotation != _displaySaved.rotation) {
        DisplayMgr::getInstance().setRotation(_display.rotation);
        for (App* app : AppMgr::getInstance().getApps()) {
            if (app) app->forceRedraw();
        }
    }

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

void AppSettings::update() {
    unsigned long now = millis();

    if (_statusUntil != 0 && now >= _statusUntil) {
        _statusUntil = 0;
        _statusMessage = "";
        _needsRedraw = true;
    }

    if (now - _lastNetworkPoll >= 2000) {
        _lastNetworkPoll = now;
        if (_screen == SCREEN_NETWORK) {
            _needsRedraw = true;
        }
    }

    if (_screen == SCREEN_JOYCAL && _joyCalStep < 5) {

        // HARDWARE EMERGENCY ABORT
        pinMode(PIN_BUTTON_BACK, INPUT_PULLUP);
        if (digitalRead(PIN_BUTTON_BACK) == LOW) {
            Serial.println("AppSettings: Calibration aborted via physical button.");

            // NEW: Protect existing calibration.
            // Check if the file exists before writing defaults to break the boot loop.
            // If the user already has a custom calibration, we do not overwrite it.
            if (!EbookFS.exists("/joy_cal.json")) {
                Serial.println("AppSettings: No calibration found. Saving defaults.");
                JoystickMgr::getInstance().saveCalibration(0, 3350, 1250, 2650, 1950);
            }

            // Wait for button release BEFORE changing the screen state.
            while (digitalRead(PIN_BUTTON_BACK) == LOW) {
                delay(10);
            }

            _screen = SCREEN_MAIN;
            _needsRedraw = true;
            return;
        }

        int raw = JoystickMgr::getInstance().readAnalogAveraged();

        if (_joyCalWaitingRelease) {
            if (raw > 3800) _joyCalWaitingRelease = false;
        } else {
            if (raw < 3800) {
                if (abs(raw - _joyCalLastRaw) < 150) {
                    if (millis() - _joyCalHoldStart > 1200) {
                        _joyCalValues[_joyCalStep] = raw;
                        _joyCalStep++;
                        _joyCalWaitingRelease = true;
                        _needsRedraw = true;

                        if (_joyCalStep == 5) {
                            JoystickMgr::getInstance().saveCalibration(_joyCalValues[0], _joyCalValues[1],
                                                                       _joyCalValues[2], _joyCalValues[3],
                                                                       _joyCalValues[4]);
                            // NEW: Set a 1-second safety cooldown for the final screen
                            _statusUntil = millis() + 1000;
                        }
                        // NEW: Force an immediate physical screen update.
                        // This bypasses the Lazy Rendering block in main.cpp,
                        // providing instant visual feedback while the user is
                        // still physically holding the joystick direction.
                        draw();
                    }
                } else {
                    _joyCalLastRaw = raw;
                    _joyCalHoldStart = millis();
                }
            } else {
                _joyCalHoldStart = millis();
                _joyCalLastRaw = 4095;
            }
        }
    }
}