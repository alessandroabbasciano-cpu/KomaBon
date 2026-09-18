#include "KBReader.h"
#include "KomaBonFS.h" // Ensures correct mounting endpoint for the external/internal VFS

KBReader::KBReader() {
    _width = 0;
    _height = 0;
    _pageCount = 0;
    _coverLen = 0;
    _coverOffset = 0;
    _dataOffset = 0;
}

KBReader::~KBReader() {
    close();
}

void KBReader::close() {
    if (_file) _file.close();
}

bool KBReader::open(const char* path) {
    close();

    // Ensures routing to the dedicated ebook partition rather than system partition
    if (!EbookFS.exists(path)) return false;
    _file = EbookFS.open(path, "r");
    if (!_file) return false;

    // 1. Header Extraction and Signature Validation (16 Bytes Total)
    char magic[5] = {0};
    _file.readBytes(magic, 4);
    if (strcmp(magic, "KMB1") != 0) {
        Serial.println("KBReader: Invalid Magic Signature. File is corrupted or not a valid KMB.");
        close();
        return false;
    }

    uint16_t version;
    _file.read((uint8_t*)&version, 2);
    if (version != 3) {
        Serial.printf("KBReader: Unsupported Format Version: %d. Expected Version 3.\n", version);
        close();
        return false;
    }

    // Extract core physical dimensions
    _file.read((uint8_t*)&_width, 2);
    _file.read((uint8_t*)&_height, 2);
    _file.read((uint8_t*)&_pageCount, 2);

    // Extract cover allocation parameters (currently unused in JS output, reserved for future extensions)
    uint32_t coverLen;
    _file.read((uint8_t*)&coverLen, 4);
    _coverLen = coverLen;

    // 2. Deterministic Offset Calculation
    // The standard header consumes exactly 16 bytes.
    _coverOffset = 16;

    // Since the format is raw uncompressed, no index tables exist.
    // The graphic payload starts immediately after the reserved cover block.
    _dataOffset = 16 + _coverLen;

    Serial.printf("KBReader: Successfully mounted %d pages, Frame Buffer Resolution: %dx%d\n", _pageCount,
                  _width, _height);
    return true;
}

bool KBReader::getCover(uint8_t* buffer, size_t bufferSize) {
    // Failsafe for missing covers to prevent buffer overruns
    if (_coverLen == 0 || bufferSize < _coverLen) return false;

    _file.seek(_coverOffset);
    size_t bytesRead = _file.read(buffer, _coverLen);

    return bytesRead == _coverLen;
}

bool KBReader::readPage(uint16_t index, uint8_t* buffer) {
    // Out-of-bounds protection layer
    if (index >= _pageCount) return false;

    // 1. Memory Stride Calculation
    // Calculates the required byte width per row allowing for sub-byte padding on non-divisible resolutions
    size_t bytesPerRow = (_width + 7) / 8;

    // Calculates the total payload size for a single uncompressed monolithic frame
    size_t bytesPerPage = bytesPerRow * _height;

    // 2. Direct Address Jump
    // Bypasses offset tables and jumps directly to the physical memory location of the requested page
    uint32_t pageOffset = _dataOffset + (index * bytesPerPage);

    _file.seek(pageOffset);

    // 3. Direct Memory Access (DMA) from Flash to RAM
    // Pours the raw bits directly into the pre-allocated buffer supplied by AppReader
    size_t bytesRead = _file.read(buffer, bytesPerPage);

    if (bytesRead != bytesPerPage) {
        Serial.printf("KBReader: Critical read failure. Data truncation detected on physical page %d\n",
                      index);
        return false;
    }

    return true;
}