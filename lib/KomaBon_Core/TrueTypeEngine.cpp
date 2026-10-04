#include "TrueTypeEngine.h"
#include <vector>
#include <esp_heap_caps.h>
#include <SD.h>

TrueTypeEngine& TrueTypeEngine::getInstance() {
    static TrueTypeEngine instance;
    return instance;
}

TrueTypeEngine::TrueTypeEngine() {
    _cacheCount = 0;
    _fontLoaded = false;
    _currentData = nullptr;
    _currentSize = 0;
    _allocatedBuffer = nullptr;
    memset(&_fontInfo, 0, sizeof(_fontInfo));
    setFont(0);
}

TrueTypeEngine::~TrueTypeEngine() {
    freeCache();
    if (_allocatedBuffer) {
        free(_allocatedBuffer);
        _allocatedBuffer = nullptr;
    }
}

void TrueTypeEngine::freeCache() {
    for (int i = 0; i < _cacheCount; i++) {
        if (_cache[i].glyphs) free(_cache[i].glyphs);
        if (_cache[i].bitmaps) free(_cache[i].bitmaps);
        _cache[i].glyphs = nullptr;
        _cache[i].bitmaps = nullptr;
    }
    _cacheCount = 0;
}

bool TrueTypeEngine::setFont(int index) {
    if (index < 0 || index >= (int)EMBEDDED_TTFS_COUNT) {
        index = 0;
    }
    return setFontFromMemory(EMBEDDED_TTFS[index].data, EMBEDDED_TTFS[index].size, false);
}

bool TrueTypeEngine::setFontFromMemory(const uint8_t* ttfData, size_t size, bool takeOwnership) {
    if (_fontLoaded && _currentData == ttfData) {
        return true;
    }

    freeCache();
    if (_allocatedBuffer && _allocatedBuffer != ttfData) {
        free(_allocatedBuffer);
        _allocatedBuffer = nullptr;
    }

    if (takeOwnership) {
        _allocatedBuffer = const_cast<uint8_t*>(ttfData);
    }

    _currentData = ttfData;
    _currentSize = size;
    _fontLoaded = false;

    if (!ttfData || size == 0) {
        return false;
    }

    if (!stbtt_InitFont(&_fontInfo, ttfData, 0)) {
        Serial.println("TrueTypeEngine: stbtt_InitFont failed");
        return false;
    }

    _fontLoaded = true;
    return true;
}

bool TrueTypeEngine::setFontFromFile(const char* path) {
    if (!path || strlen(path) == 0) return false;
    File f = SD.open(path, FILE_READ);
    if (!f) {
        Serial.printf("TrueTypeEngine: Failed to open %s\n", path);
        return false;
    }

    size_t sz = f.size();
    if (sz == 0 || sz > 8 * 1024 * 1024) {
        f.close();
        return false;
    }

    // Allocate in PSRAM if available, or internal heap
    uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (uint8_t*)malloc(sz);
    }
    if (!buf) {
        f.close();
        Serial.println("TrueTypeEngine: Out of memory loading font file");
        return false;
    }

    size_t bytesRead = f.read(buf, sz);
    f.close();

    if (bytesRead != sz) {
        free(buf);
        return false;
    }

    return setFontFromMemory(buf, sz, true);
}

const GFXfont* TrueTypeEngine::getGFXFont(float ptSize, bool bold) {
    if (!_fontLoaded) {
        if (!setFont(0)) return nullptr;
    }

    // Check cache
    for (int i = 0; i < _cacheCount; i++) {
        if (_cache[i].fontSource == _currentData && abs(_cache[i].ptSize - ptSize) < 0.1f &&
            _cache[i].bold == bold) {
            return &_cache[i].gfxFont;
        }
    }

    // Build new
    return buildGFXFont(ptSize, bold);
}

const GFXfont* TrueTypeEngine::buildGFXFont(float ptSize, bool bold) {
    if (!_fontLoaded) return nullptr;

    // Display DPI for 800x480 7.5" panel (~130 DPI)
    // 1 pt = 1/72 inch -> pixels = pt * (130 / 72) ~= pt * 1.8f
    float pixelHeight = ptSize * 1.8f;
    float scale = stbtt_ScaleForPixelHeight(&_fontInfo, pixelHeight);

    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&_fontInfo, &ascent, &descent, &lineGap);

    int firstChar = 0x20;
    int lastChar = 0xFF;
    int numChars = lastChar - firstChar + 1;

    // Allocate glyphs in PSRAM or internal RAM
    GFXglyph* glyphs = (GFXglyph*)malloc((size_t)numChars * sizeof(GFXglyph));
    if (!glyphs) {
        Serial.println("TrueTypeEngine: Failed to allocate glyphs");
        return nullptr;
    }
    memset(glyphs, 0, (size_t)numChars * sizeof(GFXglyph));

    std::vector<uint8_t> bitmapBytes;
    bitmapBytes.reserve(numChars * 16);

    uint8_t bitBuffer = 0;
    int bitCount = 0;

    auto pushBit = [&](uint8_t bit) {
        bitBuffer = (bitBuffer << 1) | (bit & 1);
        bitCount++;
        if (bitCount == 8) {
            bitmapBytes.push_back(bitBuffer);
            bitBuffer = 0;
            bitCount = 0;
        }
    };

    auto flushBits = [&]() {
        if (bitCount > 0) {
            bitBuffer <<= (8 - bitCount);
            bitmapBytes.push_back(bitBuffer);
            bitBuffer = 0;
            bitCount = 0;
        }
    };

    for (int ch = firstChar; ch <= lastChar; ch++) {
        int glyphIndex = stbtt_FindGlyphIndex(&_fontInfo, ch);
        int gIdx = ch - firstChar;

        int advanceWidth = 0, leftSideBearing = 0;
        if (glyphIndex > 0) {
            stbtt_GetGlyphHMetrics(&_fontInfo, glyphIndex, &advanceWidth, &leftSideBearing);
        } else {
            int spaceIndex = stbtt_FindGlyphIndex(&_fontInfo, ' ');
            stbtt_GetGlyphHMetrics(&_fontInfo, spaceIndex, &advanceWidth, &leftSideBearing);
        }

        int xAdv = (int)(advanceWidth * scale + 0.5f);
        if (bold) xAdv += 1;

        if (glyphIndex == 0 || ch == ' ') {
            glyphs[gIdx].bitmapOffset = bitmapBytes.size();
            glyphs[gIdx].width = 0;
            glyphs[gIdx].height = 0;
            glyphs[gIdx].xAdvance = xAdv > 0 ? xAdv : (int)(ptSize * 0.5f);
            glyphs[gIdx].xOffset = 0;
            glyphs[gIdx].yOffset = 0;
            continue;
        }

        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* mono =
            stbtt_GetGlyphBitmap(&_fontInfo, scale, scale, glyphIndex, &w, &h, &xoff, &yoff);

        if (!mono || w == 0 || h == 0) {
            if (mono) stbtt_FreeBitmap(mono, nullptr);
            glyphs[gIdx].bitmapOffset = bitmapBytes.size();
            glyphs[gIdx].width = 0;
            glyphs[gIdx].height = 0;
            glyphs[gIdx].xAdvance = xAdv;
            glyphs[gIdx].xOffset = 0;
            glyphs[gIdx].yOffset = 0;
            continue;
        }

        int finalW = w;
        if (bold) finalW += 1;

        glyphs[gIdx].bitmapOffset = bitmapBytes.size();
        glyphs[gIdx].width = finalW;
        glyphs[gIdx].height = h;
        glyphs[gIdx].xAdvance = xAdv;
        glyphs[gIdx].xOffset = xoff;
        glyphs[gIdx].yOffset = yoff;

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < finalW; x++) {
                uint8_t pixel = 0;
                if (!bold) {
                    if (mono[y * w + x] > 64) pixel = 1;
                } else {
                    if ((x < w && mono[y * w + x] > 64) || (x > 0 && mono[y * w + x - 1] > 64)) {
                        pixel = 1;
                    }
                }
                pushBit(pixel);
            }
        }
        flushBits();

        stbtt_FreeBitmap(mono, nullptr);
    }

    uint8_t* finalBitmaps = (uint8_t*)malloc(bitmapBytes.size());
    if (!finalBitmaps) {
        free(glyphs);
        Serial.println("TrueTypeEngine: Failed to allocate bitmaps");
        return nullptr;
    }
    memcpy(finalBitmaps, bitmapBytes.data(), bitmapBytes.size());

    if (_cacheCount >= MAX_CACHED_FONTS) {
        free(_cache[0].glyphs);
        free(_cache[0].bitmaps);
        for (int i = 0; i < MAX_CACHED_FONTS - 1; i++) {
            _cache[i] = _cache[i + 1];
        }
        _cacheCount = MAX_CACHED_FONTS - 1;
    }

    int entry = _cacheCount++;
    _cache[entry].ptSize = ptSize;
    _cache[entry].bold = bold;
    _cache[entry].fontSource = _currentData;
    _cache[entry].glyphs = glyphs;
    _cache[entry].bitmaps = finalBitmaps;
    _cache[entry].bitmapSize = bitmapBytes.size();

    int lineAdvance = (int)((ascent - descent + lineGap) * scale + 0.5f);

    _cache[entry].gfxFont = {finalBitmaps, glyphs, (uint8_t)firstChar, (uint8_t)lastChar,
                             (uint8_t)lineAdvance};

    return &_cache[entry].gfxFont;
}
