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

volatile bool gNetworkStartupInProgress = false;

void setup() {
    // IMMEDIATE HARDWARE SAFETY: Force SD Card to High-Z and stabilize floating MISO
    pinMode(39, OUTPUT);
    digitalWrite(39, HIGH);
    pinMode(8, INPUT_PULLUP);

    esp_ota_mark_app_valid_cancel_rollback();

    Serial.begin(115200);
    delay(250);

    Serial.println("\n\n");
    Serial.println("=======================================");
    Serial.println("        KomaBon OS Starting...         ");
    Serial.printf("  Build: %s %s  \n", __DATE__, __TIME__);
    Serial.println("=======================================");

    gNetworkStartupInProgress = false;

    Serial.println("[BOOT] Initializing MicroSD Hardware...");
    SDMgr::getInstance().init();

    Serial.println("[BOOT] Mounting Virtual File Systems...");
    WebMgr::getInstance().mountFilesystems();
    FontMgr::getInstance().init();

    DisplayMgr& displayMgr = DisplayMgr::getInstance();
    displayMgr.init();
    displayMgr.loadDisplaySettings();

    displayMgr.showBootScreen(10, "Storage & System Init Complete");

    displayMgr.showBootScreen(40, "Init Power & Controls");
    BatteryMgr::getInstance().init();
    InputMgr::getInstance().init();

    displayMgr.showBootScreen(70, "Registering Core Apps");
    AppMgr& appMgr = AppMgr::getInstance();

    appMgr.registerApp(new AppMainMenu());

    AppReader* readerApp = new AppReader();
    appMgr.registerApp(readerApp);

    AppWebTransfer* webTransferApp = new AppWebTransfer();
    appMgr.registerApp(webTransferApp);

    AppSettings* settingsApp = new AppSettings();
    appMgr.registerApp(settingsApp);

    displayMgr.showBootScreen(100, "System Ready");

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

    if (millis() - lastPhysicalInputTime > 200) {
        AppMgr::getInstance().draw();
    }

    WebMgr::getInstance().update();
    BatteryMgr::getInstance().update();

    delay(1);
}