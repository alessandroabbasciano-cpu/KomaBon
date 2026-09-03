#include "KBReader.h"
#include "KomaBonFS.h"

KBReader::KBReader() {
    _width = 0;
    _height = 0;
    _pageCount = 0;
    _coverLen = 0;
}

KBReader::~KBReader() {
    close();
}

void KBReader::close() {
    if (_file) _file.close();
}

bool KBReader::open(const char* path) {
    close();

    if (!EbookFS.exists(path)) return false;
    _file = EbookFS.open(path, "r");
    if (!_file) return false;

    // 1. Header Verification (16 bytes)
    // We expect the custom KomaBon magic string "KMB1"
    char magic[5] = {0};
    _file.readBytes(magic, 4);
    if (strcmp(magic, "KMB1") != 0) {
        Serial.println("Invalid KomaBon Magic Signature");
        close();
        return false;
    }

    uint16_t version;
    _file.read((uint8_t*)&version, 2);
    if (version != 3) {
        Serial.printf("Unsupported KB Version: %d\n", version);
        close();
        return false;
    }

    _file.read((uint8_t*)&_width, 2);
    _file.read((uint8_t*)&_height, 2);
    _file.read((uint8_t*)&_pageCount, 2);

    uint32_t coverLen;
    _file.read((uint8_t*)&coverLen, 4);
    _coverLen = coverLen;

    // Calculated Offsets
    // Header is 4+2+2+2+2+4 = 16 bytes.
    // Cover Data starts right after the header.
    _coverOffset = 16;

    // Offset Table starts after Cover Data
    _tableOffset = 16 + _coverLen;

    Serial.printf("Comic Opened: %d pages, CoverLen: %d\n", _pageCount, _coverLen);
    return true;
}

bool KBReader::getCover(uint8_t* buffer, size_t bufferSize) {
    if (_coverLen == 0) return false;

    _file.seek(_coverOffset);

    // Allocate memory for the compressed cover data
    uint8_t* compBuf = (uint8_t*)malloc(_coverLen);
    if (!compBuf) return false;

    _file.read(compBuf, _coverLen);

    // Hardware accelerated decompression using ROM functions
    size_t outLen = bufferSize;
    size_t result =
        tinfl_decompress_mem_to_mem(buffer, outLen, compBuf, _coverLen, TINFL_FLAG_PARSE_ZLIB_HEADER);

    free(compBuf);

    if (result == (size_t)-1) {
        Serial.println("Cover Decompression Failed");
        return false;
    }

    return true;
}

bool KBReader::readPage(uint16_t index, uint8_t* buffer) {
    if (index >= _pageCount) return false;

    // Read page offset from file on-demand to save RAM
    _file.seek(_tableOffset + (index * 4));
    uint32_t startOffset;
    _file.read((uint8_t*)&startOffset, 4);

    uint32_t nextOffset;
    if (index == _pageCount - 1) {
        nextOffset = _file.size();
    } else {
        uint32_t nextOff;
        _file.read((uint8_t*)&nextOff, 4);
        nextOffset = nextOff;
    }

    size_t compLen = nextOffset - startOffset;
    if (compLen == 0) return false;

    _file.seek(startOffset);

    // Allocate compression buffer in PSRAM to handle large comic pages cleanly
    uint8_t* compBuf = (uint8_t*)ps_malloc(compLen);
    if (!compBuf) {
        Serial.println("OOM Reading Page (PSRAM)");
        return false;
    }

    _file.read(compBuf, compLen);

    // Expected output size = bytes per row * height
    size_t bytesPerRow = (_width + 7) / 8;
    size_t outLen = bytesPerRow * _height;

    // Use streaming decompression with heap-allocated state to avoid stack overflow
    tinfl_decompressor* decomp = (tinfl_decompressor*)ps_malloc(sizeof(tinfl_decompressor));
    if (!decomp) {
        free(compBuf);
        Serial.println("Failed to allocate decompressor state");
        return false;
    }

    tinfl_init(decomp);

    size_t in_bytes = compLen;
    size_t out_bytes = outLen;
    const uint8_t* pIn = compBuf;
    uint8_t* pOut = buffer;

    tinfl_status status =
        tinfl_decompress(decomp, pIn, &in_bytes, buffer, pOut, &out_bytes,
                         TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

    free(decomp);
    free(compBuf);

    if (status != TINFL_STATUS_DONE) {
        Serial.printf("Page %d Decompression Failed (status: %d)\n", index, status);
        return false;
    }

    return true;
}