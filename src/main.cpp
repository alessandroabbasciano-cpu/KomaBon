#include <Arduino.h>
#include <esp_ota_ops.h>
#include <driver/rtc_io.h>
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
#include "../KomaBon_Apps/AppBookshelf/AppBookshelf.h"
#include "../KomaBon_Apps/AppSettings.h"
#include "../KomaBon_Apps/AppWebTransfer.h"
#include "AppStorageTools.h"
#include "CrashHandler.h"
#include "SafeBoot.h"

volatile bool gNetworkStartupInProgress = false;
static unsigned long gBootTimestamp = 0;
static bool gOtaConfirmedValid = false;

void setup() {
    esp_ota_mark_app_valid_cancel_rollback(); // Must be early: USB-CDC reset handshake requires valid OTA
                                              // state
    Serial.begin(115200);
    // NOTE: Serial.setTxTimeoutMs(0) is applied in loop() on first iteration.
    // Setting it here breaks the USB-CDC auto-reset handshake needed for upload.
    delay(250); // Allow USB CDC to fully enumerate with host before proceeding

    bool isDeepSleepWakeup = (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED);

    if (isDeepSleepWakeup) {
        // Deep sleep wakeup verification:
        // Clear RTC pullups latched by ext1 sleep configuration
        rtc_gpio_pullup_dis((gpio_num_t)JOY_ADC_PIN);
        rtc_gpio_pulldown_dis((gpio_num_t)JOY_ADC_PIN);
        rtc_gpio_deinit((gpio_num_t)JOY_ADC_PIN);

        rtc_gpio_pullup_dis((gpio_num_t)PIN_BUTTON_SLEEP);
        rtc_gpio_pulldown_dis((gpio_num_t)PIN_BUTTON_SLEEP);
        rtc_gpio_deinit((gpio_num_t)PIN_BUTTON_SLEEP);

        rtc_gpio_pullup_dis((gpio_num_t)PIN_BUTTON_BACK);
        rtc_gpio_pulldown_dis((gpio_num_t)PIN_BUTTON_BACK);
        rtc_gpio_deinit((gpio_num_t)PIN_BUTTON_BACK);

        pinMode(JOY_ADC_PIN, ANALOG);
        analogSetPinAttenuation(JOY_ADC_PIN, ADC_11db);
        pinMode(PIN_BUTTON_SLEEP, INPUT_PULLUP);
        pinMode(PIN_BUTTON_BACK, INPUT_PULLUP);

        uint64_t ext1_mask = esp_sleep_get_ext1_wakeup_status();
        bool confirmedWake = true;
        const int wakeCheckSamples = 18; // 18 samples * 50ms = 900ms

        for (int i = 0; i < wakeCheckSamples; i++) {
            bool isHeld = false;

            if ((ext1_mask & (1ULL << PIN_BUTTON_BACK)) && digitalRead(PIN_BUTTON_BACK) == LOW) {
                isHeld = true;
            } else if ((ext1_mask & (1ULL << JOY_ADC_PIN)) && analogRead(JOY_ADC_PIN) < 2000) {
                // Lenient threshold (< 2000 instead of 600) because resistive ladders
                // can fluctuate when pressed, causing false-abort spikes.
                isHeld = true;
            }

            // Fallback if ext1_mask is 0
            if (ext1_mask == 0) {
                if (digitalRead(PIN_BUTTON_BACK) == LOW || analogRead(JOY_ADC_PIN) < 2000) {
                    isHeld = true;
                }
            }

            if (!isHeld) {
                confirmedWake = false;
                break;
            }
            delay(50);
        }

        if (!confirmedWake) {
            Serial.println("[BOOT] Aborting deep sleep wake: Button was not held for 900ms.");
            BatteryMgr::getInstance().reenterDeepSleep();
        }

        Serial.println("[BOOT] Confirmed prolonged button wake-up. Resuming system...");
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
    appMgr.registerApp(new AppBookshelf());

    static AppStorageTools appStorageTools;
    appMgr.registerApp(&appStorageTools);

    AppWebTransfer* webTransferApp = new AppWebTransfer();
    appMgr.registerApp(webTransferApp);

    AppSettings* settingsApp = new AppSettings();
    appMgr.registerApp(settingsApp);

    if (!isDeepSleepWakeup) {
        // EMERGENCY CALIBRATION RESET
        // If the user got locked out with a bad calibration (e.g. all buttons mapped to DOWN),
        // they can hold KEY2 (PIN_BUTTON_SLEEP) while resetting the device to wipe the calibration.
        if (digitalRead(PIN_BUTTON_SLEEP) == LOW) {
            uint32_t holdStart = millis();
            bool confirmedHold = true;
            while (millis() - holdStart < 1500) {
                if (digitalRead(PIN_BUTTON_SLEEP) == HIGH) {
                    confirmedHold = false;
                    break;
                }
                delay(10);
            }

            if (confirmedHold) {
                Serial.println("[BOOT] EMERGENCY RESET: Wiping joystick calibration...");
                if (SystemFS.exists("/joy_cal.json")) SystemFS.remove("/joy_cal.json");
                if (EbookFS.exists("/joy_cal.json")) EbookFS.remove("/joy_cal.json");

                // Wait for release so the button press doesn't trigger UI events immediately
                while (digitalRead(PIN_BUTTON_SLEEP) == LOW) {
                    delay(10);
                }
            }
        }

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
        appMgr.switchTo("Settings");
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
    // Apply non-blocking Serial on first loop iteration (not setup) to preserve USB-CDC upload handshake
    static bool sSerialConfigured = false;
    if (!sSerialConfigured) {
        Serial.setTxTimeoutMs(0); // Prevents OS freeze if Web Serial host stops reading
        sSerialConfigured = true;
    }

    InputMgr::getInstance().update();
    AppMgr::getInstance().update();

    if (!InputMgr::getInstance().hasPendingActions() &&
        (millis() - InputMgr::getInstance().getLastInputTime() > LAZY_RENDER_DEBOUNCE_MS)) {
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
