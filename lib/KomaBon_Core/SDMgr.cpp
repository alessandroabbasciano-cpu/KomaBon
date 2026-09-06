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

    // 4. Allocate and start dedicated SPI bus
    // CRITICAL FIX: Pass -1 for the CS pin. The SD library handles CS via software.
    // Passing SD_CS_PIN here causes a severe hardware vs software collision.
    if (!_spi) {
        _spi = new SPIClass(HSPI);
        _spi->begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, -1);
    }

    // 5. SD Power-Up Handshake Sequence:
    // Send at least 74 clock cycles (10 bytes) of 0xFF with CS HIGH to wake the card
    for (int i = 0; i < 10; i++) {
        _spi->transfer(0xFF);
    }

    // 6. Mount SD Card at /ebooks using the stable clock (8MHz from Config.h)
    if (!SD.begin(SD_CS_PIN, *_spi, SD_FAST_FREQ, "/ebooks")) {
        Serial.println("SDMgr: Mount failed or no SD card present.");
        WebMgr::getInstance().sendLog("SDMgr: Mount failed or no SD card present.");
        _mounted = false;
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("SDMgr: No SD card attached.");
        WebMgr::getInstance().sendLog("SDMgr: No SD card attached.");
        _mounted = false;
        return false;
    }

    Serial.println("SDMgr: SD Card mounted successfully at /ebooks.");
    Serial.printf("SDMgr: SD Card Type: %d\n", cardType);
    Serial.printf("SDMgr: SD Card Size: %llu MB\n", SD.cardSize() / (1024 * 1024));

    WebMgr::getInstance().sendLog("SDMgr: SD Card mounted successfully at /ebooks.");
    WebMgr::getInstance().sendLogf("SDMgr: SD Card Type: %d\n", cardType);
    WebMgr::getInstance().sendLogf("SDMgr: SD Card Size: %llu MB\n", SD.cardSize() / (1024 * 1024));

    // 7. Redirect global filesystem abstraction pointer
    EbookFSPtr = &SD;

    _mounted = true;
    return true;
}