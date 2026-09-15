#include "AppReader.h"
#include "DisplayMgr.h"
#include "FontMgr.h"
#include "BatteryMgr.h"
#include "icon_reader.h"
#include "Fonts/FreeSans.h"

const uint8_t* AppReader::getIconImage() {
    return icon_reader_160x160;
}

void AppReader::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    if (_state == VIEW_LIBRARY)
        drawLibrary();
    else if (_state == VIEW_READING)
        drawReading();
    else if (_state == VIEW_OVERLAY_SETTINGS)
        drawOverlaySettings();
    else if (_state == VIEW_OVERLAY_TOC)
        drawOverlayTOC();
}

void AppReader::drawReading() {
    Book32Guard guard(_epubMutex);

    if (!_isComicMode && !_textRenderer) {
        _state = VIEW_LIBRARY;
        _librarySelectionOnlyRedraw = false;
        drawLibrary();
        return;
    }
    if (_isComicMode && !_kbReader) {
        _state = VIEW_LIBRARY;
        _librarySelectionOnlyRedraw = false;
        drawLibrary();
        return;
    }

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();

    if (_readingFirstDraw || _pageTurnsSinceRefresh >= _refreshEveryNPages) {
        display.setFullWindow();
        _pageTurnsSinceRefresh = 0;
        _readingFirstDraw = false;
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
        _pageTurnsSinceRefresh++;
    }

    int currentPageNum = _pageHistory.size();

    if (_isComicMode && _kbReader && _comicPageBuffer) {
        if (!_kbReader->readPage(_globalPageNumber - 1, _comicPageBuffer)) {
            Serial.println("AppReader: Failed to read KMB page data from SD.");
        }
    }

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        if (_isComicMode) {
            if (_comicPageBuffer) {
                display.drawBitmap(0, 0, _comicPageBuffer, _kbReader->getWidth(), _kbReader->getHeight(),
                                   GxEPD_BLACK);
            }
        } else {
            _currentPageRender = _textRenderer->renderRichPageDynamic(
                display, _currentRichContent, _currentPagePointer.nodeIndex, _currentPagePointer.charOffset,
                currentPageNum, _globalPageNumber, true);
            _currentPageRenderValid = true;
        }

        display.setFont(&FreeSans9pt8b);
        display.setTextColor(GxEPD_BLACK);
        char footerText[40];
        if (_totalPages > 0) {
            snprintf(footerText, sizeof(footerText), "Page %d of %d", _globalPageNumber, _totalPages);
        } else {
            snprintf(footerText, sizeof(footerText), "Page %d", _globalPageNumber);
        }

        int16_t fx1, fy1;
        uint16_t fw, fh;
        display.getTextBounds(footerText, 0, 0, &fx1, &fy1, &fw, &fh);
        int cursorX = display.width() / 2 - (int)fw / 2;
        int cursorY = display.height() - 15;

        if (_isComicMode) {
            display.fillRect(cursorX - 2, cursorY - fh - 2, fw + 4, fh + 4, GxEPD_WHITE);
        }

        display.setCursor(cursorX, cursorY);
        display.print(footerText);

        BatteryMgr::getInstance().drawStatusBar(display, display.width() - 105, 10);

    } while (display.nextPage());
}

void AppReader::drawOverlaySettings() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    int ow = 320; // Slightly wider to accommodate font names
    int oh = 230; // Reduced height since we have fewer items
    int ox = (display.width() - ow) / 2;
    int oy = (display.height() - oh) / 2;

    display.setPartialWindow(ox, oy, ow, oh);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawRect(ox, oy, ow, oh, GxEPD_BLACK);
        display.drawRect(ox + 2, oy + 2, ow - 4, oh - 4, GxEPD_BLACK);

        fontMgr.drawTextCentered(display, "Quick Settings", oy + 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        display.drawLine(ox + 20, oy + 55, ox + ow - 20, oy + 55, GxEPD_BLACK);

        // Dynamic Strings Generation
        char fontSizeStr[32];
        snprintf(fontSizeStr, sizeof(fontSizeStr), "Font Size: %d pt", _fontSizePt);

        const char* fontNames[] = {"FreeSans",     "Merriweather", "Literata",
                                   "Source Serif", "Gelasio",      "Open Sans"};
        char fontFamilyStr[40];
        // Safety bound check
        int safeFontIdx = (_fontFamily >= 0 && _fontFamily <= 5) ? _fontFamily : 0;
        snprintf(fontFamilyStr, sizeof(fontFamilyStr), "Font: %s", fontNames[safeFontIdx]);

        const char* items[] = {fontSizeStr, fontFamilyStr, "Force Refresh", "Close & Apply"};

        for (int i = 0; i < 4; i++) {
            int itemY = oy + 95 + (i * 32);
            if (i == _overlaySelectedIndex) {
                display.fillRect(ox + 20, itemY - 20, ow - 40, 28, GxEPD_BLACK);
                fontMgr.drawTextCentered(display, items[i], itemY, FONT_SIZE_BODY, GxEPD_WHITE);
            } else {
                fontMgr.drawTextCentered(display, items[i], itemY, FONT_SIZE_BODY, GxEPD_BLACK);
            }
        }
    } while (display.nextPage());
}

void AppReader::drawOverlayTOC() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    int ow = 320;
    int oh = 340;
    int ox = (display.width() - ow) / 2;
    int oy = (display.height() - oh) / 2;

    display.setPartialWindow(ox, oy, ow, oh);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawRect(ox, oy, ow, oh, GxEPD_BLACK);
        display.drawRect(ox + 2, oy + 2, ow - 4, oh - 4, GxEPD_BLACK);

        fontMgr.drawTextCentered(display, "Chapters", oy + 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        display.drawLine(ox + 20, oy + 55, ox + ow - 20, oy + 55, GxEPD_BLACK);

        int totalChapters = 0;
        {
            Book32Guard guard(_epubMutex);
            if (_epubLoader) totalChapters = _epubLoader->getChapterCount();
        }

        if (totalChapters == 0) {
            fontMgr.drawTextCentered(display, "No chapters found.", oy + 150, FONT_SIZE_BODY, GxEPD_BLACK);
        } else {
            int itemsPerPage = 7;
            for (int i = 0; i < itemsPerPage; i++) {
                int chapIdx = _overlayScrollOffset + i;
                if (chapIdx >= totalChapters) break;

                int itemY = oy + 90 + (i * 32);
                char buf[32];
                snprintf(buf, sizeof(buf), "Chapter %d", chapIdx + 1);

                if (chapIdx == _overlaySelectedIndex) {
                    display.fillRect(ox + 20, itemY - 20, ow - 40, 28, GxEPD_BLACK);
                    fontMgr.drawTextCentered(display, buf, itemY, FONT_SIZE_BODY, GxEPD_WHITE);
                } else {
                    fontMgr.drawTextCentered(display, buf, itemY, FONT_SIZE_BODY, GxEPD_BLACK);
                }
            }

            if (_overlayScrollOffset > 0) {
                fontMgr.drawTextCentered(display, "^", oy + 65, FONT_SIZE_SMALL, GxEPD_BLACK);
            }
            if (_overlayScrollOffset + itemsPerPage < totalChapters) {
                fontMgr.drawTextCentered(display, "v", oy + oh - 15, FONT_SIZE_SMALL, GxEPD_BLACK);
            }
        }
    } while (display.nextPage());
}