// Host test for KomaBon analog joystick ADC mapping (GPIO2 / ADC1_CH1)
// Build: g++ -std=c++17 -I lib/KomaBon_Core -o test_joystick_mapping tools/tests/test_joystick_mapping.cpp &&
// ./test_joystick_mapping

#include <cassert>
#include <cstdio>

// Mock mapping logic mirroring JoystickMgr thresholds for 12-bit ADC (0 - 4095)
enum InputEnum { INPUT_NONE = 0, INPUT_UP, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT, INPUT_SELECT };

InputEnum mapAdcToInput(int rawAdc) {
    // Deadzone / Neutral (e.g., around 4095 or unpressed pull-up state)
    if (rawAdc > 3800) return INPUT_NONE;

    // Thresholds mapping based on standard resistor ladder for 5-way joystick
    if (rawAdc < 300) return INPUT_SELECT;
    if (rawAdc < 1200) return INPUT_UP;
    if (rawAdc < 2200) return INPUT_DOWN;
    if (rawAdc < 3000) return INPUT_LEFT;
    if (rawAdc <= 3800) return INPUT_RIGHT;

    return INPUT_NONE;
}

int main() {
    // 1. Idle / Neutral state (No button pressed, pulled high or open)
    assert(mapAdcToInput(4095) == INPUT_NONE);
    assert(mapAdcToInput(3900) == INPUT_NONE);

    // 2. Select button range (lowest voltage drop)
    assert(mapAdcToInput(0) == INPUT_SELECT);
    assert(mapAdcToInput(150) == INPUT_SELECT);

    // 3. Up direction range
    assert(mapAdcToInput(500) == INPUT_UP);
    assert(mapAdcToInput(1000) == INPUT_UP);

    // 4. Down direction range
    assert(mapAdcToInput(1500) == INPUT_DOWN);
    assert(mapAdcToInput(2000) == INPUT_DOWN);

    // 5. Left direction range
    assert(mapAdcToInput(2500) == INPUT_LEFT);
    assert(mapAdcToInput(2900) == INPUT_LEFT);

    // 6. Right direction range
    assert(mapAdcToInput(3100) == INPUT_RIGHT);
    assert(mapAdcToInput(3700) == INPUT_RIGHT);

    printf("test_joystick_mapping: all tests passed.\n");
    return 0;
}