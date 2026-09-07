#include <Arduino.h>
#include <WiFi.h>
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
#include <WiFiManager.h>
#include <stdarg.h>

// Global custom vprintf hook to mirror all system logs to WebSocket with clean line breaks
static int webLogVprintf(const char* fmt, va_list args) {
    char loc_buf[256];
    int len = vsnprintf(loc_buf, sizeof(loc_buf), fmt, args);
    if (len > 0) {
        String msg = String(loc_buf);
        Serial.print(msg);

        // Ensure every log chunk ends with a newline for proper console formatting
        if (!msg.endsWith("\n")) {
            msg += "\n";
        }
        WebMgr::getInstance().broadcastSerial((const uint8_t*)msg.c_str(), msg.length());
    }
    return len;
}

// Bridge class that duplicates all print calls to Hardware Serial and Web Console
class SerialWebBridge : public Print {
  public:
    size_t write(uint8_t c) override {
        Serial.write(c);
        WebMgr::getInstance().broadcastSerial(&c, 1);
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        Serial.write(buffer, size);
        WebMgr::getInstance().broadcastSerial(buffer, size);
        return size;
    }
};

static SerialWebBridge LogBridge;

volatile bool gNetworkStartupInProgress = false;
static WiFiManager* gWifiManager = nullptr;

static void networkStartupTask(void* parameter) {
    (void)parameter;

    WebMgr::getInstance().sendLog("Network startup task started");
    if (!gWifiManager) {
        gWifiManager = new WiFiManager();
    }

    // Portal timeout of 120 seconds prevents blocking offline usage
    gWifiManager->setConfigPortalTimeout(120);
    bool connected = gWifiManager->autoConnect("KomaBon-Setup");

    if (!connected) {
        WebMgr::getInstance().sendLog("WiFi setup did not connect; continuing offline");
        gNetworkStartupInProgress = false;
        vTaskDelete(nullptr);
        return;
    }

    WebMgr::getInstance().sendLog("WiFi connected");
    WebMgr::getInstance().sendLog(WiFi.localIP().toString());

    App* currentApp = AppMgr::getInstance().getCurrentApp();
    if (currentApp && strcmp(currentApp->getName(), "eReader") == 0) {
        WebMgr::getInstance().sendLog("Network startup skipped services; eReader is active");
        WebMgr::getInstance().stop();
        WiFi.disconnect(false);
        WiFi.mode(WIFI_OFF);
        gNetworkStartupInProgress = false;
        vTaskDelete(nullptr);
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(250));

    WebMgr::getInstance().init();
    LogBridge.println("\n[SYS] Web Console Stream Connected.");

    WebMgr::getInstance().sendLog("Network services ready");
    gNetworkStartupInProgress = false;
    vTaskDelete(nullptr);
}

void setup() {
    esp_ota_mark_app_valid_cancel_rollback();

    Serial.begin(115200);
    delay(250);

    // Register system-wide log interceptor for the Web Console
    esp_log_set_vprintf(webLogVprintf);

    // Initialize display subsystem and show initial boot screen
    DisplayMgr& displayMgr = DisplayMgr::getInstance();
    displayMgr.init();
    displayMgr.showBootScreen(8, "Display ready");

    WebMgr::getInstance().sendLog("\n\n");
    WebMgr::getInstance().sendLog("╔═══════════════════════════════════════╗");
    WebMgr::getInstance().sendLog("║        KomaBon OS Starting...         ║");
    WebMgr::getInstance().sendLogf("║  Build: %s %s  ║\n", __DATE__, __TIME__);
    WebMgr::getInstance().sendLog("╚═══════════════════════════════════════╝");

    InputMgr& inputMgr = InputMgr::getInstance();
    AppMgr& appMgr = AppMgr::getInstance();
    WebMgr& webMgr = WebMgr::getInstance();

    // Initialize the external MicroSD card first to claim VFS mount point
    SDMgr::getInstance().init();

    // Mount internal filesystems; falls back safely if SD card is missing
    displayMgr.showBootScreen(28, "Mounting storage");
    webMgr.mountFilesystems();

    // Initialize font subsystem
    FontMgr::getInstance().init();

    // Load display orientation from internal storage
    displayMgr.loadDisplaySettings();

    displayMgr.showBootScreen(72, "Preparing controls");
    BatteryMgr::getInstance().init();

    // Initialize input management
    inputMgr.init();

    // Register core applications
    appMgr.registerApp(new AppMainMenu());
    AppReader* readerApp = new AppReader();
    appMgr.registerApp(readerApp);

    AppSettings* settingsApp = new AppSettings();
    appMgr.registerApp(settingsApp);

    displayMgr.showBootScreen(90, "Starting network");
    gNetworkStartupInProgress = true;
    BaseType_t networkTaskStarted =
        xTaskCreatePinnedToCore(networkStartupTask, "NetworkStart", 12288, nullptr, 1, nullptr, 0);
    if (networkTaskStarted != pdPASS) {
        gNetworkStartupInProgress = false;
        WebMgr::getInstance().sendLog("Failed to start network task; continuing offline");
    }

    // Check joystick calibration on internal SystemFS partition
    if (!SystemFS.exists("/joy_cal.json")) {
        displayMgr.showBootScreen(100, "Joystick Setup");
        appMgr.switchTo(2);
        settingsApp->startCalibrationWizard();
    } else if (readerApp->hasBootResume()) {
        displayMgr.showBootScreen(100, "Opening reader");
        readerApp->resumeSavedBookOnStart();
        appMgr.switchTo(1);
    } else {
        displayMgr.showBootScreen(100, "Opening menu");
        appMgr.switchTo(0);
    }

    WebMgr::getInstance().sendLog("Setup Complete");
}

void loop() {
    InputMgr::getInstance().update();
    AppMgr::getInstance().update();

    // Lazy rendering debouncer: avoids repaints while user interacts with physical keys
    static unsigned long lastPhysicalInputTime = 0;
    if (InputMgr::getInstance().isInteracting()) {
        lastPhysicalInputTime = millis();
    }

    if (millis() - lastPhysicalInputTime > 200) {
        AppMgr::getInstance().draw();
    }

    WebMgr::getInstance().update();
    BatteryMgr::getInstance().update();

    App* currentApp = AppMgr::getInstance().getCurrentApp();
    if (!currentApp || currentApp->allowsSystemStatusIndicator()) {
        if (millis() - lastPhysicalInputTime > 200) {
            BatteryMgr::getInstance().drawStatusIndicator();
        }
    }

    delay(1);
}