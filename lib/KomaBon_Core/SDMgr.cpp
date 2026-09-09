#include "SDMgr.h"
#include "../../include/Config.h"
#include "KomaBonFS.h"
#include "WebMgr.h"

SDMgr::SDMgr() : _spi(nullptr), _mounted(false) {}

bool SDMgr::init() {
    delay(350);

    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    pinMode(SD_MISO_PIN, INPUT_PULLUP);

    if (!_spi) {
        _spi = new SPIClass(HSPI);
        _spi->begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, -1);
    }

    // Wake up card sequence
    for (int i = 0; i < 10; i++) {
        _spi->transfer(0xFF);
    }

    bool mountSuccess = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (SD.begin(SD_CS_PIN, *_spi, SD_FAST_FREQ, "/ebooks", 10)) {
            mountSuccess = true;
            break;
        }
        delay(350);
    }

    if (!mountSuccess) {
        WebMgr::getInstance().sendLog("SDMgr: Mount failed or no SD card present.");
        _mounted = false;
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        WebMgr::getInstance().sendLog("SDMgr: No SD card attached.");
        _mounted = false;
        return false;
    }

    WebMgr::getInstance().sendLog("SDMgr: SD Card mounted successfully at /ebooks.");
    WebMgr::getInstance().sendLogf("SDMgr: SD Card Type: %d\n", cardType);
    WebMgr::getInstance().sendLogf("SDMgr: SD Card Size: %llu MB\n", SD.cardSize() / (1024 * 1024));

    // Bind filesystem pointer for global VFS compatibility
    EbookFSPtr = &SD;

    _mounted = true;
    return true;
}