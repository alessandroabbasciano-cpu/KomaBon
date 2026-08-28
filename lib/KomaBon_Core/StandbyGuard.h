#pragma once
#include "ButtonPressLogic.h"

// KomaBon v1.9.2 — who can send the device to standby.
//
// Manual standby is currently disabled by default
// (BOOK32_KEY2_STANDBY_ENABLED=0 in Config.h): only BatteryMgr's automatic
// idle timeout puts the device to sleep, and the code below compiles
// but stays out of InputMgr's path until the flag goes back to 1. Kept as-is
// for that case.
//
// Product rule (with manual standby enabled): only a long press of KEY2 enters standby manually.
// KEY1 and KEY3 have no path to sleep in the code, but the user saw
// KEY3 putting the reader to sleep. Diagnosis hypothesis A
// (docs/plans/2026-07-26-key1-key3-standby-diagnostics.md) is pin coupling:
// pressing KEY3/GPIO5 drags GPIO3 to LOW long enough for KEY2's long press
// detection to fire.
//
// The defense is to require that, at the instant of the decision, KEY2 remains
// pressed and no other button is down. A LOW on GPIO3 accompanied by another
// button pressed is noise, not a standby command: the user wanting
// standby presses KEY2 alone.
//
// v1.10.2: this defense only acts at the instant the threshold is reached. If
// standby is still reported firing with KEY3, it's either noise too brief
// for the other button to appear pressed in that single sample, or it's
// simply easy to cross unintentionally: KEY1/KEY3 reach the long press
// threshold (BUTTON_LONG_PRESS_MS, 400ms) with the same quick gesture used
// in any navigation. Standby, which is costly to revert (~2s of e-ink,
// the user literally has to press another button to wake the reader), now
// requires a much longer and more deliberate press — STANDBY_HOLD_MS — so
// that neither brief noise nor a common navigation long press hits it by accident.
//
// v1.10.3 (reverted in v1.10.4): hypothesis A (electrical coupling) was
// dismissed by real logs — a clean, sustained long press exclusively on GPIO3,
// with no other pin moving. It was incorrectly and hastily concluded that
// the KEY2/KEY3 wiring was swapped, and PIN_BUTTON was swapped with
// PIN_BUTTON_SLEEP in Config.h. That swap broke the already working KEY3:
// the very first log of this investigation (before any pin changes)
// already showed a genuine click on KEY3/GPIO5 producing INPUT_NEXT
// correctly. The long press on GPIO3 alone did not prove the page-turn button
// was connected to GPIO3 — it only proved that at that instant, GPIO3 was
// what was pressed, which is perfectly compatible with the user accidentally
// pressing the adjacent physical KEY2 trying to reach KEY3. See the "Reverted"
// section in docs/plans/2026-07-26-key1-key3-standby-diagnostics.md for the
// full history. Pin mapping returned to original in v1.10.4; this guard
// and STANDBY_HOLD_MS remain as defense against such accidental presses,
// which continues to be the most likely explanation of the symptom.
//
// Pure header, no Arduino dependencies — host-testable
// (tools/tests/test_standby_guard.cpp).

// Long press threshold required specifically for standby, well above
// the BUTTON_LONG_PRESS_MS used by KEY1/KEY3 for their own actions.
// Deliberately different: standby is a hard-to-revert action, the others are
// not. This alone reduces the chance of a common KEY1/KEY3 press (400ms)
// being mistaken for a standby request, and gives a larger margin for the
// guard above to detect another button down before the press finishes.
constexpr unsigned long STANDBY_HOLD_MS = 1500;

enum StandbyDecision {
    STANDBY_DENY_TOO_SHORT, // has not reached standby threshold yet
    STANDBY_DENY_RELEASED,  // the LOW disappeared: was transient
    STANDBY_DENY_OTHER_KEY, // another button is pressed -> coupling
    STANDBY_ALLOW           // genuine KEY2 long press
};

// key2Held/key1Held/key3Held: pin states re-read at the instant of decision
// (true = pressed, meaning the pin reads LOW as they are active-low).
// heldMs: time since the start of continuous LOW on KEY2.
inline StandbyDecision classifyStandbyRequest(bool key2Held, bool key1Held, bool key3Held,
                                              unsigned long heldMs) {
    if (!key2Held) return STANDBY_DENY_RELEASED;
    if (key1Held || key3Held) return STANDBY_DENY_OTHER_KEY;
    if (heldMs < STANDBY_HOLD_MS) return STANDBY_DENY_TOO_SHORT;
    return STANDBY_ALLOW;
}

inline const char* standbyDecisionName(StandbyDecision decision) {
    switch (decision) {
        case STANDBY_DENY_TOO_SHORT:
            return "too_short";
        case STANDBY_DENY_RELEASED:
            return "released";
        case STANDBY_DENY_OTHER_KEY:
            return "other_key_held";
        case STANDBY_ALLOW:
            return "allow";
    }
    return "unknown";
}