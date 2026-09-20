#include "KBReader.h"
#include "KomaBonFS.h"

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

    if (!EbookFS.exists(path)) return false;
    _file = EbookFS.open(path, "r");
    if (!_file) return false;

    char magic[5] = {0};
    _file.readBytes(magic, 4);
    if (strcmp(magic, "KMB1") != 0) {
        close();
        return false;
    }

    uint16_t version;
    _file.read((uint8_t*)&version, 2);
    if (version != 3) {
        close();
        return false;
    }

    _file.read((uint8_t*)&_width, 2);
    _file.read((uint8_t*)&_height, 2);
    _file.read((uint8_t*)&_pageCount, 2);

    uint32_t coverLen;
    _file.read((uint8_t*)&coverLen, 4);
    _coverLen = coverLen;

    _coverOffset = 16;
    _dataOffset = 16 + _coverLen;

    return true;
}

bool KBReader::getCover(uint8_t* buffer, size_t bufferSize) {
    if (_coverLen == 0 || bufferSize < _coverLen) return false;

    _file.seek(_coverOffset);
    size_t bytesRead = _file.read(buffer, _coverLen);

    return bytesRead == _coverLen;
}

bool KBReader::readPage(uint16_t index, uint8_t* buffer) {
    if (index >= _pageCount) return false;

    size_t bytesPerRow = (_width + 7) / 8;
    size_t bytesPerPage = bytesPerRow * _height;
    uint32_t pageOffset = _dataOffset + (index * bytesPerPage);

    _file.seek(pageOffset);
    size_t bytesRead = _file.read(buffer, bytesPerPage);

    if (bytesRead != bytesPerPage) {
        return false;
    }

    return true;
}