#ifndef SD_MGR_H
#define SD_MGR_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

class SDMgr {
  public:
    static SDMgr& getInstance() {
        static SDMgr instance;
        return instance;
    }

    bool init();

    // Reinitializes the SPI bus and SD card without a hard reset.
    // Useful for recovering from EMI-induced state machine lockups.
    bool recover();
    bool ensureReady();
    // Force a complete SPI bus reset and remount cycle initiated by the user
    bool remountManual();

    bool isMounted() const {
        return _mounted;
    }

  private:
    SDMgr();
    SPIClass* _spi;
    bool _mounted;
};

#endif // SD_MGR_H