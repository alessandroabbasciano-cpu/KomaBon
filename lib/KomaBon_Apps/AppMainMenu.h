#pragma once
#include "BaseApp.h"
#include "../KomaBon_Core/InputMgr.h"
#include "../KomaBon_Core/BatteryMgr.h"
#include "../KomaBon_Core/Lock.h"
#include "../KomaBon_Core/ProgressStore.h"

class AppMainMenu : public App {
  public:
    const char* getName() override {
        return "Main Menu";
    }

    void start() override;
    void update() override;
    void draw() override;
    void stop() override;
    void forceRedraw() override;

    void handleInput(InputAction action);

    void startHotspot();
    void stopHotspot();

  private:
    int selectedIndex = 1;
    bool _needsRedraw = false;
    bool _firstDraw = true;
    bool _selectionOnlyRedraw = false;
    bool _batteryOnlyRedraw = false;
    bool _footerOnlyRedraw = false;
    int _previousSelectedIndex = 1;

    bool _lastWifiConnected = false;
    bool _wifiStarting = false;
    String _lastIp = "";
    String _lastWifiFooterText = "";
    bool _hotspotActive = false;
    unsigned long _lastNetworkPoll = 0;
    unsigned long _lastBatteryPoll = 0;
    BatteryStatus _lastBatteryStatus = {0.0f, -1, false};

    // Widget State
    bool _hasResume = false;
    String _lastBookTitle = "";
    int _lastBookPage = 0;
    void loadResumeData();

    Book32Mutex _updateMutex;
    bool _updateAvailable = false;
    String _updateVersion = "";
    String getWifiFooterText() const;
};