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
#include "../../include/NetworkState.h"
#include "AppMgr.h"
#include "FontMgr.h"
#include "JoystickMgr.h"
#include <driver/rtc_io.h>
#include "SettingsStore.h"

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
      _sleepTimeoutMinutes(0), _sleepScreenMode(SLEEP_SCREEN_COVER), _sleepMessage("Press button to wake"),
      _lastActivityTime(0), _lastValidVoltage(0.0f), _criticalCount(0), _lastChargingTime(0) {
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
    KomaBonGuard guard(_mutex);
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
        KomaBonGuard guard(_mutex);

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
        KomaBonGuard guard(_mutex);
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
    KomaBonGuard guard(_mutex);
#ifdef PIN_VBAT_SWITCH
    digitalWrite(PIN_VBAT_SWITCH, VBAT_SWITCH_LEVEL);
    // RC STABILIZATION: 5 Tau = 25ms (R28||R29=50k, C62=100nF). Padded to 30ms.
    delay(30);
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

    float rawVoltage = (raw_mv / 1000.0f) * 2.0f;
    rawVoltage *= BATTERY_VOLTAGE_CALIBRATION;
    if (rawVoltage > BATTERY_FULL_VOLTAGE) {
        rawVoltage = BATTERY_FULL_VOLTAGE;
    }

    // EXPONENTIAL MOVING AVERAGE (EMA): Kills hardware noise and micro-bounces
    if (_lastValidVoltage <= 0.0f) {
        _lastValidVoltage = rawVoltage;
    } else {
        // 85% historical weight, 15% new reading weight
        _lastValidVoltage = (_lastValidVoltage * 0.85f) + (rawVoltage * 0.15f);
    }

    float voltage = _lastValidVoltage;
    int percentage = voltageToPercentage(voltage);
    float previousVoltage = _previousVoltage;

    bool currentCharging = _cachedStatus.charging;
    if (clearStaleCharging) {
        currentCharging = false;
    }

    if (previousVoltage > 0 && voltage > previousVoltage + 0.25f) {
        if (!currentCharging && !gNetworkStartupInProgress) {
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
    KomaBonGuard guard(_mutex);
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
    enterIdleSleep("low_battery");
}

BatteryStatus BatteryMgr::getStatus() {
    KomaBonGuard guard(_mutex);
    if (millis() - _lastReadTime >= CACHE_DURATION_MS) {
        updateCache();
    }
    return _cachedStatus;
}

BatteryStatus BatteryMgr::refreshNow() {
    KomaBonGuard guard(_mutex);
    updateCache(true);
    return _cachedStatus;
}

void BatteryMgr::loadSleepSettings() {
    KomaBonGuard guard(_mutex);
    SleepSettings s = SettingsStore::getInstance().loadSleep();
    _sleepTimeoutMinutes = s.timeout;
    _sleepScreenMode = s.screenMode;
    _sleepMessage = s.message;
}

void BatteryMgr::resetIdleTimer() {
    KomaBonGuard guard(_mutex);
    _lastActivityTime = millis();
}

bool BatteryMgr::loadCustomScreensaver(uint8_t* buffer, size_t maxLen) {
    if (!buffer || maxLen < 48000) return false;
    if (!KomaBonStorage::ensureReady()) return false;

    File f;
    if (EbookFS.exists("/screensaver.raw")) {
        f = EbookFS.open("/screensaver.raw", "r");
    } else if (EbookFS.exists("/screensavers/sleep.raw")) {
        f = EbookFS.open("/screensavers/sleep.raw", "r");
    } else if (SystemFS.exists("/screensaver.raw")) {
        f = SystemFS.open("/screensaver.raw", "r");
    }

    if (!f) return false;

    size_t fSize = f.size();
    if (fSize == 48004) {
        uint8_t hdr[4];
        if (f.read(hdr, 4) != 4) {
            f.close();
            return false;
        }
        size_t bytesRead = f.read(buffer, 48000);
        f.close();
        return (bytesRead == 48000);
    } else if (fSize == 48000) {
        size_t bytesRead = f.read(buffer, 48000);
        f.close();
        return (bytesRead == 48000);
    }

    f.close();
    return false;
}

void BatteryMgr::drawDefaultSleepScreen() {
    KomaBonDisplay& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    display.setFullWindow();

    bool drawnCustom = false;
    uint8_t* customBuf = nullptr;
    if (_sleepScreenMode == SLEEP_SCREEN_CUSTOM) {
        customBuf = (uint8_t*)ps_malloc(48000);
        if (!customBuf) customBuf = (uint8_t*)malloc(48000);
        if (customBuf && loadCustomScreensaver(customBuf, 48000)) {
            drawnCustom = true;
        }
    }

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        if (drawnCustom && customBuf) {
            display.drawBitmap(0, 0, customBuf, 480, 800, GxEPD_BLACK);
        } else {
            // Elegant minimal KomaBon branding
            fontMgr.drawTextCenteredBold(display, "KomaBon", 390, FONT_SIZE_HEADER, GxEPD_BLACK);
        }

        // Status bar on top: Wi-Fi, SD, and Battery icon + %
        drawStatusBar(display, display.width() - 105, 10);

    } while (display.nextPage());

    if (customBuf) free(customBuf);
}

void BatteryMgr::prepareAndEnterDeepSleep() {
    Serial.println("BatteryMgr: Preparing hardware for deep sleep (<20uA)...");
    Serial.flush();

    // 1. Put E-Ink display controller into hibernate mode
    // Sends deep sleep command and turns off high-voltage PREVGH / PREVGL charge pumps
    DisplayMgr::getInstance().getDisplay().hibernate();

    // 2. Shut down MicroSD and SPI bus to eliminate parasitic leakage
    SDMgr::getInstance().end();

    // 3. Ensure Wi-Fi radio is completely powered off
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // 4. Wait for user to release all physical inputs (joystick and buttons)
    // to prevent immediate spurious wakeup from the held gesture
    unsigned long releaseStart = millis();
    while ((digitalRead(PIN_BUTTON_BACK) == LOW ||
            digitalRead(PIN_BUTTON_SLEEP) == LOW ||
            JoystickMgr::getInstance().getDirection() != JOY_NONE) &&
           (millis() - releaseStart < 3000)) {
        delay(20);
    }
    delay(100); // Debounce physical switch release

    // 5. Configure RTC pullups and EXT1 wakeup on ESP32-S3:
    // JOY_ADC_PIN (GPIO 2, any joystick movement), PIN_BUTTON_SLEEP (GPIO 3), PIN_BUTTON_BACK (GPIO 5)
    pinMode(JOY_ADC_PIN, INPUT_PULLUP);
    pinMode(PIN_BUTTON_SLEEP, INPUT_PULLUP);
    pinMode(PIN_BUTTON_BACK, INPUT_PULLUP);

    rtc_gpio_pullup_en((gpio_num_t)JOY_ADC_PIN);
    rtc_gpio_pulldown_dis((gpio_num_t)JOY_ADC_PIN);

    rtc_gpio_pullup_en((gpio_num_t)PIN_BUTTON_SLEEP);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_BUTTON_SLEEP);

    rtc_gpio_pullup_en((gpio_num_t)PIN_BUTTON_BACK);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_BUTTON_BACK);

    esp_sleep_enable_ext1_wakeup(
        (1ULL << JOY_ADC_PIN) | (1ULL << PIN_BUTTON_SLEEP) | (1ULL << PIN_BUTTON_BACK),
        ESP_EXT1_WAKEUP_ANY_LOW
    );

    Serial.println("BatteryMgr: Entering ESP32 Deep Sleep now. Zzz...");
    Serial.flush();
    delay(50);
    esp_deep_sleep_start();
}

void BatteryMgr::reenterDeepSleep() {
    // Abort sequence when wake-up conditions (e.g. 900ms hold) are not met.
    // Wait for physical contacts to be released to avoid immediate wake loops.
    unsigned long releaseStart = millis();
    while ((digitalRead(PIN_BUTTON_BACK) == LOW ||
            digitalRead(PIN_BUTTON_SLEEP) == LOW ||
            analogRead(JOY_ADC_PIN) < 3800) &&
           (millis() - releaseStart < 2000)) {
        delay(20);
    }
    delay(50);

    pinMode(JOY_ADC_PIN, INPUT_PULLUP);
    pinMode(PIN_BUTTON_SLEEP, INPUT_PULLUP);
    pinMode(PIN_BUTTON_BACK, INPUT_PULLUP);

    rtc_gpio_pullup_en((gpio_num_t)JOY_ADC_PIN);
    rtc_gpio_pulldown_dis((gpio_num_t)JOY_ADC_PIN);
    rtc_gpio_pullup_en((gpio_num_t)PIN_BUTTON_SLEEP);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_BUTTON_SLEEP);
    rtc_gpio_pullup_en((gpio_num_t)PIN_BUTTON_BACK);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_BUTTON_BACK);

    esp_sleep_enable_ext1_wakeup(
        (1ULL << JOY_ADC_PIN) | (1ULL << PIN_BUTTON_SLEEP) | (1ULL << PIN_BUTTON_BACK),
        ESP_EXT1_WAKEUP_ANY_LOW
    );

    Serial.println("BatteryMgr: Re-entering deep sleep (unconfirmed wake).");
    Serial.flush();
    delay(20);
    esp_deep_sleep_start();
}

void BatteryMgr::enterIdleSleep(const char* reason) {
    Serial.printf("BatteryMgr: Entering sleep (reason: %s)...\n", reason);
    App* current = AppMgr::getInstance().getCurrentApp();
    bool handled = false;
    if (current) {
        handled = current->handleSleep();
    }
    if (!handled) {
        if (current) current->stop();
        drawDefaultSleepScreen();
    }
    prepareAndEnterDeepSleep();
}

void BatteryMgr::drawStatusBar(KomaBonDisplay& display, int startX, int startY) {
    bool currentWifi = (WiFi.status() == WL_CONNECTED);
    bool currentSd = SDMgr::getInstance().isMounted();
    BatteryStatus bat = getStatus();
    int percentage = bat.percentage;
    bool currentCharging = bat.charging;

    const int INDICATOR_WIDTH = 110;
    int cx = display.width() - INDICATOR_WIDTH - 4;
    int cy = 4;

    display.setTextColor(GxEPD_BLACK);

    if (currentWifi) {
        display.fillRect(cx, cy + 10, 3, 5, GxEPD_BLACK);
        display.fillRect(cx + 5, cy + 5, 3, 10, GxEPD_BLACK);
        display.fillRect(cx + 10, cy, 3, 15, GxEPD_BLACK);
    } else {
        display.drawLine(cx, cy + 15, cx + 13, cy + 2, GxEPD_BLACK);
        display.drawLine(cx, cy + 14, cx + 13, cy + 1, GxEPD_BLACK);
    }
    cx += 18;

    if (currentSd) {
        display.fillRect(cx, cy, 12, 16, GxEPD_BLACK);
        display.fillRect(cx + 2, cy + 2, 8, 12, GxEPD_WHITE);
        display.fillRect(cx + 2, cy, 3, 2, GxEPD_WHITE);
        display.fillRect(cx + 2, cy + 5, 8, 6, GxEPD_BLACK);
    }
    cx += 18;

    int batW = 20;
    int batH = 10;
    display.fillRect(cx, cy + 3, batW, batH, GxEPD_BLACK);
    display.fillRect(cx + 2, cy + 5, batW - 4, batH - 4, GxEPD_WHITE);
    display.fillRect(cx + batW, cy + 5, 2, 6, GxEPD_BLACK);

    int fill = (percentage * (batW - 4)) / 100;
    if (fill > 0) display.fillRect(cx + 2, cy + 5, fill, batH - 4, GxEPD_BLACK);

    cx += batW + 6;

    display.setFont(&FreeSans9pt8b);
    display.setCursor(cx, cy + 13);
    display.printf("%d%%", percentage);
    if (currentCharging) display.print("+");
}