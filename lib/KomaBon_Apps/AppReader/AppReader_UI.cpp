#include "AppReader.h"
#include "DisplayMgr.h"
#include "FontMgr.h"
#include "BatteryMgr.h"
#include "KomaBonFS.h"
#include "icon_reader.h"

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
    KomaBonGuard guard(_epubMutex);

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
        if (_readingFirstDraw && millis() < 8000) {
            display.setPartialWindow(0, 0, display.width(), display.height());
        } else {
            display.setFullWindow();
        }
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
            fontMgr.drawTextCentered(display, items[i], itemY, FONT_SIZE_BODY, GxEPD_BLACK);
            if (i == _overlaySelectedIndex) {
                display.drawRect(ox + 20, itemY - 20, ow - 40, 28, GxEPD_BLACK);
                display.drawRect(ox + 21, itemY - 19, ow - 42, 26, GxEPD_BLACK);
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
            KomaBonGuard guard(_epubMutex);
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

void AppReader::drawSleepCover() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    display.setFullWindow();

    String fileName = normalizedBookName(_currentBookPath);
    int dot = fileName.lastIndexOf('.');
    String baseName = (dot > 0) ? fileName.substring(0, dot) : fileName;
    String coverPath = "/covers/" + baseName + ".cover";

    std::vector<uint8_t> coverData;
    bool hasCover = false;
    if (SystemFS.exists(coverPath)) {
        File f = SystemFS.open(coverPath, "r");
        if (f) {
            coverData.resize(2400);
            if (f.read(coverData.data(), 2400) == 2400) {
                hasCover = true;
            }
            f.close();
        }
    }

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Status bar on top
        BatteryMgr::getInstance().drawStatusBar(display, display.width() - 105, 10);

        // Centered cover: 120x160 scaled 2x -> 240x320
        const int coverW = 240;
        const int coverH = 320;
        const int coverX = (display.width() - coverW) / 2;
        const int coverY = 110;

        if (hasCover) {
            const uint8_t* data = coverData.data();
            for (int sy = 0; sy < 160; sy++) {
                int rowOffset = sy * 15;
                for (int sx = 0; sx < 120; sx++) {
                    uint8_t byteVal = data[rowOffset + (sx / 8)];
                    if (byteVal & (1 << (7 - (sx % 8)))) {
                        display.fillRect(coverX + sx * 2, coverY + sy * 2, 2, 2, GxEPD_BLACK);
                    }
                }
            }

            // Elegant frame and drop shadow
            display.drawRect(coverX - 1, coverY - 1, coverW + 2, coverH + 2, GxEPD_BLACK);
            display.fillRect(coverX + 4, coverY + coverH + 1, coverW, 3, GxEPD_BLACK);
            display.fillRect(coverX + coverW + 1, coverY + 4, 3, coverH, GxEPD_BLACK);
        } else {
            display.drawRoundRect(coverX, coverY, coverW, coverH, 8, GxEPD_BLACK);
            display.drawRoundRect(coverX + 3, coverY + 3, coverW - 6, coverH - 6, 6, GxEPD_BLACK);
            display.fillRect(coverX + 16, coverY + 8, 8, coverH - 16, GxEPD_BLACK);
            fontMgr.drawTextCentered(display, "KomaBon", coverY + (coverH / 2), FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        }

        // Book title below cover
        String title = baseName;
        title.replace('_', ' ');
        if (fontMgr.getTextWidthBold(title.c_str(), FONT_SIZE_MENU) > display.width() - 40) {
            while (title.length() > 3 &&
                   fontMgr.getTextWidthBold((title + "...").c_str(), FONT_SIZE_MENU) > display.width() - 40) {
                title = title.substring(0, title.length() - 1);
            }
            title += "...";
        }
        fontMgr.drawTextCenteredBold(display, title.c_str(), 485, FONT_SIZE_MENU, GxEPD_BLACK);

        // Reading progress text
        char progStr[48];
        if (_totalPages > 0) {
            int pct = (int)(((float)_globalPageNumber / (float)_totalPages) * 100.0f + 0.5f);
            if (pct > 100) pct = 100;
            snprintf(progStr, sizeof(progStr), "Page %d of %d  (%d%%)", _globalPageNumber, _totalPages, pct);
        } else {
            snprintf(progStr, sizeof(progStr), "Page %d", _globalPageNumber);
        }
        fontMgr.drawTextCentered(display, progStr, 520, FONT_SIZE_BODY, GxEPD_BLACK);

        // Progress bar
        if (_totalPages > 0) {
            const int barW = 280;
            const int barH = 6;
            const int barX = (display.width() - barW) / 2;
            const int barY = 535;
            display.drawRect(barX, barY, barW, barH, GxEPD_BLACK);
            int fillW = (int)(((float)_globalPageNumber / (float)_totalPages) * (barW - 2));
            if (fillW > barW - 2) fillW = barW - 2;
            if (fillW > 0) {
                display.fillRect(barX + 1, barY + 1, fillW, barH - 2, GxEPD_BLACK);
            }
        }

        // Bottom pill badge: "Zzz Sleeping — Move joystick to wake"
        const int pillW = 360;
        const int pillH = 34;
        const int pillX = (display.width() - pillW) / 2;
        const int pillY = 720;
        display.fillRoundRect(pillX, pillY, pillW, pillH, 17, GxEPD_BLACK);
        fontMgr.drawTextCentered(display, "Zzz Sleeping - Move joystick to wake", pillY + 23, FONT_SIZE_BODY,
                                 GxEPD_WHITE);

    } while (display.nextPage());
}