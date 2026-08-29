#include "JoystickMgr.h"
#include "Config.h" // Assumes JOY_ADC_PIN is defined here as 2

JoystickMgr::JoystickMgr() {
    // Default safe calibration windows (assuming 3.3V logic, 12-bit ADC).
    // These will be overridden later by the auto-calibration tool.
    _cal = {
        500,        // centerMax (0-500) -> Dead short to GND
        1000, 1500, // down window
        1700, 2200, // right window
        2400, 2900, // left window
        3100, 3600  // up window
    };
}

void JoystickMgr::init() {
    // STRICT HARDWARE SAFETY: Ensure the pin is explicitly configured
    // for analog input to prevent dead shorts if the center button is pressed.

    // Set 11dB attenuation for full 0-3.3V range mapping
    analogSetPinAttenuation(JOY_ADC_PIN, ADC_11db);

    // Set 12-bit resolution (0 to 4095)
    analogReadResolution(12);

    Serial.println("JoystickMgr: ADC1 initialized safely on JOY_ADC_PIN");
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
        delayMicroseconds(50); // Small delay allows the ADC to stabilize
    }

    return sum / numSamples;
}

JoyDirection JoystickMgr::getDirection() {
    int val = readAnalogAveraged();

    // Deadzone: Nothing pressed usually pulls the line near VCC (4095)
    if (val > 3800) return JOY_NONE;

    // Map the averaged ADC value to a logical direction
    if (val >= 0 && val <= _cal.centerMax) return JOY_CENTER;
    if (val >= _cal.downMin && val <= _cal.downMax) return JOY_DOWN;
    if (val >= _cal.rightMin && val <= _cal.rightMax) return JOY_RIGHT;
    if (val >= _cal.leftMin && val <= _cal.leftMax) return JOY_LEFT;
    if (val >= _cal.upMin && val <= _cal.upMax) return JOY_UP;

    // Fallback if the value falls in a noise gap between windows
    return JOY_NONE;
}