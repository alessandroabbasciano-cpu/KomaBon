#include "SDMgr.h"
#include "../../include/Config.h"
#include "KomaBonFS.h"
#include "WebMgr.h"

SDMgr::SDMgr() : _spi(nullptr), _mounted(false) {}

bool SDMgr::init() {
    // 1. Give voltage regulator time to settle after display initialization
    delay(100);

    // 2. Hardware Safety: configure CS high to deselect card during bus setup
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);

    // 3. Enable internal pull-up on MISO line to prevent floating noise
    pinMode(SD_MISO_PIN, INPUT_PULLUP);

    // 4. Allocate and start dedicated SPI bus (pass SD_CS_PIN to avoid pin warnings)
    if (!_spi) {
        _spi = new SPIClass(HSPI);
        _spi->begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    }

    // 5. Mount SD Card at /ebooks using the stable 8 MHz clock
    if (!SD.begin(SD_CS_PIN, *_spi, SD_FAST_FREQ, "/ebooks")) {
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

    // 6. Redirect global filesystem abstraction pointer
    EbookFSPtr = &SD;

    _mounted = true;
    return true;
}