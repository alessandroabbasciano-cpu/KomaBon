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
        _spi->begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, -1);
    }

    bool mountSuccess = false;

    for (int attempt = 1; attempt <= 3; attempt++) {
        digitalWrite(SD_CS_PIN, HIGH);
        SD.end();
        delay(20);

        // SD Physical Specification Protocol: Supply 74 to 80 clock cycles with CS HIGH
        // to wake up the card's internal state machine before any commands.
        _spi->beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
        for (int i = 0; i < 10; i++) {
            _spi->transfer(0xFF); // 10 bytes * 8 bits = 80 clock cycles
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

    // 1. Detach hardware SPI to take manual control of the pins
    if (_spi) {
        _spi->end();
    }

    // 2. Force pins to safe idle state imitating MCU reset
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);

    pinMode(SD_MOSI_PIN, OUTPUT);
    digitalWrite(SD_MOSI_PIN, HIGH);

    pinMode(SD_SCK_PIN, OUTPUT);
    digitalWrite(SD_SCK_PIN, LOW);

    pinMode(SD_MISO_PIN, INPUT_PULLUP);

    delay(20);

    // 3. SD Physical Spec protocol: send >74 dummy clock cycles with CS and MOSI HIGH
    // This physically wakes the SD controller out of EMI-induced lockup states
    for (int i = 0; i < 200; i++) {
        digitalWrite(SD_SCK_PIN, HIGH);
        delayMicroseconds(5);
        digitalWrite(SD_SCK_PIN, LOW);
        delayMicroseconds(5);
    }

    delay(50);

    // 4. Re-attach the hardware SPI peripheral
    if (_spi) {
        _spi->begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, -1);
    }

    gpio_set_drive_capability((gpio_num_t)SD_SCK_PIN, GPIO_DRIVE_CAP_1);
    gpio_set_drive_capability((gpio_num_t)SD_MOSI_PIN, GPIO_DRIVE_CAP_1);
    gpio_set_drive_capability((gpio_num_t)SD_CS_PIN, GPIO_DRIVE_CAP_1);

    bool success = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        digitalWrite(SD_CS_PIN, HIGH);
        delay(100);
        if (SD.begin(SD_CS_PIN, *_spi, 1000000, "/ebooks", 10)) {
            success = true;
            break;
        }
        Serial.printf("SDMgr: Manual attempt %d failed.\n", attempt);
        delay(250);
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