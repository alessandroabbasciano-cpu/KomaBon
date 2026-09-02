#include "InputMgr.h"
#include "../../include/Config.h"
#include "BatteryMgr.h"
#include "AppMgr.h"
#include "ButtonPressLogic.h"
#include "StandbyGuard.h"
#include "JoystickMgr.h"

InputMgr::InputMgr()
    : btn(255, true, true), btnBack(PIN_BUTTON_BACK, true, true), // 255 disables OneButton su GPIO2
      btnSleep(PIN_BUTTON_SLEEP, true, true) {                    // Active Low, Pullup
    callback = nullptr;
}

InputMgr& InputMgr::getInstance() {
    static InputMgr instance;
    return instance;
}

void InputMgr::init() {
    JoystickMgr::getInstance().init(); // Start the ADC safely first
    // Configure timing FIRST - ULTRA SNAPPY SETTINGS.
    // KEY3 is the one button actually driven by OneButton::tick(), so these
    // settings take effect here. KEY1 and KEY2 are polled with digitalRead()
    // and get their timing from ButtonPressLogic.h instead.[cite: 61]
    btn.setDebounceMs(BUTTON_DEBOUNCE_MIN_MS);
    btn.setClickMs(100); // Very short click window - no waiting for double-click
    btn.setPressMs(BUTTON_LONG_PRESS_MS);

    // Attach static handlers that trampoline to member functions
    btn.attachClick(staticClick, this);
    btn.attachLongPressStart(staticLongPress, this);
    // Double-click disabled for faster response

    // KEY1 - dedicated Back button - Use long press for going to main menu.
    // No handlers attached and tick() is never called on it: the polling task
    // reads the pin directly, so these setters are inert. They are kept only so
    // the object is left in a consistent state if tick() is ever restored.
    // Real debounce for KEY1 lives in classifyButtonRelease().[cite: 61]
    btnBack.setDebounceMs(BUTTON_DEBOUNCE_MIN_MS);
    btnBack.setPressMs(BUTTON_LONG_PRESS_MS);

    // KEY2 - short click triggers a manual full display refresh; long press
    // used to enter standby (now off by default - see
    // BOOK32_KEY2_STANDBY_ENABLED in Config.h; only BatteryMgr's automatic
    // idle timeout sleeps the device). When re-enabled, standby needs
    // STANDBY_HOLD_MS held (see StandbyGuard.h), well beyond the ordinary
    // BUTTON_LONG_PRESS_MS, so a brush against the button - or against
    // KEY1/KEY3 - never drops the device into deep sleep mid-page. Polled
    // manually for the same reason as KEY1.[cite: 61]
    btnSleep.setDebounceMs(BUTTON_DEBOUNCE_MIN_MS);
    btnSleep.setPressMs(STANDBY_HOLD_MS);
    pinMode(PIN_BUTTON_SLEEP, INPUT_PULLUP);

    if (!_taskHandle) {
        BaseType_t result = xTaskCreatePinnedToCore(inputTask, "InputPoll", 3072, this, 2, &_taskHandle, 1);
        _taskRunning = (result == pdPASS);
        if (!_taskRunning) {
            Serial.println("Input task failed to start; falling back to loop polling");
            _taskHandle = nullptr;
        }
    }
}

void InputMgr::update() {
    // Standby requested by the input task. Handled first: if there are actions
    // queued, sleeping is better than flipping another page (two seconds
    // of refresh) before doing so.[cite: 61]
    if (_standbyRequested) {
        _standbyRequested = false;
        enterStandby(); // does not return: deep sleep
    }

    if (!_taskRunning) {
        // btn.tick(); // DISABLED: Prevent phantom input from pin 255
        //  Don't tick btnBack - using manual polling in inputTask
    }

    InputAction action = INPUT_NONE;
    while (dequeueAction(action)) {
        // A manual full refresh is a display concern, not an app command, so
        // it's consumed here instead of being dispatched. Every screen gets it
        // for free that way, including modals that would drop an unknown
        // action on the floor. forceRedraw() only sets the app's dirty flags;
        // the repaint itself happens in the next AppMgr::draw(), which keeps
        // the ~2s e-ink refresh off this code path.[cite: 61]
        if (action == INPUT_REFRESH) {
            Serial.println("INPUT: KEY2 Click -> FULL REFRESH");
            App* current = AppMgr::getInstance().getCurrentApp();
            if (current) current->forceRedraw();
            continue;
        }
        Serial.printf("InputMgr::update() - dispatching action %d to callback\n", action);
        if (callback) callback(action);
    }
}

void InputMgr::inputTask(void* parameter) {
    InputMgr* self = static_cast<InputMgr*>(parameter);

    // Joystick state variables persist in the task
    JoyDirection lastJoyDirection = JOY_NONE;
    unsigned long joyPressTime = 0;
    bool joyLongPressSent = false;

    while (true) {
        // self->btn.tick(); // DISABLED: Prevent phantom input from pin 255
        // btnBack.tick() removed - using manual polling instead

        // Raw state of the three buttons, read once per cycle and shared by
        // diagnostics and detection blocks below. KEY3 enters here because
        // the standby guard needs to know if it is pressed: it is read raw
        // and not through OneButton, which only reports pre-classified events.[cite: 61]
        bool key1Pressed = (digitalRead(PIN_BUTTON_BACK) == LOW); // Active low
        bool key2Pressed = (digitalRead(PIN_BUTTON_SLEEP) == LOW);

        JoyDirection currentJoyDir = JoystickMgr::getInstance().getDirection();
        bool key3Pressed =
            (currentJoyDir == JOY_CENTER); // Maintains compatibility with the underlying diagnostic
        bool joyActive = (currentJoyDir != JOY_NONE);
        unsigned long now = millis();

        // Keeps track if the user is currently pressing any physical control
        self->_isInteracting = (key1Pressed || key2Pressed || joyActive);

        // diagnostics (PINDIAG): raw snapshot of the three pins on each
        // transition. 1 = released (pull-up), 0 = pressed (active low).
        // Maintained after correction: shows if pressing KEY3 drags GPIO3 with
        // it, which is the hypothesis defended by the standby guard.[cite: 61]
        {
            uint8_t snapshot =
                (uint8_t)((key1Pressed ? 0 : 0x01) | (key2Pressed ? 0 : 0x02) | (key3Pressed ? 0 : 0x04));
            if (snapshot != self->_lastPinSnapshot) {
                self->_lastPinSnapshot = snapshot;
                Serial.printf("PINDIAG: KEY1/GPIO%d=%d  KEY2/GPIO%d=%d  KEY3/GPIO%d=%d\n", PIN_BUTTON_BACK,
                              (snapshot & 0x01) ? 1 : 0, PIN_BUTTON_SLEEP, (snapshot & 0x02) ? 1 : 0,
                              JOY_ADC_PIN, (snapshot & 0x04) ? 1 : 0);
            }
        }

        // Any button down is user activity. The inactivity timer reset used
        // to live only in pre-classified events, meaning a held KEY3
        // (or presses OneButton hadn't closed yet) didn't count as activity
        // and the timeout could fire during use — indistinguishable to the
        // reader from "KEY3 sent the reader to sleep".
        //
        // Limited to once per IDLE_RESET_THROTTLE_MS: resetIdleTimer() acquires
        // the BatteryMgr mutex, which ADC readings hold for tens of ms, and
        // risking blocking button sampling every 5ms is not worth it.[cite: 61]
        if ((key1Pressed || key2Pressed || joyActive) &&
            (self->_lastIdleResetTime == 0 || (now - self->_lastIdleResetTime) >= IDLE_RESET_THROTTLE_MS)) {
            self->_lastIdleResetTime = now;
            BatteryMgr::getInstance().resetIdleTimer();
        }

        if (joyActive) {
            if (joyPressTime == 0) {
                joyPressTime = now;
                joyLongPressSent = false;
                lastJoyDirection = currentJoyDir;
            } else if (!joyLongPressSent) {
                unsigned long heldTime = now - joyPressTime;
                if (heldTime < 15) {
                    lastJoyDirection = currentJoyDir;
                } else if (heldTime >= BUTTON_LONG_PRESS_MS) {
                    // Long press threshold reached. Check which direction is being held.
                    if (currentJoyDir == JOY_CENTER) {
                        Serial.println("INPUT: JOY Center / KEY1 Long Press -> GO TO MAIN MENU");
                        BatteryMgr::getInstance().resetIdleTimer();
                        self->enqueueAction(INPUT_GO_TO_MAIN_MENU);
                        joyLongPressSent = true;
                    } else if (currentJoyDir == JOY_LEFT) {
                        // Long press LEFT to go back/abort without reaching for KEY3
                        Serial.println("INPUT: JOY Left Long Press -> BACK");
                        BatteryMgr::getInstance().resetIdleTimer();
                        self->enqueueAction(INPUT_BACK);
                        joyLongPressSent = true;
                    }
                }
            }
        } else {
            // Joystick released
            if (joyPressTime != 0) {
                unsigned long pressDuration = now - joyPressTime;

                // If a long press was already sent, joyLongPressSent is true,
                // so we safely skip the short-press action here.
                if (pressDuration >= BUTTON_DEBOUNCE_MIN_MS && !joyLongPressSent) {
                    BatteryMgr::getInstance().resetIdleTimer();
                    // Logical mapping of directions for SHORT press.
                    switch (lastJoyDirection) {
                        case JOY_RIGHT:
                            self->enqueueAction(INPUT_NEXT);
                            break;
                        case JOY_LEFT:
                            self->enqueueAction(INPUT_PREV);
                            break;
                        case JOY_UP:
                            self->enqueueAction(INPUT_PREV); // Pan up / Scroll up
                            break;
                        case JOY_DOWN:
                            self->enqueueAction(INPUT_NEXT); // Pan down / Scroll down
                            break;
                        case JOY_CENTER:
                            self->enqueueAction(INPUT_SELECT); // KEY1 or Joy Center
                            break;
                        default:
                            break;
                    }
                }
                // Reset state machine
                joyPressTime = 0;
                joyLongPressSent = false;
                lastJoyDirection = JOY_NONE;
            }
        }

        // Manual KEY1 long press detection (PIN_BUTTON_BACK)
        bool btnPressed = key1Pressed;

        if (btnPressed) {
            // Button is pressed
            if (self->_btnBackPressTime == 0) {
                // Just pressed
                self->_btnBackPressTime = now;
                self->_btnBackLongPressSent = false;
                Serial.println("KEY1: Button pressed");
            } else if (!self->_btnBackLongPressSent &&
                       (now - self->_btnBackPressTime) >= BUTTON_LONG_PRESS_MS) {
                // Long press threshold
                Serial.println("INPUT: KEY1 Long Press -> GO TO MAIN MENU");
                BatteryMgr::getInstance().resetIdleTimer();
                self->enqueueAction(INPUT_GO_TO_MAIN_MENU);
                self->_btnBackLongPressSent = true;
            }
        } else {
            // Button released
            if (self->_btnBackPressTime != 0) {
                unsigned long pressDuration = now - self->_btnBackPressTime;
                Serial.printf("KEY1: Button released after %lu ms\n", pressDuration);

                // Shared classifier: rejects contact bounce below the debounce
                // floor, and releases where the long press already fired.
                // Without the floor a rebound sent a second INPUT_PREV and the
                // reader went back two pages on one press.[cite: 61]
                if (classifyButtonRelease(pressDuration, self->_btnBackLongPressSent) ==
                    BUTTON_RELEASE_CLICK) {
                    Serial.println("INPUT: KEY1 Click -> PREV");
                    BatteryMgr::getInstance().resetIdleTimer();
                    self->enqueueAction(INPUT_PREV);
                }

                self->_btnBackPressTime = 0;
                self->_btnBackLongPressSent = false;
            }
        }

        // Manual KEY2 detection (PIN_BUTTON_SLEEP): short click -> full
        // refresh, long press -> standby. Standby is consumed by InputMgr in
        // the main loop, rather than dispatched via callback, to work across
        // all apps and modal screens like the unsaved changes warning.[cite: 61]
        bool sleepPressed = key2Pressed;

        if (sleepPressed) {
            if (self->_btnSleepPressTime == 0) {
                self->_btnSleepPressTime = now;
                self->_btnSleepLongPressSent = false;
                self->_btnSleepAborted = false;
                Serial.println("KEY2: Button pressed");
            } else if (!self->_btnSleepLongPressSent && !self->_btnSleepAborted &&
                       (now - self->_btnSleepPressTime) >= STANDBY_HOLD_MS) {
#if BOOK32_KEY2_STANDBY_ENABLED
                // Standby guard: re-reads the three pins and only accepts the
                // request if KEY2 is genuinely pressed and no other button is down.
                // Without this, a LOW induced on GPIO3 by pressing KEY3 would
                // trigger standby (see StandbyGuard.h). The threshold is
                // STANDBY_HOLD_MS (well above BUTTON_LONG_PRESS_MS used by KEY1
                // and KEY3) on purpose: a common navigation long press cannot pass
                // as a standby request.[cite: 61]
                StandbyDecision decision = classifyStandbyRequest(digitalRead(PIN_BUTTON_SLEEP) == LOW,
                                                                  digitalRead(PIN_BUTTON_BACK) == LOW,
                                                                  joyActive, now - self->_btnSleepPressTime);

                if (decision == STANDBY_ALLOW) {
                    Serial.println("INPUT: KEY2 Long Press -> STANDBY requested");
                    self->_btnSleepLongPressSent = true;
                    // Mark only: the main loop puts the device to sleep in update().
                    // Drawing to the e-ink screen is forbidden here (see enterStandby).[cite: 61]
                    self->_standbyRequested = true;
                } else {
                    // Definitive refusal for this press: without this brake, the
                    // next cycle would test again and a single instant with the
                    // other buttons released would let a spurious standby through.
                    // The press only counts again after KEY2 is released.[cite: 61]
                    self->_btnSleepAborted = true;
                    Serial.printf("SLEEPDIAG: standby denied  reason=%s  held=%lums\n",
                                  standbyDecisionName(decision), now - self->_btnSleepPressTime);
                }
#else
                // Manual standby disabled (BOOK32_KEY2_STANDBY_ENABLED=0,
                // see Config.h): only BatteryMgr's automatic idle timeout
                // puts the device to sleep. Consumed as refused to avoid
                // re-testing every 5ms until KEY2 is released.[cite: 61]
                self->_btnSleepAborted = true;
#endif
            }
        } else {
            if (self->_btnSleepPressTime != 0) {
                unsigned long pressDuration = now - self->_btnSleepPressTime;

                // Same shared classifier as KEY1. The debounce floor matters
                // here because contact bounce would otherwise cost a ~2s full
                // refresh.
                //
                // The classifier's long press branch became reachable: standby
                // no longer runs here, so releasing the button reaches this line
                // before the main loop sleeps. Without it, releasing the button
                // after a long press would cost a ~2s full refresh.
                //
                // A press refused by the guard counts as consumed: if the
                // LOW on GPIO3 came from another button, it's not a refresh
                // request either, and a ~2s full refresh is too costly to risk
                // on noise.[cite: 61]
                if (classifyButtonRelease(pressDuration,
                                          self->_btnSleepLongPressSent || self->_btnSleepAborted) ==
                    BUTTON_RELEASE_CLICK) {
                    Serial.printf("KEY2: Button released after %lu ms -> REFRESH\n", pressDuration);
                    BatteryMgr::getInstance().resetIdleTimer();
                    self->enqueueAction(INPUT_REFRESH);
                }

                self->_btnSleepPressTime = 0;
                self->_btnSleepLongPressSent = false;
                self->_btnSleepAborted = false;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// Runs in the main loop (see update()), never in the input task.[cite: 61]
void InputMgr::enterStandby() {
    // v1.9.1 diagnostics: record which pins were actually held at the moment
    // standby was decided. If KEY2/GPIO3 reads 1 (released) here, the LOW that
    // triggered the long press was transient or came from another pin.[cite: 61]
    Serial.printf("SLEEPDIAG: path=KEY2_LONG_PRESS  KEY1/GPIO%d=%d  KEY2/GPIO%d=%d  JOY_ACTIVE=%d\n",
                  PIN_BUTTON_BACK, digitalRead(PIN_BUTTON_BACK), PIN_BUTTON_SLEEP,
                  digitalRead(PIN_BUTTON_SLEEP),
                  (JoystickMgr::getInstance().getDirection() != JOY_NONE ? 1 : 0));
    Serial.flush();

    // Give the active app a chance to persist state first. The reader already
    // saves progress on stop(); the settings menu would otherwise lose an
    // unsaved draft to the deep sleep reset.
    // stop() is the app's own save hook: the reader persists reading progress
    // and the settings menu flushes an unsaved draft. Calling it through the
    // base interface keeps InputMgr free of any app-specific dependency.[cite: 61]
    App* current = AppMgr::getInstance().getCurrentApp();
    if (current) current->stop();

    // Reuses the existing idle-sleep path: e-ink message, ext0 wake on KEY3,
    // then deep sleep. Wake still happens on KEY3 because ext0 supports a
    // single pin; adding KEY2 would require switching to ext1 with a pin mask.[cite: 61]
    BatteryMgr::getInstance().enterIdleSleep("key2_long_press");
}

void InputMgr::enqueueAction(InputAction action) {
    if (action == INPUT_NONE) return;

    portENTER_CRITICAL(&_queueMux);
    uint8_t nextHead = (_queueHead + 1) % QUEUE_SIZE;
    if (nextHead != _queueTail) {
        _queue[_queueHead] = action;
        _queueHead = nextHead;
    }
    portEXIT_CRITICAL(&_queueMux);
}

bool InputMgr::dequeueAction(InputAction& action) {
    bool hasAction = false;
    portENTER_CRITICAL(&_queueMux);
    if (_queueTail != _queueHead) {
        action = _queue[_queueTail];
        _queueTail = (_queueTail + 1) % QUEUE_SIZE;
        hasAction = true;
    }
    portEXIT_CRITICAL(&_queueMux);
    return hasAction;
}

// Trampolines
void InputMgr::staticClick(void* ptr) {
    if (ptr) static_cast<InputMgr*>(ptr)->onClick();
}
void InputMgr::staticDoubleClick(void* ptr) {
    if (ptr) static_cast<InputMgr*>(ptr)->onDoubleClick();
}
void InputMgr::staticLongPress(void* ptr) {
    if (ptr) static_cast<InputMgr*>(ptr)->onLongPress();
}

// Handlers -> Dispatch to App
void InputMgr::onClick() {
    Serial.println("INPUT: Click -> NEXT");
    BatteryMgr::getInstance().resetIdleTimer(); // Reset idle timer on user interaction
    enqueueAction(INPUT_NEXT);
}

void InputMgr::onDoubleClick() {
    // Disabled for faster single-click response
    Serial.println("INPUT: Double-Click -> PREV");
    BatteryMgr::getInstance().resetIdleTimer(); // Reset idle timer on user interaction
    enqueueAction(INPUT_PREV);
}

void InputMgr::onLongPress() {
    Serial.println("INPUT: Long Press -> SELECT");
    BatteryMgr::getInstance().resetIdleTimer(); // Reset idle timer on user interaction
    enqueueAction(INPUT_SELECT);
}