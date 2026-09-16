#ifndef APP_WEB_TRANSFER_H
#define APP_WEB_TRANSFER_H

#include "../KomaBon_Core/BaseApp.h"
#include "../KomaBon_Core/InputMgr.h"

class AppWebTransfer : public App {
  public:
    AppWebTransfer();
    virtual ~AppWebTransfer() = default;

    void start() override;
    void stop() override;
    void update() override;
    void draw() override;
    void forceRedraw() override;

    const char* getName() override {
        return "Web Transfer";
    }

    const uint8_t* getIconImage() override;

    bool allowsSystemStatusIndicator() override {
        return true;
    }

  private:
    bool _needsRedraw;
    bool _wifiConnecting;
    bool _wifiReady;

    void handleInput(InputAction action);
    void drawConnecting();
    void drawReady();
};

#endif // APP_WEB_TRANSFER_H