#pragma once
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include "Config.h"

// Define the display class here to be used across the app
// Using 800x480 BW (Waveshare 7.5 V2)
typedef GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT> KomaBonDisplay;

class DisplayMgr {
  public:
    static DisplayMgr& getInstance();

    void init();
    void update(); // Handles partial updates if needed

    KomaBonDisplay& getDisplay() {
        return display;
    }

    void clear();
    void fullRefresh();
    void showBootScreen(uint8_t progress, const char* status);

    // Display orientation (0, 1, 2, 3) for full 360-degree support.
    // Note: for this specific 7.5" panel, 0/2 are landscape and 1/3 are portrait.
    void setRotation(int rotation);
    int getRotation() const {
        return _rotation;
    }
    void loadDisplaySettings(); // Reads /display_config.json (call after FS mount)

  private:
    DisplayMgr();
    KomaBonDisplay display;
    bool _bootScreenActive = false;
    int _rotation = 3; // Default: portrait, button on the left
};
