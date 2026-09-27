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
#include "CrashHandler.h"
#include "SafeBoot.h"

volatile bool gNetworkStartupInProgress = false;
static unsigned long gBootTimestamp = 0;
static bool gOtaConfirmedValid = false;

void setup() {
    Serial.begin(115200);
    delay(50);

    bool isDeepSleepWakeup = (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED);

    if (isDeepSleepWakeup) {
        // Deep sleep wakeup verification:
        // Only confirm wakeup if JOY_CENTER is held continuously for 900ms.
        // Accidental bumps, quick touches, or tilts (UP/DOWN/LEFT/RIGHT) immediately
        // return to deep sleep without touching display or storage.
        pinMode(JOY_ADC_PIN, INPUT);
        analogSetAttenuation(ADC_11db);

        bool confirmedWake = true;
        const int wakeCheckSamples = 18; // 18 samples * 50ms = 900ms
        for (int i = 0; i < wakeCheckSamples; i++) {
            int joyVal = analogRead(JOY_ADC_PIN);
            // JOY_CENTER connects GPIO 2 directly to GND (< 600).
            // Tilts produce >= 1200, open switch > 3800.
            if (joyVal >= 600) {
                confirmedWake = false;
                break;
            }
            delay(50);
        }

        if (!confirmedWake) {
            Serial.println("[BOOT] Aborting deep sleep wake: JOY_CENTER was not held for 900ms.");
            BatteryMgr::getInstance().reenterDeepSleep();
        }

        Serial.println("[BOOT] Confirmed prolonged JOY_CENTER wake-up. Resuming system...");
        // Inform InputMgr to suppress the release of this initial wake hold,
        // so releasing the joystick after the screen updates doesn't trigger a ghost click.
        InputMgr::getInstance().suppressWakeRelease();
    } else {
        // Emergency Hardware Safe-Boot check:
        // If the back button or joystick center is held at power-on, enter rescue mode immediately.
        if (SafeBoot::checkRequested()) {
            SafeBoot::run();
        }
    }

    gBootTimestamp = millis();

    // Initialize post-mortem crash handler and record boot reason
    CrashHandler::getInstance().init();

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

    if (!isDeepSleepWakeup) {
        // Compile post-boot diagnostic report
        char bootReport[64];
        snprintf(bootReport, sizeof(bootReport), "SD: %s | Joy: %s | Bat: %d%%",
                 SDMgr::getInstance().isMounted() ? "Mounted" : "Failed",
                 (SystemFS.exists("/joy_cal.json") || EbookFS.exists("/joy_cal.json")) ? "Calibrated"
                                                                                       : "Default",
                 BatteryMgr::getInstance().getStatus().percentage);

        // Single full-screen update only after all buses are safely stabilized
        displayMgr.showBootScreen(100, bootReport);
        delay(2000); // Give user time to read boot report
    } else {
        Serial.println("[BOOT] Woke up from Deep Sleep via GPIO interrupt. Fast resume...");
    }

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

    // Dual-OTA Rollback Protection:
    // Only confirm the new OTA partition as permanently valid after 30 seconds
    // of continuous operation or upon the first physical user interaction.
    if (!gOtaConfirmedValid) {
        if ((millis() - gBootTimestamp >= 30000) || InputMgr::getInstance().isInteracting()) {
            gOtaConfirmedValid = true;
            esp_ota_mark_app_valid_cancel_rollback();
            Serial.println("[OTA] Firmware runtime validated and rollback cancelled.");
        }
    }

    WebMgr::getInstance().update();
    BatteryMgr::getInstance().update();

    delay(1);
}