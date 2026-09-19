#include "SDMgr.h"
#include "../../include/Config.h"
#include "KomaBonFS.h"

SDMgr::SDMgr() : _spi(nullptr), _mounted(false) {}

bool SDMgr::init() {
    delay(100);

    // Hardware directive: Ensure internal pull-up on MISO to prevent EMI noise
    pinMode(SD_MISO_PIN, INPUT_PULLUP);

    if (!_spi) {
        _spi = new SPIClass(FSPI);
        // Initialize SPI2 (FSPI) on dedicated modding pins
        _spi->begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    }

    bool mountSuccess = false;

    // Use explicit safe frequency (400 kHz for initial handshake)
    const uint32_t sdInitFreq = 400000;
    // Use 16 MHz for high-speed data transfer after successful mount
    const uint32_t sdFastFreq = 16000000;

    for (int attempt = 1; attempt <= 3; attempt++) {
        if (SD.begin(SD_CS_PIN, *_spi, sdInitFreq, "/ebooks", 10)) {
            mountSuccess = true;
            break;
        }
        delay(350);
    }

    if (!mountSuccess) {
        Serial.println("SDMgr: Mount failed or no SD card present.");
        _mounted = false;
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("SDMgr: No SD card attached.");
        _mounted = false;
        return false;
    }

    Serial.println("SDMgr: SD Card mounted successfully at /ebooks.");
    Serial.printf("SDMgr: SD Card Type: %d\n", cardType);
    Serial.printf("SDMgr: SD Card Size: %llu MB\n", SD.cardSize() / (1024 * 1024));

    // Bind filesystem pointer for global VFS compatibility
    EbookFSPtr = &SD;

    _mounted = true;
    return true;
}