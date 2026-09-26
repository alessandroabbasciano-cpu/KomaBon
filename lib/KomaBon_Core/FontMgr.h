#ifndef FONT_MGR_H
#define FONT_MGR_H

#include <Arduino.h>
#include "DisplayMgr.h"

#include "Fonts/FreeSans.h"
#include "Fonts/Gelasio.h"
#include "Fonts/Literata.h"
#include "Fonts/Merriweather.h"
#include "Fonts/OpenSans.h"
#include "Fonts/SourceSerif4.h"

// Font size presets (in pixels) - mapped to GFX fonts
#define FONT_SIZE_SMALL 14
#define FONT_SIZE_BODY 18
#define FONT_SIZE_MENU 20
#define FONT_SIZE_SUBTITLE 24
#define FONT_SIZE_TITLE 28
#define FONT_SIZE_HEADER 36

enum class FontFamily { FreeSans, Gelasio, Literata, Merriweather, OpenSans, SourceSerif4 };

class FontMgr {
  public:
    static FontMgr& getInstance();

    // Initialize
    bool init();

    // Check if font system is ready (always true for GFX fonts)
    bool hasTTFFont() {
        return true;
    }

    // Dynamic Font Switching
    void setFontFamily(FontFamily family);
    FontFamily getFontFamily() const;

    // Draw text at position using Adafruit GFX fonts
    void drawText(KomaBonDisplay& display, const char* text, int x, int y, int fontSize,
                  uint16_t color = GxEPD_BLACK);
    void drawTextBold(KomaBonDisplay& display, const char* text, int x, int y, int fontSize,
                      uint16_t color = GxEPD_BLACK);

    // Draw text centered horizontally
    void drawTextCentered(KomaBonDisplay& display, const char* text, int y, int fontSize,
                          uint16_t color = GxEPD_BLACK);
    void drawTextCenteredBold(KomaBonDisplay& display, const char* text, int y, int fontSize,
                              uint16_t color = GxEPD_BLACK);

    // Draw text right-aligned
    void drawTextRight(KomaBonDisplay& display, const char* text, int x, int y, int fontSize,
                       uint16_t color = GxEPD_BLACK);
    void drawTextRightBold(KomaBonDisplay& display, const char* text, int x, int y, int fontSize,
                           uint16_t color = GxEPD_BLACK);

    // Get text width for layout calculations
    int getTextWidth(const char* text, int fontSize);
    int getTextWidthBold(const char* text, int fontSize);

    // Get text height
    int getTextHeight(int fontSize);

    // Get the GFX font for a given size and current family
    const GFXfont* getFont(int fontSize);
    const GFXfont* getFontBold(int fontSize);

    static void utf8ToLatin1(const char* src, char* dst, size_t dstSize);
    static String utf8ToLatin1(const String& src);

  private:
    FontMgr();
    ~FontMgr();

    uint8_t _charWidths[256];
    const GFXfont* _lastFont = nullptr;
    FontFamily _currentFamily = FontFamily::FreeSans;

    void cacheCharWidths(const GFXfont* font);
};

#endif