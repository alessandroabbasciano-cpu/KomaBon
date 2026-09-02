#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "BaseApp.h"
#include "../KomaBon_Core/InputMgr.h"
#include "../KomaBon_Core/SettingsStore.h"

enum SettingsScreen {
    SCREEN_MAIN,
    SCREEN_NETWORK,
    SCREEN_SYSTEM,
    SCREEN_CONFIRM,
    SCREEN_CONFIRM_FORGET_WIFI,
    SCREEN_JOYCAL
};

class AppSettings : public App {
  public:
    AppSettings();

    void start() override;
    void startCalibrationWizard();
    void update() override;
    void draw() override;
    void stop() override;
    void forceRedraw() override;

    const char* getName() override {
        return "Settings";
    }
    const uint8_t* getIconImage() override;

    void handleInput(InputAction action);
    void saveDraftIfDirty();

  private:
    // Moved here so both CPP files can share the layout definitions
    enum SettingsRow {
        ROW_FONT_SIZE = 0,
        ROW_FONT_FAMILY,
        ROW_ROTATION,
        ROW_REFRESH,
        ROW_SLEEP,
        ROW_WIFI,
        ROW_NETWORK,
        ROW_SYSTEM,
        ROW_JOYSTICK,
        ROW_SAVE,
        ROW_DISCARD,
        ROW_COUNT
    };

    SettingsScreen _screen;
    int _selectedIndex;
    int _subSelectedIndex;
    bool _needsRedraw;
    bool _dirty;
    bool _selectionOnlyRedraw;
    int _previousSelectedIndex;
    int _previousSubSelectedIndex;
    int _joyCalStep = 0;
    unsigned long _joyCalHoldStart = 0;
    int _joyCalLastRaw = 4095;
    bool _joyCalWaitingRelease = false;
    int _joyCalValues[5];

    ReaderSettings _reader;
    DisplaySettings _display;
    SleepSettings _sleep;
    ReaderSettings _readerSaved;
    DisplaySettings _displaySaved;

    String _statusMessage;
    unsigned long _statusUntil;
    unsigned long _lastNetworkPoll;

    void cycleValue(int index, bool forward);
    void activate(int index);
    bool applyAndSave();
    void discardChanges();
    void recomputeDirty();
    void setStatus(const String& msg, unsigned long durationMs = 2500);

    void toggleWifi();
    bool isWifiOn() const;
    void forgetNetwork();

    // UI Rendering declarations (Implemented in AppSettings_UI.cpp)
    void drawMainScreen();
    void drawFontScreen();
    void drawNetworkScreen();
    void drawSystemScreen();
    void drawConfirmScreen();
    void drawConfirmForgetWifiScreen();
    void drawHeader(const char* title);
    void drawFooter(const char* hint);
    void drawJoyCalScreen();

    String valueForRow(int index) const;
    bool rowChanged(int index) const;
};

#endif