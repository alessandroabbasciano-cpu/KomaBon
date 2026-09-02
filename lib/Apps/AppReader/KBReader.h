#ifndef KB_READER_H
#define KB_READER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

// ROM-based decompression library for ESP32
// Essential for extremely fast, low-memory extraction of comic pages
#include <rom/miniz.h>

class KBReader {
  public:
    KBReader();
    ~KBReader();

    // Opens a .kmb comic file
    bool open(const char* path);
    void close();

    // Comic Metadata
    uint16_t getWidth() const {
        return _width;
    }
    uint16_t getHeight() const {
        return _height;
    }
    uint16_t getPageCount() const {
        return _pageCount;
    }

    // Cover Management
    bool hasCover() const {
        return _coverLen > 0;
    }
    bool getCover(uint8_t* buffer, size_t bufferSize);

    // Page Reading
    // Decompresses the specific comic page index into the provided buffer
    bool readPage(uint16_t index, uint8_t* buffer);

  private:
    File _file;

    uint16_t _width;
    uint16_t _height;
    uint16_t _pageCount;
    uint32_t _coverLen;
    uint32_t _coverOffset; // Start of cover data in file
    uint32_t _tableOffset; // Start of offset table

    // Helper for decompression
    bool decompressChunk(uint32_t offset, size_t compressedLen, uint8_t* outBuffer, size_t outLen);
};

#endif