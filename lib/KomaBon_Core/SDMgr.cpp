#include "SDMgr.h"
#include "../../include/Config.h"
#include "KomaBonFS.h"
#include "WebMgr.h"

SDMgr::SDMgr() : _spi(nullptr), _mounted(false) {}

bool SDMgr::init() {
    // 1. Give voltage regulator and capacitors time to settle after display refresh
    delay(350);

    // 2. Hardware Safety: configure CS high to deselect card during bus setup
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);

    // 3. Enable internal pull-up on MISO line to prevent floating noise
    pinMode(SD_MISO_PIN, INPUT_PULLUP);

    // 4. Allocate and start dedicated SPI bus (CS managed via software)
    if (!_spi) {
        _spi = new SPIClass(HSPI);
        _spi->begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, -1);
    }

    // 5. SD Power-Up Handshake Sequence:
    // Send 80 dummy clock cycles (10 bytes of 0xFF) with CS HIGH to wake the card
    for (int i = 0; i < 10; i++) {
        _spi->transfer(0xFF);
    }

    // 6. Mount SD Card with retry mechanism for reliable cold boots
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

    // 7. Redirect global filesystem abstraction pointer
    EbookFSPtr = &SD;

    _mounted = true;
    return true;
}