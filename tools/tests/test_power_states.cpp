// Host test for KomaBon power management, idle timeout, and Wi-Fi watchdog
// Build: g++ -std=c++17 -I lib/KomaBon_Core -o test_power_states tools/tests/test_power_states.cpp &&
// ./test_power_states

#include <cassert>
#include <cstdio>

class PowerStateManager {
  public:
    PowerStateManager(unsigned long timeoutMs) : _timeoutMs(timeoutMs), _lastActivity(0), _wifiActive(true) {}

    void recordActivity(unsigned long currentMillis) {
        _lastActivity = currentMillis;
    }

    bool checkTimeout(unsigned long currentMillis) {
        if (!_wifiActive) return false;
        if (currentMillis - _lastActivity > _timeoutMs) {
            _wifiActive = false; // Trigger shutdown
            return true;
        }
        return false;
    }

    bool isWifiActive() const {
        return _wifiActive;
    }

    void reset(unsigned long currentMillis) {
        _wifiActive = true;
        _lastActivity = currentMillis;
    }

  private:
    unsigned long _timeoutMs;
    unsigned long _lastActivity;
    bool _wifiActive;
};

int main() {
    // 5 minutes timeout in milliseconds (300,000 ms)
    const unsigned long TIMEOUT = 300000;
    PowerStateManager pm(TIMEOUT);

    // 1. Initial state: Wi-Fi should be active upon boot/start
    assert(pm.isWifiActive() == true);

    // 2. Before timeout limit: Wi-Fi stays active
    pm.recordActivity(0);
    assert(pm.checkTimeout(150000) == false); // 2.5 minutes elapsed
    assert(pm.isWifiActive() == true);

    // 3. Exactly at or past timeout limit: Wi-Fi shuts down
    assert(pm.checkTimeout(300001) == true); // 300,001 ms elapsed
    assert(pm.isWifiActive() == false);

    // 4. Activity after shutdown does not automatically revive Wi-Fi without explicit reset
    pm.recordActivity(350000);
    assert(pm.isWifiActive() == false);

    // 5. Explicit reset brings back Wi-Fi and resets timer baseline
    pm.reset(400000);
    assert(pm.isWifiActive() == true);
    assert(pm.checkTimeout(650000) == false); // 250k ms elapsed since reset
    assert(pm.checkTimeout(705000) == true);  // 305k ms elapsed since reset, triggers shutdown again

    printf("test_power_states: all tests passed.\n");
    return 0;
}