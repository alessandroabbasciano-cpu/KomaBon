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

// System-wide flag for network status UI indicators
volatile bool gNetworkStartupInProgress = false;

void setup() {
    // 1. Core System Init
    esp_ota_mark_app_valid_cancel_rollback();

    Serial.begin(115200);
    delay(250);

    Serial.println("\n\n");
    Serial.println("=======================================");
    Serial.println("        KomaBon OS Starting...         ");
    Serial.printf("  Build: %s %s  \n", __DATE__, __TIME__);
    Serial.println("=======================================");

    // Enforce strict offline mode on boot
    gNetworkStartupInProgress = false;

    // 2. Base Filesystem
    WebMgr::getInstance().mountFilesystems();

    // 3. UI Assets
    FontMgr::getInstance().init();

    // 4. Display Subsystem
    DisplayMgr& displayMgr = DisplayMgr::getInstance();
    displayMgr.init();
    displayMgr.loadDisplaySettings();
    displayMgr.showBootScreen(10, "System Initialization");

    // 5. External Storage
    displayMgr.showBootScreen(35, "Mounting SD Card");
    SDMgr::getInstance().init();

    // 6. Power & Controls
    displayMgr.showBootScreen(60, "Init Power & Controls");
    BatteryMgr::getInstance().init();
    InputMgr::getInstance().init();

    // 7. Application Registry
    displayMgr.showBootScreen(85, "Registering Core Apps");
    AppMgr& appMgr = AppMgr::getInstance();

    appMgr.registerApp(new AppMainMenu());

    AppReader* readerApp = new AppReader();
    appMgr.registerApp(readerApp);

    AppWebTransfer* webTransferApp = new AppWebTransfer();
    appMgr.registerApp(webTransferApp);

    AppSettings* settingsApp = new AppSettings();
    appMgr.registerApp(settingsApp);

    // 8. Boot Routing Logic
    displayMgr.showBootScreen(100, "System Ready");

    // Check joystick calibration safely on the internal SystemFS partition
    if (!SystemFS.exists("/joy_cal.json")) {
        Serial.println("[BOOT] Missing calibration. Starting wizard.");
        appMgr.switchTo(2); // SettingsApp is index 2
    } else if (readerApp->hasBootResume()) {
        Serial.println("[BOOT] Resuming last opened book.");
        readerApp->resumeSavedBookOnStart();
        appMgr.switchTo(1); // eReader is index 1
    } else {
        Serial.println("[BOOT] Loading Main Menu.");
        appMgr.switchTo(0); // MainMenu is index 0
    }

    Serial.println("[BOOT] Sequence Complete. Entering Lazy Render Loop.");
}

void loop() {
    InputMgr::getInstance().update();
    AppMgr::getInstance().update();

    // --- LAZY RENDERING (DEBOUNCED DRAWING) ---
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