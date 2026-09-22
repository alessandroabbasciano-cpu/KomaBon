#include "SDMgr.h"
#include "Config.h"
#include "KomaBonFS.h"
#include "driver/gpio.h"

SDMgr::SDMgr() : _spi(nullptr), _mounted(false) {}

bool SDMgr::init() {
    delay(100);

    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);

    pinMode(SD_SCK_PIN, OUTPUT);
    pinMode(SD_MOSI_PIN, OUTPUT);
    pinMode(SD_MISO_PIN, INPUT_PULLUP);

    // SIGNAL INTEGRITY (EMI Mitigation): Reduce slew rate on driven pins
    gpio_set_drive_capability((gpio_num_t)SD_SCK_PIN, GPIO_DRIVE_CAP_1);
    gpio_set_drive_capability((gpio_num_t)SD_MOSI_PIN, GPIO_DRIVE_CAP_1);
    gpio_set_drive_capability((gpio_num_t)SD_CS_PIN, GPIO_DRIVE_CAP_1);

    if (!_spi) {
        _spi = new SPIClass(HSPI);
        // Evitiamo che l'hardware SPI si impossessi del pin CS
        _spi->begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, -1);
    }

    bool mountSuccess = false;

    for (int attempt = 1; attempt <= 3; attempt++) {
        digitalWrite(SD_CS_PIN, HIGH);

        // SPI RECOVERY MANEUVER: Dummy clocks per sbloccare la state machine
        _spi->beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
        for (int i = 0; i < 10; i++) {
            _spi->transfer(0xFF);
        }
        _spi->endTransaction();

        pinMode(SD_MISO_PIN, INPUT_PULLUP);

        if (SD.begin(SD_CS_PIN, *_spi, 1000000, "/ebooks", 10)) {
            mountSuccess = true;
            break;
        }
        Serial.printf("SDMgr: Mount attempt %d failed, retrying...\n", attempt);

        digitalWrite(SD_CS_PIN, HIGH);
        delay(350);
    }

    if (!mountSuccess) {
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
    EbookFSPtr = &SD;
    _mounted = true;
    return true;
}

bool SDMgr::recover() {
    Serial.println("SDMgr: Attempting runtime recovery of SD Card...");
    if (_mounted) {
        SD.end();
        _mounted = false;
    }

    // Ensure SPI controller releases the lines
    if (_spi) {
        _spi->end();
        delete _spi;
        _spi = nullptr;
    }

    // Brief power-down delay
    delay(50);

    return init();
}