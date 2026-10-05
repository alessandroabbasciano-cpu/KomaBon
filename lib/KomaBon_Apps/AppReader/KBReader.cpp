#include "KBReader.h"
#include "KomaBonFS.h"
#include "SDMgr.h"

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
    _path = "";
}

bool KBReader::open(const char* path) {
    close();

    if (!EbookFS.exists(path)) return false;
    _file = EbookFS.open(path, "r");
    if (!_file) return false;

    _path = String(path);

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

    if (!_file && _path.length() > 0) {
        SDMgr::getInstance().ensureReady();
        _file = EbookFS.open(_path.c_str(), "r");
    }

    if (!_file) return false;

    _file.seek(_coverOffset);
    size_t bytesRead = _file.read(buffer, _coverLen);

    if (bytesRead != _coverLen && _path.length() > 0) {
        _file.close();
        if (SDMgr::getInstance().ensureReady()) {
            _file = EbookFS.open(_path.c_str(), "r");
            if (_file) {
                _file.seek(_coverOffset);
                bytesRead = _file.read(buffer, _coverLen);
            }
        }
    }

    return bytesRead == _coverLen;
}

bool KBReader::readPage(uint16_t index, uint8_t* buffer) {
    if (index >= _pageCount) return false;

    size_t bytesPerRow = (_width + 7) / 8;
    size_t bytesPerPage = bytesPerRow * _height;
    uint32_t pageOffset = _dataOffset + (index * bytesPerPage);

    if (!_file && _path.length() > 0) {
        SDMgr::getInstance().ensureReady();
        _file = EbookFS.open(_path.c_str(), "r");
    }

    if (!_file) return false;

    _file.seek(pageOffset);
    size_t bytesRead = _file.read(buffer, bytesPerPage);

    if (bytesRead != bytesPerPage && _path.length() > 0) {
        _file.close();
        if (SDMgr::getInstance().ensureReady()) {
            _file = EbookFS.open(_path.c_str(), "r");
            if (_file) {
                _file.seek(pageOffset);
                bytesRead = _file.read(buffer, bytesPerPage);
            }
        }
    }

    return (bytesRead == bytesPerPage);
}