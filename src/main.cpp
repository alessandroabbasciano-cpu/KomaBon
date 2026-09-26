#include <Arduino.h>
#include <esp_ota_ops.h>
#include "Config.h"
#include "NetworkState.h"

#include "DisplayMgr.h"
#include "InputMgr.h"
#include "AppMgr.h"
#include "WebMgr.h"
#include "BatteryMgr.h"
#include "FontMgr.h"
#include "SDMgr.h"
#include "KomaBonFS.h"

#include "../KomaBon_Apps/AppMainMenu.h"
#include "../KomaBon_Apps/AppReader/AppReader.h"
#include "../KomaBon_Apps/AppSettings.h"
#include "../KomaBon_Apps/AppWebTransfer.h"
#include "AppStorageTools.h"

volatile bool gNetworkStartupInProgress = false;

void setup() {
    esp_ota_mark_app_valid_cancel_rollback();

    Serial.begin(115200);
    delay(250);

    Serial.println("\n\n");
    Serial.println("=======================================");
    Serial.println("        KomaBon OS Starting...         ");
    Serial.printf("  Build: %s %s  \n", __DATE__, __TIME__);
    Serial.println("=======================================");

    gNetworkStartupInProgress = false;

    // Execute hardware initialization in electrical silence.
    // The E-ink charge pump is kept strictly OFF to prevent VCC brownouts
    // during the critical MicroSD SPI negotiation.
    Serial.println("[BOOT] Initializing MicroSD Hardware...");
    SDMgr::getInstance().init();

    Serial.println("[BOOT] Mounting Virtual File Systems...");
    WebMgr::getInstance().mountFilesystems();
    FontMgr::getInstance().init();

    DisplayMgr& displayMgr = DisplayMgr::getInstance();
    displayMgr.init();
    displayMgr.loadDisplaySettings();

    BatteryMgr::getInstance().init();
    InputMgr::getInstance().init();

    AppMgr& appMgr = AppMgr::getInstance();

    appMgr.registerApp(new AppMainMenu());

    AppReader* readerApp = new AppReader();
    appMgr.registerApp(readerApp);

    static AppStorageTools appStorageTools;
    appMgr.registerApp(&appStorageTools);

    AppWebTransfer* webTransferApp = new AppWebTransfer();
    appMgr.registerApp(webTransferApp);

    AppSettings* settingsApp = new AppSettings();
    appMgr.registerApp(settingsApp);

    // Compile post-boot diagnostic report
    char bootReport[64];
    snprintf(bootReport, sizeof(bootReport), "SD: %s | Joy: %s | Bat: %d%%",
             SDMgr::getInstance().isMounted() ? "Mounted" : "Failed",
             (SystemFS.exists("/joy_cal.json") || EbookFS.exists("/joy_cal.json")) ? "Calibrated" : "Default",
             BatteryMgr::getInstance().getStatus().percentage);

    // Single full-screen update only after all buses are safely stabilized
    displayMgr.showBootScreen(100, bootReport);
    delay(2000); // Give user time to read boot report

    if (!SystemFS.exists("/joy_cal.json") && !EbookFS.exists("/joy_cal.json")) {
        Serial.println("[BOOT] Missing calibration. Starting wizard.");
        appMgr.switchTo(3);
        settingsApp->startCalibrationWizard();
    } else if (readerApp->hasBootResume()) {
        Serial.println("[BOOT] Resuming last opened book.");
        readerApp->resumeSavedBookOnStart();
        appMgr.switchTo(1);
    } else {
        Serial.println("[BOOT] Loading Main Menu.");
        appMgr.switchTo(0);
    }

    Serial.println("[BOOT] Sequence Complete. Entering Lazy Render Loop.");
}

void loop() {
    InputMgr::getInstance().update();
    AppMgr::getInstance().update();

    static unsigned long lastPhysicalInputTime = 0;

    if (InputMgr::getInstance().isInteracting()) {
        lastPhysicalInputTime = millis();
    }

    if (!InputMgr::getInstance().hasPendingActions() &&
        (millis() - lastPhysicalInputTime > LAZY_RENDER_DEBOUNCE_MS)) {
        AppMgr::getInstance().draw();
    }

    WebMgr::getInstance().update();
    BatteryMgr::getInstance().update();

    delay(1);
}