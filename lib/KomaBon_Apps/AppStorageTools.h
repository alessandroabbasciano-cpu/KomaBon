#pragma once
#include "BaseApp.h"
#include "../KomaBon_Core/InputMgr.h"

class AppStorageTools : public App {
  public:
    const char* getName() override {
        return "Storage Tools";
    }

    const uint8_t* getIconImage() override;

    void start() override;
    void update() override;
    void draw() override;
    void stop() override;
    void forceRedraw() override;

    void handleInput(InputAction action);

  private:
    int _selectedIndex = 0;
    int _previousSelectedIndex = 0;
    bool _needsRedraw = true;
    bool _selectionOnlyRedraw = false;
    bool _firstDraw = true;

    String _statusMessage = "";

    void executeSelection();
    void remountSD();
    void cleanReboot();
    void resetReadingProgress();
    void purgeThumbnails();

    void drawHeader(const char* title);
    void drawFooter(const char* hint);
};