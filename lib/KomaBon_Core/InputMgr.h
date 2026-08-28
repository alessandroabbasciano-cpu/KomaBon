#pragma once
#include <Arduino.h>
#include <OneButton.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "Config.h"

enum InputAction {
    INPUT_NONE,
    INPUT_NEXT,
    INPUT_PREV,
    INPUT_SELECT,
    INPUT_BACK,
    INPUT_GO_TO_MAIN_MENU,
    INPUT_SLEEP,
    // Manual full display refresh (KEY2 short click). Appended rather than
    // inserted: these values are printed raw in the input logs, so reordering
    // would silently change the meaning of older traces.
    INPUT_REFRESH
};

class InputMgr {
  public:
    static InputMgr& getInstance();

    void init();
    void update();

    // Allow apps to register a callback function
    using InputCallback = std::function<void(InputAction)>;
    void setCallback(InputCallback cb) {
        callback = cb;
    }
    void clearCallback() {
        callback = nullptr;
    }

  private:
    InputMgr();
    OneButton btn;
    OneButton btnBack;
    OneButton btnSleep;
    InputCallback callback;
    TaskHandle_t _taskHandle = nullptr;
    bool _taskRunning = false;

    // KEY1 manual tracking
    unsigned long _btnBackPressTime = 0;
    bool _btnBackLongPressSent = false;

    // v1.9.1 diagnostics: last raw snapshot of the three button pins, so the
    // polling task only logs on an edge instead of every 5ms tick.
    // bit0 = KEY1/PIN_BUTTON_BACK, bit1 = KEY2/PIN_BUTTON_SLEEP,
    // bit2 = KEY3/PIN_BUTTON. 0xFF = nothing sampled yet.
    uint8_t _lastPinSnapshot = 0xFF;

    // KEY2 manual tracking (manual polling, same pattern as KEY1, so both
    // buttons behave identically rather than mixing OneButton callbacks in)
    unsigned long _btnSleepPressTime = 0;
    bool _btnSleepLongPressSent = false;

    // KEY2 press refused by the standby guard (see StandbyGuard.h).
    // Held until KEY2 is released, preventing the same LOW from being
    // re-evaluated cycle after cycle until it happens to pass.
    bool _btnSleepAborted = false;

    // Minimum interval between idle timer resets triggered by raw pin sampling
    // (the cycle runs every 5ms).
    static const unsigned long IDLE_RESET_THROTTLE_MS = 250;
    unsigned long _lastIdleResetTime = 0;

    static const uint8_t QUEUE_SIZE = 8;
    volatile uint8_t _queueHead = 0;
    volatile uint8_t _queueTail = 0;
    InputAction _queue[QUEUE_SIZE];
    portMUX_TYPE _queueMux = portMUX_INITIALIZER_UNLOCKED;

    void enqueueAction(InputAction action);
    bool dequeueAction(InputAction& action);
    static void inputTask(void* parameter);

    void onClick();
    void onDoubleClick();
    void onLongPress();
    void onBackLongPress();

    // Handled inside InputMgr rather than dispatched to the active app, so
    // standby works everywhere including modal screens.
    //
    // Called only from update(), i.e., the main loop: writes the sleep message
    // to the e-ink screen and calls stop() on the active app. The display does
    // not have a single owner — doing this in the input task would cause it to
    // draw at the same time as AppMgr::draw().
    void enterStandby();

    // Standby request coming from the input task (long KEY2), consumed by the
    // main loop. It is a flag rather than a queued action because it cannot
    // be lost: the queue drops actions when full, and a power command that
    // the user has to repeat is worse than one arriving a cycle later.
    volatile bool _standbyRequested = false;

    static void staticClick(void* ptr);
    static void staticDoubleClick(void* ptr);
    static void staticLongPress(void* ptr);
    static void staticBackLongPress(void* ptr);
};