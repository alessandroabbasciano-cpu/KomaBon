#include "JoystickMgr.h"
#include "Config.h"      // Assumes JOY_ADC_PIN is defined here as 2
#include "KomaBonFS.h"   // NEW: Required for file operations
#include <ArduinoJson.h> // NEW: Required to parse/build the config file

JoystickMgr::JoystickMgr() {
    // Safe fallback defaults in case calibration is missing
    _cal = {0, 3350, 1250, 2650, 1950};
}

JoyDirection JoystickMgr::getDirection() {
    int val = readAnalogAveraged();

    if (val > 3800) return JOY_NONE; // Pull-up resting state

    // Find the "Nearest Neighbor": calculate absolute distance to each target
    int dCenter = abs(val - _cal.center);
    int dUp = abs(val - _cal.up);
    int dDown = abs(val - _cal.down);
    int dLeft = abs(val - _cal.left);
    int dRight = abs(val - _cal.right);

    // Assume center is the closest initially
    int minD = dCenter;
    JoyDirection dir = JOY_CENTER;

    // Check if any other direction is closer
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

    // Ignore completely wild values (noise spike during transition)
    if (minD > 500) return JOY_NONE;

    return dir;
}

bool JoystickMgr::loadCalibration() {
    if (!EbookFS.exists("/joy_cal.json")) return false;

    File file = EbookFS.open("/joy_cal.json", "r");
    if (!file) return false;

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

    Serial.println("JoystickMgr: Calibration saved to /joy_cal.json");
    return true;
}

void JoystickMgr::setCalibration(const JoyCalibration& cal) {
    _cal = cal;
}

int JoystickMgr::readAnalogAveraged() {
    const int numSamples = 16;
    long sum = 0;

    // Perform multisampling to filter out hardware noise
    for (int i = 0; i < numSamples; i++) {
        sum += analogRead(JOY_ADC_PIN);
        delayMicroseconds(50);
    }

    return sum / numSamples;
}

void JoystickMgr::init() {
    // CRITICAL: Enable internal pull-up resistor.
    // On unmodified (stock) hardware, this prevents the ADC pin from floating
    // when KEY1 is released, ensuring a clean return to 4095 (JOY_NONE).
    pinMode(JOY_ADC_PIN, INPUT_PULLUP);

    analogSetPinAttenuation(JOY_ADC_PIN, ADC_11db);
    analogReadResolution(12);
    Serial.println("JoystickMgr: ADC1 initialized safely on JOY_ADC_PIN with internal pull-up");

    // Try to load saved calibration, otherwise retain hardcoded defaults
    if (!loadCalibration()) {
        Serial.println("JoystickMgr: No calibration file found, using defaults");
    }
}