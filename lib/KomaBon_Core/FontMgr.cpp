#include "FontMgr.h"

FontMgr::FontMgr() {
    memset(_charWidths, 0, sizeof(_charWidths));
}

FontMgr::~FontMgr() {}

FontMgr& FontMgr::getInstance() {
    static FontMgr instance;
    return instance;
}

bool FontMgr::init() {
    Serial.println("FontMgr: Initialized with multiple Adafruit GFX fonts");
    return true;
}

void FontMgr::setFontFamily(FontFamily family) {
    if (_currentFamily != family) {
        _currentFamily = family;
        _lastFont = nullptr; // Invalidate cache to force recalculation
    }
}

FontFamily FontMgr::getFontFamily() const {
    return _currentFamily;
}

const GFXfont* FontMgr::getFont(int fontSize) {
    switch (_currentFamily) {
        case FontFamily::Merriweather:
            if (fontSize >= 22) return &Merriweather_Regular18pt8b; // Fallback for 24pt
            if (fontSize >= 16) return &Merriweather_Regular12pt8b;
            return &Merriweather_Regular9pt8b;
        case FontFamily::Literata:
            if (fontSize >= 22) return &Literata_Regular18pt8b;
            if (fontSize >= 16) return &Literata_Regular12pt8b;
            return &Literata_Regular9pt8b;
        case FontFamily::Gelasio:
            if (fontSize >= 22) return &Gelasio_Regular18pt8b;
            if (fontSize >= 16) return &Gelasio_Regular12pt8b;
            return &Gelasio_Regular9pt8b;
        case FontFamily::OpenSans:
            if (fontSize >= 22) return &OpenSans_Regular18pt8b;
            if (fontSize >= 16) return &OpenSans_Regular12pt8b;
            return &OpenSans_Regular9pt8b;
        case FontFamily::SourceSerif4:
            if (fontSize >= 22) return &SourceSerif4_Regular18pt8b;
            if (fontSize >= 16) return &SourceSerif4_Regular12pt8b;
            return &SourceSerif4_Regular9pt8b;
        case FontFamily::FreeSans:
        default:
            if (fontSize >= 30) return &FreeSans24pt8b;
            if (fontSize >= 22) return &FreeSans18pt8b;
            if (fontSize >= 16) return &FreeSans12pt8b;
            return &FreeSans9pt8b;
    }
}

const GFXfont* FontMgr::getFontBold(int fontSize) {
    switch (_currentFamily) {
        case FontFamily::Merriweather:
            if (fontSize >= 30) return &Merriweather_Bold24pt8b;
            if (fontSize >= 22) return &Merriweather_Bold18pt8b;
            if (fontSize >= 16) return &Merriweather_Bold12pt8b;
            return &Merriweather_Bold9pt8b;
        case FontFamily::Literata:
            if (fontSize >= 30) return &Literata_Bold24pt8b;
            if (fontSize >= 22) return &Literata_Bold18pt8b;
            if (fontSize >= 16) return &Literata_Bold12pt8b;
            return &Literata_Bold9pt8b;
        case FontFamily::Gelasio:
            if (fontSize >= 30) return &Gelasio_Bold24pt8b;
            if (fontSize >= 22) return &Gelasio_Bold18pt8b;
            if (fontSize >= 16) return &Gelasio_Bold12pt8b;
            return &Gelasio_Bold9pt8b;
        case FontFamily::OpenSans:
            if (fontSize >= 30) return &OpenSans_Bold24pt8b;
            if (fontSize >= 22) return &OpenSans_Bold18pt8b;
            if (fontSize >= 16) return &OpenSans_Bold12pt8b;
            return &OpenSans_Bold9pt8b;
        case FontFamily::SourceSerif4:
            if (fontSize >= 30) return &SourceSerif4_Bold24pt8b;
            if (fontSize >= 22) return &SourceSerif4_Bold18pt8b;
            if (fontSize >= 16) return &SourceSerif4_Bold12pt8b;
            return &SourceSerif4_Bold9pt8b;
        case FontFamily::FreeSans:
        default:
            if (fontSize >= 30) return &FreeSansBold24pt8b;
            if (fontSize >= 22) return &FreeSansBold18pt8b;
            if (fontSize >= 16) return &FreeSansBold12pt8b;
            return &FreeSansBold9pt8b;
    }
}

void FontMgr::cacheCharWidths(const GFXfont* font) {
    if (font == _lastFont) return;
    _lastFont = font;

    for (int c = 32; c < 256; c++) {
        if (c >= font->first && c <= font->last) {
            _charWidths[c] = font->glyph[c - font->first].xAdvance;
        } else {
            _charWidths[c] = 0;
        }
    }
}

void FontMgr::drawText(KomaBonDisplay& display, const char* text, int x, int y, int fontSize,
                       uint16_t color) {
    char latin1[512];
    utf8ToLatin1(text, latin1, sizeof(latin1));
    const GFXfont* font = getFont(fontSize);
    display.setFont(font);
    display.setTextColor(color);
    display.setCursor(x, y);
    display.print(latin1);
}

void FontMgr::drawTextCentered(KomaBonDisplay& display, const char* text, int y, int fontSize,
                               uint16_t color) {
    int width = getTextWidth(text, fontSize);
    int x = (display.width() - width) / 2;
    drawText(display, text, x, y, fontSize, color);
}

void FontMgr::drawTextRight(KomaBonDisplay& display, const char* text, int x, int y, int fontSize,
                            uint16_t color) {
    int width = getTextWidth(text, fontSize);
    drawText(display, text, x - width, y, fontSize, color);
}

int FontMgr::getTextWidth(const char* text, int fontSize) {
    const GFXfont* font = getFont(fontSize);
    cacheCharWidths(font);

    char latin1[512];
    utf8ToLatin1(text, latin1, sizeof(latin1));

    int width = 0;
    for (const unsigned char* p = (const unsigned char*)latin1; *p; p++) {
        width += _charWidths[*p];
    }
    return width;
}

void FontMgr::utf8ToLatin1(const char* src, char* dst, size_t dstSize) {
    if (dstSize == 0) return;
    size_t o = 0;
    const uint8_t* s = (const uint8_t*)src;
    while (*s && o + 1 < dstSize) {
        uint32_t cp;
        uint8_t b = *s++;
        if (b < 0x80) {
            cp = b;
        } else if ((b & 0xE0) == 0xC0 && (s[0] & 0xC0) == 0x80) {
            cp = ((uint32_t)(b & 0x1F) << 6) | (s[0] & 0x3F);
            s += 1;
        } else if ((b & 0xF0) == 0xE0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) {
            cp = ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(s[0] & 0x3F) << 6) | (s[1] & 0x3F);
            s += 2;
        } else if ((b & 0xF8) == 0xF0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80 &&
                   (s[2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(b & 0x07) << 18) | ((uint32_t)(s[0] & 0x3F) << 12) |
                 ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            s += 3;
        } else {
            dst[o++] = (char)b;
            continue;
        }

        if (cp == 0x00A0) {
            dst[o++] = ' ';
        } else if (cp == 0x00AD) {
            continue;
        } else if (cp <= 0xFF) {
            dst[o++] = (char)cp;
        } else {
            switch (cp) {
                case 0x2018:
                case 0x2019:
                case 0x201A:
                    dst[o++] = '\'';
                    break;
                case 0x201C:
                case 0x201D:
                case 0x201E:
                    dst[o++] = '"';
                    break;
                case 0x2013:
                case 0x2014:
                    dst[o++] = '-';
                    break;
                case 0x2026:
                    dst[o++] = '.';
                    if (o + 1 < dstSize) dst[o++] = '.';
                    if (o + 1 < dstSize) dst[o++] = '.';
                    break;
                default:
                    dst[o++] = '?';
                    break;
            }
        }
    }
    dst[o] = '\0';
}

String FontMgr::utf8ToLatin1(const String& src) {
    size_t bufSize = src.length() + 1;
    char* buf = (char*)malloc(bufSize);
    if (!buf) return src;
    utf8ToLatin1(src.c_str(), buf, bufSize);
    String out(buf);
    free(buf);
    return out;
}

int FontMgr::getTextHeight(int fontSize) {
    const GFXfont* font = getFont(fontSize);
    return font->yAdvance;
}