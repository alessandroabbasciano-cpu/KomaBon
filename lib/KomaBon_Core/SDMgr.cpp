#include "SDMgr.h"
#include "Config.h"
#include "KomaBonFS.h"

SDMgr::SDMgr() : _spi(nullptr), _mounted(false) {}

bool SDMgr::init() {
    delay(100);

    // Hardware directive: Ensure internal pull-up on MISO (GPIO8) to prevent EMI noise
    pinMode(SD_MISO_PIN, INPUT_PULLUP);

    // Hardware Hardening: Force CS high to prevent crosstalk from E-ink EPD_RST (GPIO38)
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);

    if (!_spi) {
        _spi = new SPIClass(HSPI);
        // Initialize SPI3 (HSPI) on dedicated modding pins defined in Config.h
        _spi->begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    }

    bool mountSuccess = false;

    // Use the safe 4 MHz operating frequency defined in Config.h.
    for (int attempt = 1; attempt <= 3; attempt++) {
        // Re-assert pull-ups because ESP-IDF SD.begin() resets GPIO matrix
        pinMode(SD_MISO_PIN, INPUT_PULLUP);

        if (SD.begin(SD_CS_PIN, *_spi, SD_FAST_FREQ, "/ebooks", 10)) {
            mountSuccess = true;
            break;
        }
        Serial.printf("SDMgr: Mount attempt %d failed, retrying...\n", attempt);

        // Lock CS high during retry delay to ignore E-ink noise
        pinMode(SD_CS_PIN, OUTPUT);
        digitalWrite(SD_CS_PIN, HIGH);
        delay(350);
    }

    if (!mountSuccess) {
        // Keep CS High permanently to defend against floating pin crosstalk
        pinMode(SD_CS_PIN, OUTPUT);
        digitalWrite(SD_CS_PIN, HIGH);

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