#include "JoystickMgr.h"
#include "Config.h"
#include "KomaBonFS.h"
#include <ArduinoJson.h>

JoystickMgr::JoystickMgr() {
    _cal = {0, 3350, 1250, 2650, 1950};
}

JoyDirection JoystickMgr::getDirection() {
    int val = readAnalogAveraged();
    if (val > 3800) return JOY_NONE;

    int dCenter = abs(val - _cal.center);
    int dUp = abs(val - _cal.up);
    int dDown = abs(val - _cal.down);
    int dLeft = abs(val - _cal.left);
    int dRight = abs(val - _cal.right);

    int minD = dCenter;
    JoyDirection dir = JOY_CENTER;

    if (dUp < minD) {
        minD = dUp;
        dir = JOY_UP;
    }
    if (dDown < minD) {
        minD = dDown;
        dir = JOY_DOWN;
    }
    if (dLeft < minD) {
        minD = dLeft;
        dir = JOY_LEFT;
    }
    if (dRight < minD) {
        minD = dRight;
        dir = JOY_RIGHT;
    }

    if (minD > 500) return JOY_NONE;
    return dir;
}

bool JoystickMgr::loadCalibration() {
    File file;
    if (EbookFS.exists("/joy_cal.json")) {
        file = EbookFS.open("/joy_cal.json", "r");
    } else if (SystemFS.exists("/joy_cal.json")) {
        file = SystemFS.open("/joy_cal.json", "r");
    } else {
        return false;
    }

    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (err) return false;

    _cal.center = doc["center"] | 0;
    _cal.up = doc["up"] | 3350;
    _cal.down = doc["down"] | 1250;
    _cal.left = doc["left"] | 2650;
    _cal.right = doc["right"] | 1950;

    Serial.println("JoystickMgr: Calibration v2 loaded.");
    return true;
}

bool JoystickMgr::saveCalibration(int center, int up, int down, int left, int right) {
    _cal.center = center;
    _cal.up = up;
    _cal.down = down;
    _cal.left = left;
    _cal.right = right;

    DynamicJsonDocument doc(512);
    doc["center"] = _cal.center;
    doc["up"] = _cal.up;
    doc["down"] = _cal.down;
    doc["left"] = _cal.left;
    doc["right"] = _cal.right;

    File file = EbookFS.open("/joy_cal.json", "w");
    if (!file) return false;
    serializeJson(doc, file);
    file.close();

    Serial.println("JoystickMgr: Calibration saved to /joy_cal.json on EbookFS");
    return true;
}

void JoystickMgr::setCalibration(const JoyCalibration& cal) {
    _cal = cal;
}

int JoystickMgr::readAnalogAveraged() {
    const int numSamples = 16;
    long sum = 0;
    for (int i = 0; i < numSamples; i++) {
        sum += analogRead(JOY_ADC_PIN);
        delayMicroseconds(50);
    }
    return sum / numSamples;
}

void JoystickMgr::init() {
    pinMode(JOY_ADC_PIN, ANALOG); // explicitly disable digital I/O buffer to prevent leakage
    analogSetPinAttenuation(JOY_ADC_PIN, ADC_11db);
    analogReadResolution(12);
    Serial.println("JoystickMgr: ADC1 initialized safely on JOY_ADC_PIN.");

    if (!loadCalibration()) {
        Serial.println("JoystickMgr: No calibration file found, using defaults.");
    }
}