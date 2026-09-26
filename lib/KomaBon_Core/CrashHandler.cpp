#include "CrashHandler.h"
#include "KomaBonFS.h"
#include <LittleFS.h>
#include <esp_sleep.h>

#define CRASH_MAGIC 0x4B4D4243 // "KMBC" (KomaBon Crash)
#define CRASH_LOG_PATH "/crash.log"

RTC_DATA_ATTR static uint32_t s_bootCount = 0;
RTC_NOINIT_ATTR static CrashBreadcrumb s_breadcrumb;

CrashHandler::CrashHandler() : _lastResetReason(ESP_RST_UNKNOWN), _hasCrashLog(false) {}

static const char* getReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXTERNAL_PIN";
        case ESP_RST_SW:        return "SOFTWARE_RESET";
        case ESP_RST_PANIC:     return "EXCEPTION_PANIC";
        case ESP_RST_INT_WDT:   return "INTERRUPT_WATCHDOG";
        case ESP_RST_TASK_WDT:  return "TASK_WATCHDOG";
        case ESP_RST_WDT:       return "OTHER_WATCHDOG";
        case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP_WAKE";
        case ESP_RST_BROWNOUT:  return "BROWNOUT_VOLTAGE_DROP";
        case ESP_RST_SDIO:      return "SDIO_RESET";
        default:                return "UNKNOWN";
    }
}

const char* CrashHandler::getResetReasonString() const {
    return getReasonName(_lastResetReason);
}

uint32_t CrashHandler::getBootCount() const {
    return s_bootCount;
}

void CrashHandler::init() {
    s_bootCount++;
    _lastResetReason = esp_reset_reason();

    Serial.printf("[CrashHandler] Boot #%u | Reset Reason: %s (%d)\n",
                  s_bootCount, getReasonName(_lastResetReason), (int)_lastResetReason);

    // Check if previous reset was abnormal
    bool abnormal = (_lastResetReason == ESP_RST_PANIC ||
                     _lastResetReason == ESP_RST_INT_WDT ||
                     _lastResetReason == ESP_RST_TASK_WDT ||
                     _lastResetReason == ESP_RST_WDT ||
                     _lastResetReason == ESP_RST_BROWNOUT);

    if (abnormal && SystemFS.begin()) {
        File f = SystemFS.open(CRASH_LOG_PATH, "a");
        if (f) {
            f.printf("=== CRASH EVENT (Boot #%u) ===\n", s_bootCount);
            f.printf("Reset Reason: %s (%d)\n", getReasonName(_lastResetReason), (int)_lastResetReason);
            if (s_breadcrumb.magic == CRASH_MAGIC) {
                f.printf("Last Uptime: %u sec\n", s_breadcrumb.uptimeSeconds);
                f.printf("Last App: %s\n", s_breadcrumb.currentApp);
                f.printf("Last Action: %s\n", s_breadcrumb.currentAction);
                f.printf("Free Heap: %u bytes\n", s_breadcrumb.freeHeap);
            } else {
                f.println("Breadcrumb: Not available or memory uninitialized.");
            }
            f.println("================================\n");
            f.close();
            Serial.println("[CrashHandler] Crash event successfully appended to /crash.log");
        }
    }

    // Reset breadcrumb for current session
    s_breadcrumb.magic = CRASH_MAGIC;
    s_breadcrumb.bootCount = s_bootCount;
    s_breadcrumb.uptimeSeconds = 0;
    strncpy(s_breadcrumb.currentApp, "Boot", sizeof(s_breadcrumb.currentApp) - 1);
    s_breadcrumb.currentApp[sizeof(s_breadcrumb.currentApp) - 1] = '\0';
    strncpy(s_breadcrumb.currentAction, "Initializing", sizeof(s_breadcrumb.currentAction) - 1);
    s_breadcrumb.currentAction[sizeof(s_breadcrumb.currentAction) - 1] = '\0';
    s_breadcrumb.freeHeap = esp_get_free_heap_size();

    _hasCrashLog = SystemFS.exists(CRASH_LOG_PATH);
}

void CrashHandler::setBreadcrumb(const char* app, const char* action) {
    s_breadcrumb.magic = CRASH_MAGIC;
    s_breadcrumb.uptimeSeconds = millis() / 1000;
    if (app) {
        strncpy(s_breadcrumb.currentApp, app, sizeof(s_breadcrumb.currentApp) - 1);
        s_breadcrumb.currentApp[sizeof(s_breadcrumb.currentApp) - 1] = '\0';
    }
    if (action) {
        strncpy(s_breadcrumb.currentAction, action, sizeof(s_breadcrumb.currentAction) - 1);
        s_breadcrumb.currentAction[sizeof(s_breadcrumb.currentAction) - 1] = '\0';
    }
    s_breadcrumb.freeHeap = esp_get_free_heap_size();
}

bool CrashHandler::hasCrashLog() const {
    return SystemFS.exists(CRASH_LOG_PATH);
}

String CrashHandler::getCrashLog() const {
    if (!SystemFS.exists(CRASH_LOG_PATH)) return "";
    File f = SystemFS.open(CRASH_LOG_PATH, "r");
    if (!f) return "";
    String content = f.readString();
    f.close();
    return content;
}

bool CrashHandler::clearCrashLog() {
    if (SystemFS.exists(CRASH_LOG_PATH)) {
        return SystemFS.remove(CRASH_LOG_PATH);
    }
    return true;
}
