#include "InputMgr.h"
#include "../../include/Config.h"
#include "BatteryMgr.h"
#include "AppMgr.h"
#include "ButtonPressLogic.h"
#include "StandbyGuard.h"
#include "JoystickMgr.h"

InputMgr::InputMgr() : btn(), btnBack(PIN_BUTTON_BACK, true, true), btnSleep(PIN_BUTTON_SLEEP, true, true) {
    callback = nullptr;
}

InputMgr& InputMgr::getInstance() {
    static InputMgr instance;
    return instance;
}

void InputMgr::init() {
    JoystickMgr::getInstance().init();
    btn.setDebounceMs(BUTTON_DEBOUNCE_MIN_MS);
    btn.setClickMs(100);
    btn.setPressMs(BUTTON_LONG_PRESS_MS);

    btn.attachClick(staticClick, this);
    btn.attachLongPressStart(staticLongPress, this);

    btnBack.setDebounceMs(BUTTON_DEBOUNCE_MIN_MS);
    btnBack.setPressMs(BUTTON_LONG_PRESS_MS);

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
    if (_standbyRequested) {
        _standbyRequested = false;
        enterStandby();
    }

    InputAction action = INPUT_NONE;
    while (dequeueAction(action)) {
        if (action == INPUT_REFRESH) {
            Serial.println("INPUT: KEY2 Click -> FULL REFRESH");
            App* current = AppMgr::getInstance().getCurrentApp();
            if (current) current->forceRedraw();
            continue;
        }
        if (callback) callback(action);
    }
}

void InputMgr::inputTask(void* parameter) {
    InputMgr* self = static_cast<InputMgr*>(parameter);

    static const unsigned long JOY_COOLDOWN_MS = 80;
    JoyDirection lastJoyDirection = JOY_NONE;
    unsigned long joyPressTime = 0;
    bool joyLongPressSent = false;
    unsigned long joyCooldown = 0;

    while (true) {
        unsigned long now = millis();
        bool key1Pressed = (digitalRead(PIN_BUTTON_BACK) == LOW);
        bool key2Pressed = (digitalRead(PIN_BUTTON_SLEEP) == LOW);

        JoyDirection currentJoyDir = JoystickMgr::getInstance().getDirection();

        // SIGNAL INTEGRITY: Mask ADC transients caused by mechanical release and resistive ladder discharge
        if (now < joyCooldown) {
            currentJoyDir = JOY_NONE;
        }

        bool key3Pressed = (currentJoyDir == JOY_CENTER);
        bool joyActive = (currentJoyDir != JOY_NONE);

        self->_isInteracting = (key1Pressed || key2Pressed || joyActive);

#if KOMABON_PIN_DIAG
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
#endif

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

                if (heldTime < 30) {
                    // Lock the direction to prevent thumb rolling errors
                    lastJoyDirection = currentJoyDir;
                } else if (heldTime >= BUTTON_LONG_PRESS_MS) {
                    if (lastJoyDirection == JOY_CENTER) {
                        Serial.println("INPUT: JOY Center Long Press -> GO TO MAIN MENU");
                        BatteryMgr::getInstance().resetIdleTimer();
                        self->enqueueAction(INPUT_GO_TO_MAIN_MENU);
                        joyLongPressSent = true;
                    } else if (lastJoyDirection == JOY_LEFT) {
                        Serial.println("INPUT: JOY Left Long Press -> BACK");
                        BatteryMgr::getInstance().resetIdleTimer();
                        self->enqueueAction(INPUT_BACK);
                        joyLongPressSent = true;
                    }
                }
            }
        } else {
            if (joyPressTime != 0) {
                unsigned long pressDuration = now - joyPressTime;

                if (pressDuration >= BUTTON_DEBOUNCE_MIN_MS && !joyLongPressSent) {
                    BatteryMgr::getInstance().resetIdleTimer();
                    switch (lastJoyDirection) {
                        case JOY_UP:
                            self->enqueueAction(INPUT_PREV);
                            break;
                        case JOY_DOWN:
                            self->enqueueAction(INPUT_NEXT);
                            break;
                        case JOY_LEFT:
                            self->enqueueAction(INPUT_LEFT);
                            break;
                        case JOY_RIGHT:
                            self->enqueueAction(INPUT_RIGHT);
                            break;
                        case JOY_CENTER:
                            self->enqueueAction(INPUT_SELECT);
                            break;
                        default:
                            break;
                    }
                }

                joyPressTime = 0;
                joyLongPressSent = false;
                lastJoyDirection = JOY_NONE;

                // Engage deadzone to absorb voltage spikes as the switch opens
                joyCooldown = now + JOY_COOLDOWN_MS;
            }
        }

        bool btnPressed = key1Pressed;

        if (btnPressed) {
            if (self->_btnBackPressTime == 0) {
                self->_btnBackPressTime = now;
                self->_btnBackLongPressSent = false;
                Serial.println("KEY1: Button pressed");
            } else if (!self->_btnBackLongPressSent &&
                       (now - self->_btnBackPressTime) >= BUTTON_LONG_PRESS_MS) {
                Serial.println("INPUT: KEY1 Long Press -> GO TO MAIN MENU");
                BatteryMgr::getInstance().resetIdleTimer();
                self->enqueueAction(INPUT_GO_TO_MAIN_MENU);
                self->_btnBackLongPressSent = true;
            }
        } else {
            if (self->_btnBackPressTime != 0) {
                unsigned long pressDuration = now - self->_btnBackPressTime;

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

        bool sleepPressed = key2Pressed;

        if (sleepPressed) {
            if (self->_btnSleepPressTime == 0) {
                self->_btnSleepPressTime = now;
                self->_btnSleepLongPressSent = false;
                self->_btnSleepAborted = false;
                Serial.println("KEY2: Button pressed");
            } else if (!self->_btnSleepLongPressSent && !self->_btnSleepAborted &&
                       (now - self->_btnSleepPressTime) >= STANDBY_HOLD_MS) {
#if KOMABON_KEY2_STANDBY_ENABLED
                StandbyDecision decision = classifyStandbyRequest(digitalRead(PIN_BUTTON_SLEEP) == LOW,
                                                                  digitalRead(PIN_BUTTON_BACK) == LOW,
                                                                  joyActive, now - self->_btnSleepPressTime);

                if (decision == STANDBY_ALLOW) {
                    Serial.println("INPUT: KEY2 Long Press -> STANDBY requested");
                    self->_btnSleepLongPressSent = true;
                    self->_standbyRequested = true;
                } else {
                    self->_btnSleepAborted = true;
                    Serial.printf("SLEEPDIAG: standby denied  reason=%s  held=%lums\n",
                                  standbyDecisionName(decision), now - self->_btnSleepPressTime);
                }
#else
                self->_btnSleepAborted = true;
#endif
            }
        } else {
            if (self->_btnSleepPressTime != 0) {
                unsigned long pressDuration = now - self->_btnSleepPressTime;

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

void InputMgr::enterStandby() {
    Serial.printf("SLEEPDIAG: path=KEY2_LONG_PRESS  KEY1/GPIO%d=%d  KEY2/GPIO%d=%d  JOY_ACTIVE=%d\n",
                  PIN_BUTTON_BACK, digitalRead(PIN_BUTTON_BACK), PIN_BUTTON_SLEEP,
                  digitalRead(PIN_BUTTON_SLEEP),
                  (JoystickMgr::getInstance().getDirection() != JOY_NONE ? 1 : 0));
    Serial.flush();

    App* current = AppMgr::getInstance().getCurrentApp();
    if (current) current->stop();

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

void InputMgr::staticClick(void* ptr) {
    if (ptr) static_cast<InputMgr*>(ptr)->onClick();
}
void InputMgr::staticDoubleClick(void* ptr) {
    if (ptr) static_cast<InputMgr*>(ptr)->onDoubleClick();
}
void InputMgr::staticLongPress(void* ptr) {
    if (ptr) static_cast<InputMgr*>(ptr)->onLongPress();
}

void InputMgr::onClick() {
    Serial.println("INPUT: Click -> NEXT");
    BatteryMgr::getInstance().resetIdleTimer();
    enqueueAction(INPUT_NEXT);
}

void InputMgr::onDoubleClick() {
    Serial.println("INPUT: Double-Click -> PREV");
    BatteryMgr::getInstance().resetIdleTimer();
    enqueueAction(INPUT_PREV);
}

void InputMgr::onLongPress() {
    Serial.println("INPUT: Long Press -> SELECT");
    BatteryMgr::getInstance().resetIdleTimer();
    enqueueAction(INPUT_SELECT);
}