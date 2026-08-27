#pragma once
// Book32 — mutual exclusion for state shared between tasks.
//
// Rationale: state singletons (ProgressStore, SettingsStore, BatteryMgr,
// and BookMeta functions) are accessed simultaneously by two tasks — the main
// loop and the ESPAsyncWebServer task, which runs HTTP handlers in its own 
// context. Concrete examples that existed:
//
//   * GET /api/status measures the battery (turns on the measurement switch, 
//     takes 30 ADC readings, turns it off) while the main loop might be doing 
//     the exact same thing — one task would turn off the switch mid-reading 
//     for the other.
//   * DELETE /api/reader/progress clears the in-memory ProgressStore map 
//     while the reader is saving the page position. Two writers on the same 
//     std::map cause memory corruption, not just a lost write.
//   * POST /api/settings/reader performs a read-modify-write on the same 
//     file used by the on-device settings menu. LittleFS serializes each file 
//     operation, but not the entire sequence: the last write would overwrite 
//     the other's change.
//
// The mutex is recursive on purpose: several public methods of these classes 
// call each other (ProgressStore::get() -> begin() -> load()), and a simple 
// mutex would deadlock when taken a second time by the same task.
//
// Acquisition order: ProgressStore -> BookMeta is the only existing chain 
// (applyImportedJson reads metadata). No path does the reverse, so there is 
// no deadlock cycle. Maintain this rule when adding new locks.

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class Book32Mutex {
public:
    Book32Mutex() : _handle(xSemaphoreCreateRecursiveMutex()) {}

    // A null handle (no memory for the semaphore at startup) degrades to
    // "no lock" behavior instead of locking the system forever.
    void lock() {
        if (_handle) xSemaphoreTakeRecursive(_handle, portMAX_DELAY);
    }
    void unlock() {
        if (_handle) xSemaphoreGiveRecursive(_handle);
    }

private:
    SemaphoreHandle_t _handle;
};

class Book32Guard {
public:
    explicit Book32Guard(Book32Mutex& mutex) : _mutex(mutex) { _mutex.lock(); }
    ~Book32Guard() { _mutex.unlock(); }

    Book32Guard(const Book32Guard&) = delete;
    Book32Guard& operator=(const Book32Guard&) = delete;

private:
    Book32Mutex& _mutex;
};