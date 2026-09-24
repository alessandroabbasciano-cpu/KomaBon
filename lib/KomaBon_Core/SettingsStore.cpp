#include "SettingsStore.h"
#include "KomaBonFS.h"
#include <ArduinoJson.h>

static const char* READER_CONFIG_PATH = "/reader_config.json";
static const char* DISPLAY_CONFIG_PATH = "/display_config.json";
static const char* SLEEP_CONFIG_PATH = "/sleep_config.json";

SettingsStore& SettingsStore::getInstance() {
    static SettingsStore instance;
    return instance;
}

SettingsStore::Transaction::Transaction() {
    SettingsStore::getInstance()._mutex.lock();
}

SettingsStore::Transaction::~Transaction() {
    SettingsStore::getInstance()._mutex.unlock();
}

int SettingsStore::clampFontSize(int pt) {
    if (pt >= 18) return 18;
    if (pt >= 12) return 12;
    return 9;
}

int SettingsStore::clampFontFamily(int family) {
    if (family < 0 || family > 5) return 0;
    return family;
}

int SettingsStore::clampRotation(int rotation) {
    if (rotation < 0 || rotation > 3) return 3;
    return rotation;
}

int SettingsStore::clampRefreshFrequency(int n) {
    if (n < 1) return 1;
    if (n > 100) return 100;
    return n;
}

int SettingsStore::clampSleepTimeout(int minutes) {
    if (minutes < 0) return 0;
    if (minutes > 240) return 240;
    return minutes;
}

ReaderSettings SettingsStore::loadReader() {
    KomaBonGuard guard(_mutex);
    ReaderSettings s;

    File file;
    if (EbookFS.exists(READER_CONFIG_PATH)) {
        file = EbookFS.open(READER_CONFIG_PATH, "r");
    } else if (SystemFS.exists(READER_CONFIG_PATH)) {
        file = SystemFS.open(READER_CONFIG_PATH, "r");
    }

    if (file) {
        DynamicJsonDocument doc(256);
        if (!deserializeJson(doc, file)) {
            s.refreshFrequency = clampRefreshFrequency(doc["refreshFrequency"] | 10);
            s.fontSize = clampFontSize(doc["fontSize"] | 9);
            s.fontFamily = clampFontFamily(doc["fontFamily"] | 0);
        }
        file.close();
    }

    return s;
}

DisplaySettings SettingsStore::loadDisplay() {
    KomaBonGuard guard(_mutex);
    DisplaySettings s;

    File file;
    if (EbookFS.exists(DISPLAY_CONFIG_PATH)) {
        file = EbookFS.open(DISPLAY_CONFIG_PATH, "r");
    } else if (SystemFS.exists(DISPLAY_CONFIG_PATH)) {
        file = SystemFS.open(DISPLAY_CONFIG_PATH, "r");
    }

    if (file) {
        DynamicJsonDocument doc(128);
        if (!deserializeJson(doc, file)) {
            s.rotation = clampRotation(doc["rotation"] | 3);
        }
        file.close();
    }

    return s;
}

SleepSettings SettingsStore::loadSleep() {
    KomaBonGuard guard(_mutex);
    SleepSettings s;

    File file;
    if (EbookFS.exists(SLEEP_CONFIG_PATH)) {
        file = EbookFS.open(SLEEP_CONFIG_PATH, "r");
    } else if (SystemFS.exists(SLEEP_CONFIG_PATH)) {
        file = SystemFS.open(SLEEP_CONFIG_PATH, "r");
    }

    if (file) {
        DynamicJsonDocument doc(512);
        if (!deserializeJson(doc, file)) {
            s.timeout = clampSleepTimeout(doc["sleepTimeout"] | 0);
            s.message = doc["sleepMessage"] | "Press button to wake";
        }
        file.close();
    }

    return s;
}

bool SettingsStore::saveReader(const ReaderSettings& s) {
    KomaBonGuard guard(_mutex);
    DynamicJsonDocument doc(256);
    doc["refreshFrequency"] = clampRefreshFrequency(s.refreshFrequency);
    doc["fontSize"] = clampFontSize(s.fontSize);
    doc["fontFamily"] = clampFontFamily(s.fontFamily);

    // Atomicity: Write to temporary file first
    String tmpPath = String(READER_CONFIG_PATH) + ".tmp";
    File file = EbookFS.open(tmpPath, FILE_WRITE);
    if (!file) {
        Serial.println("SettingsStore: failed to open temporary reader config for write");
        return false;
    }
    size_t bytesWritten = serializeJson(doc, file);
    file.flush();
    file.close();

    if (bytesWritten == 0) {
        EbookFS.remove(tmpPath);
        return false;
    }

    // Atomicity: Swap original with new data
    if (!EbookFS.rename(tmpPath, READER_CONFIG_PATH)) {
        EbookFS.remove(READER_CONFIG_PATH);
        if (!EbookFS.rename(tmpPath, READER_CONFIG_PATH)) {
            EbookFS.remove(tmpPath);
            return false;
        }
    }
    return true;
}

bool SettingsStore::saveDisplay(const DisplaySettings& s) {
    KomaBonGuard guard(_mutex);
    DynamicJsonDocument doc(128);
    doc["rotation"] = clampRotation(s.rotation);

    String tmpPath = String(DISPLAY_CONFIG_PATH) + ".tmp";
    File file = EbookFS.open(tmpPath, FILE_WRITE);
    if (!file) {
        Serial.println("SettingsStore: failed to open temporary display config for write");
        return false;
    }
    size_t bytesWritten = serializeJson(doc, file);
    file.flush();
    file.close();

    if (bytesWritten == 0) {
        EbookFS.remove(tmpPath);
        return false;
    }

    if (!EbookFS.rename(tmpPath, DISPLAY_CONFIG_PATH)) {
        EbookFS.remove(DISPLAY_CONFIG_PATH);
        if (!EbookFS.rename(tmpPath, DISPLAY_CONFIG_PATH)) {
            EbookFS.remove(tmpPath);
            return false;
        }
    }
    return true;
}

bool SettingsStore::saveSleep(const SleepSettings& s) {
    KomaBonGuard guard(_mutex);
    DynamicJsonDocument doc(512);
    doc["sleepTimeout"] = clampSleepTimeout(s.timeout);
    doc["sleepMessage"] = s.message;

    String tmpPath = String(SLEEP_CONFIG_PATH) + ".tmp";
    File file = EbookFS.open(tmpPath, FILE_WRITE);
    if (!file) {
        Serial.println("SettingsStore: failed to open temporary sleep config for write");
        return false;
    }
    size_t bytesWritten = serializeJson(doc, file);
    file.flush();
    file.close();

    if (bytesWritten == 0) {
        EbookFS.remove(tmpPath);
        return false;
    }

    if (!EbookFS.rename(tmpPath, SLEEP_CONFIG_PATH)) {
        EbookFS.remove(SLEEP_CONFIG_PATH);
        if (!EbookFS.rename(tmpPath, SLEEP_CONFIG_PATH)) {
            EbookFS.remove(tmpPath);
            return false;
        }
    }
    return true;
}