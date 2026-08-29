#ifndef JOYSTICK_MGR_H
#define JOYSTICK_MGR_H

#include <Arduino.h>

// Enumeration for logical joystick directions
enum JoyDirection { JOY_NONE, JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_CENTER };

// Struct to hold calibration thresholds (min and max ADC values)
struct JoyCalibration {
    int centerMax;
    int downMin;
    int downMax;
    int rightMin;
    int rightMax;
    int leftMin;
    int leftMax;
    int upMin;
    int upMax;
};

class JoystickMgr {
  public:
    static JoystickMgr& getInstance() {
        static JoystickMgr instance;
        return instance;
    }

    void init();
    JoyDirection getDirection();
    void setCalibration(const JoyCalibration& cal);

    // EXPOSED: Allows the settings menu to read raw ADC voltages for calibration
    int readAnalogAveraged();

  private:
    JoystickMgr();
    JoyCalibration _cal;
};

#endif // JOYSTICK_MGR_H