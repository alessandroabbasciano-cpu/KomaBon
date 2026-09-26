#ifndef CRASH_HANDLER_H
#define CRASH_HANDLER_H

#include <Arduino.h>
#include <esp_system.h>

struct CrashBreadcrumb {
    uint32_t magic;
    uint32_t bootCount;
    uint32_t uptimeSeconds;
    char currentApp[20];
    char currentAction[36];
    uint32_t freeHeap;
};

class CrashHandler {
  public:
    static CrashHandler& getInstance() {
        static CrashHandler instance;
        return instance;
    }

    void init();
    void setBreadcrumb(const char* app, const char* action);

    const char* getResetReasonString() const;
    esp_reset_reason_t getResetReason() const {
        return _lastResetReason;
    }

    bool hasCrashLog() const;
    String getCrashLog() const;
    bool clearCrashLog();
    uint32_t getBootCount() const;

  private:
    CrashHandler();
    esp_reset_reason_t _lastResetReason;
    bool _hasCrashLog;
};

#endif // CRASH_HANDLER_H
