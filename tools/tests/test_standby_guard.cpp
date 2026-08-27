// Host test for the standby guard: which button holds may enter standby.
// Build: g++ -std=c++17 -I ../../lib/KomaBon_Core -o test_standby_guard test_standby_guard.cpp && ./test_standby_guard
#include "StandbyGuard.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    // 1. Legitimate case: KEY2 alone, held beyond standby threshold.
    assert(classifyStandbyRequest(true, false, false, STANDBY_HOLD_MS) == STANDBY_ALLOW);
    assert(classifyStandbyRequest(true, false, false, 5000) == STANDBY_ALLOW);

    // 2. KEY3 pressed at the same time: reported symptom. A LOW on GPIO3
    //    accompanied by KEY3 down is coupling, not a standby command.
    assert(classifyStandbyRequest(true, false, true, 5000) == STANDBY_DENY_OTHER_KEY);

    // 3. Same for KEY1, and for both simultaneously.
    assert(classifyStandbyRequest(true, true, false, 5000) == STANDBY_DENY_OTHER_KEY);
    assert(classifyStandbyRequest(true, true, true, 5000) == STANDBY_DENY_OTHER_KEY);

    // 4. The LOW has already disappeared at the instant of decision: it was transient.
    assert(classifyStandbyRequest(false, false, false, 5000) == STANDBY_DENY_RELEASED);

    // 5. "Released" wins over "other button pressed": both deny, but the reason
    //    logged must identify what actually happened.
    assert(classifyStandbyRequest(false, false, true, 5000) == STANDBY_DENY_RELEASED);

    // 6. Has not reached standby threshold yet.
    assert(classifyStandbyRequest(true, false, false, STANDBY_HOLD_MS - 1) ==
           STANDBY_DENY_TOO_SHORT);
    assert(classifyStandbyRequest(true, false, false, 0) == STANDBY_DENY_TOO_SHORT);

    // 7. A short click of KEY2 must never put the device to sleep, even
    //    alone: standby is on long press on purpose, so a brush against the
    //    button doesn't drop the reader into deep sleep mid-page.
    assert(classifyStandbyRequest(true, false, false, BUTTON_DEBOUNCE_MIN_MS) ==
           STANDBY_DENY_TOO_SHORT);

    // 8. v1.10.2: a common long press of KEY1/KEY3 (BUTTON_LONG_PRESS_MS,
    //    400ms) alone is not enough for standby, which requires STANDBY_HOLD_MS.
    //    Before this version both thresholds were the same value, so a normal
    //    navigation gesture reached exactly the point where any brief noise on
    //    GPIO3 had margin to pass unnoticed.
    assert(STANDBY_HOLD_MS > BUTTON_LONG_PRESS_MS);
    assert(classifyStandbyRequest(true, false, false, BUTTON_LONG_PRESS_MS) ==
           STANDBY_DENY_TOO_SHORT);

    // 9. Decision names, which go to the diagnostic log.
    assert(strcmp(standbyDecisionName(STANDBY_ALLOW), "allow") == 0);
    assert(strcmp(standbyDecisionName(STANDBY_DENY_OTHER_KEY), "other_key_held") == 0);

    printf("All 9 tests passed.\n");
    return 0;
}