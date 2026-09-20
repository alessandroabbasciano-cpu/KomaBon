#ifndef KB_READER_H
#define KB_READER_H

#include <Arduino.h>
#include <LittleFS.h>

class KBReader {
  public:
    KBReader();
    ~KBReader();

    bool open(const char* path);
    void close();

    uint16_t getWidth() const {
        return _width;
    }
    uint16_t getHeight() const {
        return _height;
    }
    uint16_t getPageCount() const {
        return _pageCount;
    }
    uint32_t getCoverLength() const {
        return _coverLen;
    }

    bool hasCover() const {
        return _coverLen > 0;
    }
    bool getCover(uint8_t* buffer, size_t bufferSize);

    bool readPage(uint16_t index, uint8_t* buffer);

  private:
    File _file;

    uint16_t _width;
    uint16_t _height;
    uint16_t _pageCount;
    uint32_t _coverLen;

    uint32_t _coverOffset;
    uint32_t _dataOffset;
};

#endif