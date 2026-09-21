#include "BatteryMgr.h"
#include "../../include/Config.h"
#include "Config.h"
#include <esp_sleep.h>
#include "KomaBonFS.h"
#include "DisplayMgr.h"
#include <ArduinoJson.h>
#include "Fonts/FreeSans.h"
#include "SDMgr.h"
#include <WiFi.h>

const float BatteryMgr::CHARGE_THRESHOLD = 0.03f;
const float BatteryMgr::CRITICAL_VOLTAGE = 3.0f;
const float BatteryMgr::HIGH_VOLTAGE_THRESHOLD = 4.0f;
const float BatteryMgr::SPIKE_REJECT_THRESHOLD = 0.5f;

static int voltageToPercentage(float voltage) {
    if (voltage >= BATTERY_FULL_VOLTAGE) return 100;
    if (voltage <= BATTERY_EMPTY_VOLTAGE) return 0;

    return (int)(((voltage - BATTERY_EMPTY_VOLTAGE) / (BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE)) *
                     100.0f +
                 0.5f);
}

BatteryMgr::BatteryMgr()
    : _lastReadTime(0), _historyIndex(0), _lastHistoryUpdate(0), _previousVoltage(0.0f),
      _sleepTimeoutMinutes(0), _sleepMessage("Press button to wake"), _lastActivityTime(0),
      _lastValidVoltage(0.0f), _criticalCount(0), _lastChargingTime(0) {
    _cachedStatus = {0.0f, 0, false};
    for (int i = 0; i < 5; i++) {
        _voltageHistory[i] = 0.0f;
        _historyTimes[i] = 0;
    }
}

BatteryMgr& BatteryMgr::getInstance() {
    static BatteryMgr instance;
    return instance;
}

void BatteryMgr::init() {
    Book32Guard guard(_mutex);
    pinMode(PIN_BAT_VOLT, INPUT);
#ifdef PIN_VBAT_SWITCH
    pinMode(PIN_VBAT_SWITCH, OUTPUT);
    digitalWrite(PIN_VBAT_SWITCH, !VBAT_SWITCH_LEVEL);
#endif
    analogSetAttenuation(ADC_11db);

    updateCache();

    _previousVoltage = _cachedStatus.voltage;
    for (int i = 0; i < 5; i++) {
        _voltageHistory[i] = _cachedStatus.voltage;
        _historyTimes[i] = millis();
    }
    _lastHistoryUpdate = millis();

    Serial.printf("Battery: Initial voltage %.2fV (%d%%)\n", _cachedStatus.voltage, _cachedStatus.percentage);

    loadSleepSettings();
    _lastActivityTime = millis();
}

void BatteryMgr::update() {
    unsigned long now = millis();

    {
        Book32Guard guard(_mutex);

        if (now - _lastHistoryUpdate >= HISTORY_INTERVAL_MS) {
            if (now - _lastReadTime >= CACHE_DURATION_MS) {
                updateCache();
            }

            _voltageHistory[_historyIndex] = _cachedStatus.voltage;
            _historyTimes[_historyIndex] = now;
            _historyIndex = (_historyIndex + 1) % 5;
            _lastHistoryUpdate = now;

            int oldestIndex = _historyIndex;
            float oldestVoltage = _voltageHistory[oldestIndex];
            unsigned long oldestTime = _historyTimes[oldestIndex];

            if (oldestTime > 0 && (now - oldestTime) >= 90000) {
                float voltageChange = _cachedStatus.voltage - oldestVoltage;

                if (voltageChange > CHARGE_THRESHOLD) {
                    if (!_cachedStatus.charging) {
                        _cachedStatus.charging = true;
                        Serial.printf("Battery: Charging detected via trend (%.3fV -> %.3fV, +%.3fV)\n",
                                      oldestVoltage, _cachedStatus.voltage, voltageChange);
                    }
                    _lastChargingTime = now;
                } else if (_cachedStatus.voltage < HIGH_VOLTAGE_THRESHOLD &&
                           voltageChange < -(CHARGE_THRESHOLD * 3.0f)) {
                    if (_cachedStatus.charging) {
                        _cachedStatus.charging = false;
                        Serial.printf("Battery: Discharging detected (%.3fV -> %.3fV, %.3fV)\n",
                                      oldestVoltage, _cachedStatus.voltage, voltageChange);
                    }
                }
            }
        }
    }

    if (isCriticallyLow()) {
        Serial.println("CRITICAL: Battery voltage too low! Shutting down...");
        shutdownLowBattery();
    }

    int sleepTimeoutMinutes;
    bool charging;
    unsigned long lastActivity;
    {
        Book32Guard guard(_mutex);
        sleepTimeoutMinutes = _sleepTimeoutMinutes;
        charging = _cachedStatus.charging;
        lastActivity = _lastActivityTime;
    }

    if (sleepTimeoutMinutes > 0 && !charging) {
        unsigned long idleNow = millis();
        unsigned long idleTime = idleNow - lastActivity;
        unsigned long timeoutMs = (unsigned long)sleepTimeoutMinutes * 60 * 1000;
        if (idleTime >= timeoutMs) {
            Serial.printf("SLEEPDIAG: path=IDLE_TIMEOUT  idle=%lums  timeout=%lums\n", idleTime, timeoutMs);
            Serial.printf("Idle timeout reached (%d minutes). Entering sleep...\n", sleepTimeoutMinutes);
            enterIdleSleep("idle_timeout");
        }
    }
}

void BatteryMgr::updateCache(bool clearStaleCharging) {
    Book32Guard guard(_mutex);
#ifdef PIN_VBAT_SWITCH
    digitalWrite(PIN_VBAT_SWITCH, VBAT_SWITCH_LEVEL);
    delay(5);
#endif

    analogRead(PIN_BAT_VOLT);
    uint32_t raw_mv = 0;
    for (int i = 0; i < 30; i++) {
        raw_mv += analogReadMilliVolts(PIN_BAT_VOLT);
        delay(1);
    }
    raw_mv /= 30;

#ifdef PIN_VBAT_SWITCH
    digitalWrite(PIN_VBAT_SWITCH, !VBAT_SWITCH_LEVEL);
#endif

    float voltage = (raw_mv / 1000.0f) * 2.0f;
    voltage *= BATTERY_VOLTAGE_CALIBRATION;
    if (voltage > BATTERY_FULL_VOLTAGE) {
        voltage = BATTERY_FULL_VOLTAGE;
    }

    if (_lastValidVoltage > 0.0f && fabsf(voltage - _lastValidVoltage) > SPIKE_REJECT_THRESHOLD) {
        voltage = _lastValidVoltage;
    } else {
        _lastValidVoltage = voltage;
    }

    int percentage = voltageToPercentage(voltage);
    float previousVoltage = _previousVoltage;

    bool currentCharging = _cachedStatus.charging;
    if (clearStaleCharging) {
        currentCharging = false;
    }

    if (previousVoltage > 0 && voltage > previousVoltage + 0.25f) {
        if (!currentCharging) {
            currentCharging = true;
            Serial.printf("Battery: Hard USB plug detected (%.3fV -> %.3fV, +%.3fV)\n", previousVoltage,
                          voltage, voltage - previousVoltage);
        }
        _lastChargingTime = millis();
    } else if (previousVoltage > 0 && voltage < previousVoltage - 0.20f) {
        if (currentCharging) {
            currentCharging = false;
            Serial.printf("Battery: Hard USB unplug detected (%.3fV -> %.3fV, %.3fV)\n", previousVoltage,
                          voltage, voltage - previousVoltage);
        }
    }

    _previousVoltage = voltage;
    _cachedStatus = {voltage, percentage, currentCharging};
    _lastReadTime = millis();
}

bool BatteryMgr::isCriticallyLow() {
    Book32Guard guard(_mutex);
    if (millis() - _lastReadTime >= CACHE_DURATION_MS) {
        updateCache();
    }

    if (_lastChargingTime > 0 && (millis() - _lastChargingTime) < CHARGING_GRACE_MS) {
        _criticalCount = 0;
        return false;
    }

    if (_cachedStatus.voltage <= CRITICAL_VOLTAGE && !_cachedStatus.charging) {
        _criticalCount++;
        Serial.printf("Battery: Critical reading #%d (%.2fV)\n", _criticalCount, _cachedStatus.voltage);
        if (_criticalCount >= CRITICAL_CONFIRM_COUNT) {
            return true;
        }
    } else {
        if (_criticalCount > 0) {
            Serial.printf("Battery: Critical counter reset (voltage=%.2fV, charging=%s)\n",
                          _cachedStatus.voltage, _cachedStatus.charging ? "yes" : "no");
        }
        _criticalCount = 0;
    }
    return false;
}

void BatteryMgr::shutdownLowBattery() {
    Serial.println("Battery critically low - entering deep sleep");
    Serial.printf("Voltage: %.2fV\n", _cachedStatus.voltage);
    Serial.flush();
    delay(100);
    esp_deep_sleep_start();
}

BatteryStatus BatteryMgr::getStatus() {
    Book32Guard guard(_mutex);
    if (millis() - _lastReadTime >= CACHE_DURATION_MS) {
        updateCache();
    }
    return _cachedStatus;
}

BatteryStatus BatteryMgr::refreshNow() {
    Book32Guard guard(_mutex);
    updateCache(true);
    return _cachedStatus;
}

float BatteryMgr::getVoltage() {
    return getStatus().voltage;
}

int BatteryMgr::getPercentage() {
    return getStatus().percentage;
}

bool BatteryMgr::isCharging() {
    return getStatus().charging;
}

void BatteryMgr::loadSleepSettings() {
    Book32Guard guard(_mutex);
    if (EbookFS.exists("/sleep_config.json")) {
        File file = EbookFS.open("/sleep_config.json", "r");
        if (file) {
            DynamicJsonDocument doc(512);
            if (!deserializeJson(doc, file)) {
                _sleepTimeoutMinutes = doc.containsKey("sleepTimeout") ? doc["sleepTimeout"].as<int>() : 0;
                _sleepMessage = doc["sleepMessage"] | "Press button to wake";
            }
            file.close();
        }
    } else {
        _sleepTimeoutMinutes = 0;
        _sleepMessage = "Press button to wake";
    }
}

void BatteryMgr::resetIdleTimer() {
    Book32Guard guard(_mutex);
    _lastActivityTime = millis();
}

void BatteryMgr::enterIdleSleep(const char* reason) {
    String sleepMessage;
    {
        Book32Guard guard(_mutex);
        sleepMessage = _sleepMessage;
    }

    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeSans18pt8b);
        display.setTextColor(GxEPD_BLACK);

        int16_t tbx, tby;
        uint16_t tbw, tbh;
        display.getTextBounds(sleepMessage.c_str(), 0, 0, &tbx, &tby, &tbw, &tbh);

        int16_t x = (display.width() - tbw) / 2 - tbx;
        int16_t y = (display.height() - tbh) / 2 - tby;

        display.setCursor(x, y);
        display.print(sleepMessage);
    } while (display.nextPage());

    delay(100);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BUTTON_BACK, 0);

    delay(50);
    esp_deep_sleep_start();
}

void BatteryMgr::drawStatusBar(KomaBonDisplay& display, int startX, int startY) {
    bool currentWifi = (WiFi.status() == WL_CONNECTED);
    bool currentSd = SDMgr::getInstance().isMounted();
    BatteryStatus bat = getStatus();
    int percentage = bat.percentage;
    bool currentCharging = bat.charging;

    // Tighter bounding box, pushed to the top-right corner
    const int INDICATOR_WIDTH = 110;
    int cx = display.width() - INDICATOR_WIDTH - 4; // Minimal right margin
    int cy = 4;                                     // Minimal top margin

    display.setTextColor(GxEPD_BLACK);

    // 1. Wi-Fi Icon (Scaled down to 3px bars)
    if (currentWifi) {
        display.fillRect(cx, cy + 10, 3, 5, GxEPD_BLACK);
        display.fillRect(cx + 5, cy + 5, 3, 10, GxEPD_BLACK);
        display.fillRect(cx + 10, cy, 3, 15, GxEPD_BLACK);
    } else {
        display.drawLine(cx, cy + 15, cx + 13, cy + 2, GxEPD_BLACK);
        display.drawLine(cx, cy + 14, cx + 13, cy + 1, GxEPD_BLACK);
    }
    cx += 18;

    // 2. SD Icon (Scaled down to 12x16)
    if (currentSd) {
        display.fillRect(cx, cy, 12, 16, GxEPD_BLACK);
        display.fillRect(cx + 2, cy + 2, 8, 12, GxEPD_WHITE);
        display.fillRect(cx + 2, cy, 3, 2, GxEPD_WHITE);     // Corner notch
        display.fillRect(cx + 2, cy + 5, 8, 6, GxEPD_BLACK); // Inner contacts
    }
    cx += 18;

    // 3. Battery Icon (Scaled down to 20x10)
    int batW = 20;
    int batH = 10;
    display.fillRect(cx, cy + 3, batW, batH, GxEPD_BLACK);
    display.fillRect(cx + 2, cy + 5, batW - 4, batH - 4, GxEPD_WHITE);
    // Positive terminal
    display.fillRect(cx + batW, cy + 5, 2, 6, GxEPD_BLACK);

    int fill = (percentage * (batW - 4)) / 100;
    if (fill > 0) display.fillRect(cx + 2, cy + 5, fill, batH - 4, GxEPD_BLACK);

    cx += batW + 6;

    // 4. Percentage Text (Baseline adjusted for new cy)
    display.setFont(&FreeSans9pt8b);
    display.setCursor(cx, cy + 13);
    display.printf("%d%%", percentage);
    if (currentCharging) display.print("+");
}