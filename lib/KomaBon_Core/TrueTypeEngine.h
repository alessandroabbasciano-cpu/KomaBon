#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include "stb_truetype.h"
#include "TTFonts/TTFonts.h"

class TrueTypeEngine {
  public:
    static TrueTypeEngine& getInstance();

    bool setFont(int index);
    bool setFontFromMemory(const uint8_t* ttfData, size_t size, bool takeOwnership = false);
    bool setFontFromFile(const char* path);
    const GFXfont* getGFXFont(float ptSize, bool bold = false);

  private:
    TrueTypeEngine();
    ~TrueTypeEngine();

    stbtt_fontinfo _fontInfo;
    bool _fontLoaded;
    const uint8_t* _currentData;
    size_t _currentSize;
    uint8_t* _allocatedBuffer;

    struct CachedGFXFont {
        float ptSize;
        bool bold;
        const uint8_t* fontSource;
        GFXfont gfxFont;
        GFXglyph* glyphs;
        uint8_t* bitmaps;
        size_t bitmapSize;
    };

    static const int MAX_CACHED_FONTS = 16;
    CachedGFXFont* _cache[MAX_CACHED_FONTS];
    int _cacheCount;

    void freeCache();
    const GFXfont* buildGFXFont(float ptSize, bool bold);
};
