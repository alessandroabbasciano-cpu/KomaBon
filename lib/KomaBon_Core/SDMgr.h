#ifndef SD_MGR_H
#define SD_MGR_H

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

class SDMgr {
  public:
    // Singleton pattern for global access
    static SDMgr& getInstance() {
        static SDMgr instance;
        return instance;
    }

    // Mounts the SD card using the dedicated SPI2 bus
    bool init();

    // Returns true if the SD card is currently mounted and accessible
    bool isMounted() const {
        return _mounted;
    }

  private:
    SDMgr();
    SPIClass* _spi;
    bool _mounted;
};

#endif // SD_MGR_H