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

        // Force cleanup of any previous failed FatFS state before retrying
        SD.end();

        // Flush SD state machine with 200 dummy clocks (25 bytes of 0xFF) while CS is HIGH
        _spi->beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
        for (int i = 0; i < 25; i++) {
            _spi->transfer(0xFF);
        }
        _spi->endTransaction();

        pinMode(SD_MISO_PIN, INPUT_PULLUP);
        delay(20);

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
    // If card was never mounted at boot, do not attempt recovery to avoid VFS conflicts
    if (!_mounted) {
        return false;
    }

    Serial.println("SDMgr: Attempting runtime recovery of SD Card...");
    _mounted = false;
    SD.end();
    delay(50);
    return init();
}

bool SDMgr::ensureReady() {
    if (!_mounted) {
        return false;
    }

    // Quick hardware probe: check if card is still answering
    uint8_t type = SD.cardType();
    if (type == CARD_NONE) {
        Serial.println("SDMgr: Card connection lost, recovering...");
        return recover();
    }
    return true;
}

extern fs::LittleFSFS InternalEbookFS;

bool SDMgr::remountManual() {
    Serial.println("SDMgr: Manual hardware remount sequence triggered...");

    if (_mounted) {
        SD.end();
        _mounted = false;
    }

    // 1. Reset the ESP32-S3 SPI peripheral hardware registers
    if (_spi) {
        _spi->end();
        delay(20);
        _spi->begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, -1);
    }

    // 2. Enforce slew-rate limiting on flying leads to suppress switching ringing
    gpio_set_drive_capability((gpio_num_t)SD_SCK_PIN, GPIO_DRIVE_CAP_1);
    gpio_set_drive_capability((gpio_num_t)SD_MOSI_PIN, GPIO_DRIVE_CAP_1);
    gpio_set_drive_capability((gpio_num_t)SD_CS_PIN, GPIO_DRIVE_CAP_1);

    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    pinMode(SD_MISO_PIN, INPUT_PULLUP);

    // 3. Drain card FIFO: 320 dummy clock pulses (40 bytes of 0xFF) with CS de-asserted
    if (_spi) {
        _spi->beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
        for (int i = 0; i < 40; i++) {
            _spi->transfer(0xFF);
        }
        _spi->endTransaction();
    }

    delay(50);

    // 4. Negotiate card handshake
    bool success = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        digitalWrite(SD_CS_PIN, HIGH);
        delay(100);
        if (SD.begin(SD_CS_PIN, *_spi, 1000000, "/ebooks", 10)) {
            success = true;
            break;
        }
        Serial.printf("SDMgr: Manual attempt %d failed.\n", attempt);
        delay(200);
    }

    _mounted = success;

    if (success) {
        EbookFSPtr = &SD;
        Serial.println("SDMgr: Manual remount SUCCESS. EbookFS mapped to MicroSD.");
    } else {
        EbookFSPtr = &InternalEbookFS;
        Serial.println("SDMgr: Manual remount FAILED. EbookFS retained on Internal Flash.");
    }

    return success;
}