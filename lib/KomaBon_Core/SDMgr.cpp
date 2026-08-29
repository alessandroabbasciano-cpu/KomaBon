#include "SDMgr.h"
#include "../../include/Config.h"
#include "KomaBonFS.h" // NEW: Required to access the global EbookFSPtr

SDMgr::SDMgr() : _spi(nullptr), _mounted(false) {}

bool SDMgr::init() {
    // 1. STRICT HARDWARE SAFETY: Force CS high immediately to set SD to High-Z.
    // This prevents the SD card from driving the MISO line during boot and
    // causing bus collisions.
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);

    // 2. STRICT HARDWARE SAFETY: Enable internal pull-up on MISO.
    // Prevents the pin from floating and capturing EMI noise when the SD card
    // is deselected or not inserted.
    pinMode(SD_MISO_PIN, INPUT_PULLUP);

    // 3. Initialize custom SPI bus (FSPI corresponds to SPI2 on ESP32-S3)
    // Avoids touching SPI0/SPI1 which are strictly reserved for PSRAM.
    _spi = new SPIClass(FSPI);
    _spi->begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    // 4. Mount SD Card at the custom "/ebooks" VFS mount point instead of default "/sd"
    // This forces low-level C libraries (like unzipLIB) to transparently hit 
    // the external SD card instead of the internal LittleFS partition.
    if (!SD.begin(SD_CS_PIN, *_spi, SD_FAST_FREQ, "/ebooks")) {
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

    // 5. Redirect the global Arduino filesystem abstraction pointer
    // From now on, any C++ call to EbookFS.open() will route directly to the SD.
    EbookFSPtr = &SD;

    _mounted = true;
    return true;
}