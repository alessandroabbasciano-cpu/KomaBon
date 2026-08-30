#ifndef JOYSTICK_MGR_H
#define JOYSTICK_MGR_H

#include <Arduino.h>

// Enumeration for logical joystick directions
enum JoyDirection { JOY_NONE, JOY_UP, JOY_DOWN, JOY_LEFT, JOY_RIGHT, JOY_CENTER };

// Struct to hold exact calibration targets
struct JoyCalibration {
    int center;
    int up;
    int down;
    int left;
    int right;
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

    // NEW: File I/O for persistent hardware tuning
    bool loadCalibration();
    bool saveCalibration(int center, int up, int down, int left, int right);

  private:
    JoystickMgr();
    JoyCalibration _cal;
};

#endif // JOYSTICK_MGR_H