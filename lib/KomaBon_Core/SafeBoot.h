#ifndef SAFE_BOOT_H
#define SAFE_BOOT_H

#include <Arduino.h>

class SafeBoot {
  public:
    // Samples physical pins during early setup. Returns true if recovery hold is detected.
    static bool checkRequested();

    // Enters emergency recovery mode: minimal SoftAP, rescue HTTP server in RAM, no app boot
    static void run();
};

#endif // SAFE_BOOT_H
