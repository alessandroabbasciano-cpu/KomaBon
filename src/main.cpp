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
#include "../Apps/AppReader/AppReader.h"
#include "../KomaBon_Apps/AppSettings.h"

// System-wide flag for network status UI indicators
volatile bool gNetworkStartupInProgress = false;

// Hook for ESP_LOGx macros to mirror to Web Console ONLY IF active.
// Prevents broadcasting to a dead socket during offline boot.
static int webLogVprintf(const char* fmt, va_list args) {
    char loc_buf[256];
    int len = vsnprintf(loc_buf, sizeof(loc_buf), fmt, args);
    if (len > 0) {
        // Always print to hardware serial
        Serial.print(loc_buf);

        // Broadcast only if the On-Demand Wi-Fi is actively running
        if (WebMgr::getInstance().isInitialized()) {
            String msg = String(loc_buf);
            if (!msg.endsWith("\n")) {
                msg += "\n";
            }
            WebMgr::getInstance().broadcastSerial((const uint8_t*)msg.c_str(), msg.length());
        }
    }
    return len;
}

void setup() {
    // 1. Core System Init
    esp_ota_mark_app_valid_cancel_rollback();

    Serial.begin(115200);
    delay(250);

    esp_log_set_vprintf(webLogVprintf);

    Serial.println("\n\n");
    Serial.println("=======================================");
    Serial.println("        KomaBon OS Starting...         ");
    Serial.printf("  Build: %s %s  \n", __DATE__, __TIME__);
    Serial.println("=======================================");

    // Enforce strict offline mode on boot
    gNetworkStartupInProgress = false;

    // 2. Hardware Subsystems
    DisplayMgr& displayMgr = DisplayMgr::getInstance();
    displayMgr.init();
    displayMgr.showBootScreen(10, "Init Display Subsystem");

    // Initialize the external MicroSD card via high-speed SPI (SdFat)
    displayMgr.showBootScreen(25, "Mounting High-Speed SD");
    SDMgr::getInstance().init();

    // Mount internal LittleFS filesystems (SystemFS, EbookFS fallback)
    displayMgr.showBootScreen(40, "Mounting Internal Storage");
    WebMgr::getInstance().mountFilesystems();

    // 3. UI & Managers
    displayMgr.showBootScreen(55, "Loading Font Assets");
    FontMgr::getInstance().init();
    displayMgr.loadDisplaySettings();

    displayMgr.showBootScreen(70, "Init Power & Controls");
    BatteryMgr::getInstance().init();
    InputMgr::getInstance().init();

    // 4. Application Registry
    displayMgr.showBootScreen(85, "Registering Core Apps");
    AppMgr& appMgr = AppMgr::getInstance();

    appMgr.registerApp(new AppMainMenu());

    AppReader* readerApp = new AppReader();
    appMgr.registerApp(readerApp);

    AppSettings* settingsApp = new AppSettings();
    appMgr.registerApp(settingsApp);

    // 5. Boot Routing Logic
    displayMgr.showBootScreen(100, "System Ready");

    // Check joystick calibration safely on the internal SystemFS partition
    if (!SystemFS.exists("/joy_cal.json")) {
        Serial.println("[BOOT] Missing calibration. Starting wizard.");
        appMgr.switchTo(2); // SettingsApp is index 2
        settingsApp->startCalibrationWizard();
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

    // Ask InputManager if the user is currently interacting with the controls
    if (InputMgr::getInstance().isInteracting()) {
        lastPhysicalInputTime = millis();
    }

    // Wait for 200ms of absolute silence before allowing the e-ink screen to update
    if (millis() - lastPhysicalInputTime > 200) {
        AppMgr::getInstance().draw();
    }

    WebMgr::getInstance().update();
    BatteryMgr::getInstance().update();

    App* currentApp = AppMgr::getInstance().getCurrentApp();
    if (!currentApp || currentApp->allowsSystemStatusIndicator()) {
        // Ensure the battery indicator also respects the lazy rendering rule
        if (millis() - lastPhysicalInputTime > 200) {
            BatteryMgr::getInstance().drawStatusIndicator();
        }
    }

    delay(1); // Yield to FreeRTOS watchdog
}