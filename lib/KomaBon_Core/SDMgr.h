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
    bool remount();

    bool isMounted() const {
        return _mounted;
    }

    uint32_t getClusterSize() const {
        return _clusterSize;
    }

    bool isClusterOptimal() const {
        return _clusterSize >= 32768 && _clusterAligned;
    }

    bool isClusterAligned() const {
        return _clusterAligned;
    }

  private:
    SDMgr();
    void checkFatClusterAlignment();

    SPIClass* _spi;
    bool _mounted;
    uint32_t _clusterSize;
    bool _clusterAligned;
};

#endif // SD_MGR_H