#pragma once
#include <Arduino.h>
#include "Lock.h"
#include "DisplayMgr.h"

// Combined battery status to avoid multiple ADC reads
struct BatteryStatus {
    float voltage;
    int percentage;
    bool charging;
};

class BatteryMgr {
  public:
    static BatteryMgr& getInstance();

    void init();

    // Call periodically from main loop to update charge detection and check critical battery
    void update();

    // Preferred: Get all battery info from single cached read
    BatteryStatus getStatus();
    BatteryStatus refreshNow();

    // Legacy methods (still work, but use getStatus() to avoid multiple reads)
    float getVoltage();
    int getPercentage();
    bool isCharging();

    // Check if battery is critically low (should shutdown)
    bool isCriticallyLow();

    // Safely power off the device (deep sleep)
    void shutdownLowBattery();

    // Idle sleep management
    void loadSleepSettings(); // Load from EbookFS
    void resetIdleTimer();    // Call when user interacts
    void enterIdleSleep(const char* reason = "unspecified");

    // Status indicator on e-ink display (partial update)
    void drawStatusIndicator();

    // Draws the complete status bar icons (Wi-Fi, SD, Battery) into the provided display buffer
    void drawStatusBar(KomaBonDisplay& display, int startX, int startY);

  private:
    BatteryMgr();

    void updateCache(bool clearStaleCharging = false);

    Book32Mutex _mutex;

    BatteryStatus _cachedStatus;
    unsigned long _lastReadTime;
    static const unsigned long CACHE_DURATION_MS = 5000;

    float _voltageHistory[5];
    unsigned long _historyTimes[5];
    int _historyIndex;
    unsigned long _lastHistoryUpdate;
    float _previousVoltage;
    static const unsigned long HISTORY_INTERVAL_MS = 30000;
    static const float CHARGE_THRESHOLD;
    static const float HIGH_VOLTAGE_THRESHOLD;

    static const float CRITICAL_VOLTAGE;

    float _lastValidVoltage;
    int _criticalCount;
    unsigned long _lastChargingTime;
    static const int CRITICAL_CONFIRM_COUNT = 3;
    static const float SPIKE_REJECT_THRESHOLD;
    static const unsigned long CHARGING_GRACE_MS = 60000;

    int _sleepTimeoutMinutes;
    String _sleepMessage;
    unsigned long _lastActivityTime;

    // Status indicator tracking (declared only once)
    bool _lastDisplayedCharging;
    bool _lastDisplayedWifi;
    bool _lastDisplayedSd;
    unsigned long _lastIndicatorUpdate;
};