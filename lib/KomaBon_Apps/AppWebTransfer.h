#ifndef APP_WEB_TRANSFER_H
#define APP_WEB_TRANSFER_H

#include "../KomaBon_Core/BaseApp.h"
#include "../KomaBon_Core/InputMgr.h"

enum class WebTransferState { Init, WaitingForInitScreen, StartingRadio, WaitingForReadyScreen, Ready };

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
        return false; // Cruciale: previene ridisegni spontanei dovuti a fluttuazioni di batteria
    }

  private:
    bool _needsRedraw;
    WebTransferState _state;
    unsigned long _stateTimer;

    void handleInput(InputAction action);
    void drawConnecting();
    void drawReady();
};

#endif // APP_WEB_TRANSFER_H