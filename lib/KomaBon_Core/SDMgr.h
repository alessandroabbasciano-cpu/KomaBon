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

    bool isMounted() const {
        return _mounted;
    }

  private:
    SDMgr();
    SPIClass* _spi;
    bool _mounted;
};

#endif // SD_MGR_H