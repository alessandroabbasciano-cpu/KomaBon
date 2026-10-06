#include "AppReader.h"
#include "DisplayMgr.h"
#include "FontMgr.h"
#include "BatteryMgr.h"
#include "KomaBonFS.h"
#include "icon_reader.h"
#include "../KomaBon_Core/SettingsStore.h"
#include "../../KomaBon_Core/AppMgr.h"
#include "BookMeta.h"
#include "BookmarkStore.h"

const uint8_t* AppReader::getIconImage() {
    return icon_reader_160x160;
}

void AppReader::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    if (_state == VIEW_READING)
        drawReading();
    else if (_state == VIEW_OVERLAY_SETTINGS)
        drawOverlaySettings();
    else if (_state == VIEW_OVERLAY_TOC)
        drawOverlayTOC();
    else if (_state == VIEW_OVERLAY_GOTO)
        drawOverlayGoto();
    else if (_state == VIEW_OVERLAY_BOOKMARKS)
        drawOverlayBookmarks();
}

void AppReader::drawReading() {
    KomaBonGuard guard(_epubMutex);

    if (!_isComicMode && !_textRenderer) {
        AppMgr::getInstance().switchTo("Bookshelf");
        return;
    }
    if (_isComicMode && !_kbReader) {
        AppMgr::getInstance().switchTo("Bookshelf");
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

        // Right side: Page progress and percentage, e.g. "42 / 200 (21%)" or "Page 42"
        char rightText[48];
        if (_totalPages > 0) {
            int pct = (int)((((long)_globalPageNumber) * 100) / _totalPages);
            if (pct > 100) pct = 100;
            snprintf(rightText, sizeof(rightText), "%d / %d (%d%%)", _globalPageNumber, _totalPages, pct);
        } else {
            snprintf(rightText, sizeof(rightText), "Page %d", _globalPageNumber);
        }

        int marginX = 25;
        int cursorY = display.height() - 15;

        // Measure right text
        int16_t rx1, ry1;
        uint16_t rw, rh;
        display.getTextBounds(rightText, 0, 0, &rx1, &ry1, &rw, &rh);
        int rightX = display.width() - marginX - (int)rw;

        // Left side: Current chapter title (for EPUB and KMB manga)
        String titleStr;
        if (_currentChapterTitle.length() > 0) {
            titleStr = _currentChapterTitle;
        } else if (!_isComicMode) {
            titleStr = "Chapter " + String(_currentChapter + 1);
        }

        if (titleStr.length() > 0) {
            // Available width between left margin and right text with safety spacing
            int maxTitleWidth = rightX - marginX - 35;
            int16_t lx1, ly1;
            uint16_t lw, lh;
            display.getTextBounds(titleStr.c_str(), 0, 0, &lx1, &ly1, &lw, &lh);

            // If the title is wider than the available space, truncate gracefully with "..."
            if ((int)lw > maxTitleWidth && maxTitleWidth > 40) {
                while (titleStr.length() > 3) {
                    titleStr.remove(titleStr.length() - 1);
                    String testStr = titleStr + "...";
                    display.getTextBounds(testStr.c_str(), 0, 0, &lx1, &ly1, &lw, &lh);
                    if ((int)lw <= maxTitleWidth) {
                        titleStr = testStr;
                        break;
                    }
                }
            }

            // In comic mode, clear white box behind text
            if (_isComicMode) {
                display.fillRect(marginX - 4, cursorY - lh - 2, lw + 8, lh + 6, GxEPD_WHITE);
            }
            display.setCursor(marginX, cursorY);
            display.print(titleStr);
        }

        // Draw Right Text
        if (_isComicMode) {
            display.fillRect(rightX - 4, cursorY - rh - 2, rw + 8, rh + 6, GxEPD_WHITE);
        }
        display.setCursor(rightX, cursorY);
        display.print(rightText);

        BatteryMgr::getInstance().drawStatusBar(display, display.width() - 105, 10);
        drawBookmarkIndicator(display);

    } while (display.nextPage());
}

void AppReader::drawOverlaySettings() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    int ow = 320; // Slightly wider to accommodate font names
    int oh = 390; // Height for 9 items
    int ox = (display.width() - ow) / 2;
    int oy = (display.height() - oh) / 2;

    display.setPartialWindow(ox, oy, ow, oh);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawRect(ox, oy, ow, oh, GxEPD_BLACK);
        display.drawRect(ox + 2, oy + 2, ow - 4, oh - 4, GxEPD_BLACK);

        fontMgr.drawTextCentered(display, "Quick Settings", oy + 30, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        display.drawLine(ox + 20, oy + 46, ox + ow - 20, oy + 46, GxEPD_BLACK);

        // Dynamic Strings Generation
        char fontSizeStr[32];
        snprintf(fontSizeStr, sizeof(fontSizeStr), "Font Size: %d pt", _fontSizePt);

        const char* fontNames[] = {"Atkinson",     "Merriweather", "Literata",
                                   "Source Serif", "Gelasio",      "Open Sans"};
        char fontFamilyStr[40];
        // Safety bound check
        int safeFontIdx = (_fontFamily >= 0 && _fontFamily <= 5) ? _fontFamily : 0;
        snprintf(fontFamilyStr, sizeof(fontFamilyStr), "Font: %s", fontNames[safeFontIdx]);

        const char* marginNames[] = {"Narrow", "Medium", "Wide"};
        char marginStr[32];
        int safeMargin = (_margin >= 0 && _margin <= 2) ? _margin : 1;
        snprintf(marginStr, sizeof(marginStr), "Margins: %s", marginNames[safeMargin]);

        char justifyStr[32];
        snprintf(justifyStr, sizeof(justifyStr), "Justify: %s", _justifyText ? "On" : "Off");

        char rtlModeStr[32];
        snprintf(rtlModeStr, sizeof(rtlModeStr), "RTL Mode: %s", _isRTL ? "On" : "Off");

        const char* items[] = {fontSizeStr,     fontFamilyStr,  marginStr,       justifyStr,     rtlModeStr,
                               "Go to Page...", "Bookmarks...", "Force Refresh", "Close & Apply"};

        for (int i = 0; i < 9; i++) {
            int itemY = oy + 76 + (i * 32);
            fontMgr.drawTextCentered(display, items[i], itemY, FONT_SIZE_BODY, GxEPD_BLACK);
            if (i == _overlaySelectedIndex) {
                display.drawRect(ox + 20, itemY - 20, ow - 40, 28, GxEPD_BLACK);
                display.drawRect(ox + 21, itemY - 19, ow - 42, 26, GxEPD_BLACK);
            }
        }
    } while (display.nextPage());
}

void AppReader::drawOverlayGoto() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    int ow = 340;
    int oh = 220;
    int ox = (display.width() - ow) / 2;
    int oy = (display.height() - oh) / 2;

    display.setPartialWindow(ox, oy, ow, oh);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawRect(ox, oy, ow, oh, GxEPD_BLACK);
        display.drawRect(ox + 2, oy + 2, ow - 4, oh - 4, GxEPD_BLACK);

        fontMgr.drawTextCentered(display, "Go to Page", oy + 32, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        display.drawLine(ox + 20, oy + 46, ox + ow - 20, oy + 46, GxEPD_BLACK);

        char pageStr[48];
        if (_totalPages > 0) {
            int pct = (_gotoTargetPage * 100) / _totalPages;
            snprintf(pageStr, sizeof(pageStr), "Page %d of %d  (%d%%)", _gotoTargetPage, _totalPages, pct);
        } else {
            snprintf(pageStr, sizeof(pageStr), "Page %d", _gotoTargetPage);
        }
        fontMgr.drawTextCentered(display, pageStr, oy + 84, FONT_SIZE_BODY, GxEPD_BLACK);

        // Slider bar
        int barX = ox + 30;
        int barY = oy + 106;
        int barW = ow - 60; // 280
        int barH = 16;
        display.drawRect(barX, barY, barW, barH, GxEPD_BLACK);
        display.drawRect(barX + 1, barY + 1, barW - 2, barH - 2, GxEPD_BLACK);

        if (_totalPages > 1) {
            int fillW = ((_gotoTargetPage - 1) * (barW - 6)) / (_totalPages - 1);
            if (fillW < 0) fillW = 0;
            if (fillW > barW - 6) fillW = barW - 6;
            if (_gotoTargetPage == _totalPages) fillW = barW - 6;
            if (fillW > 0) {
                display.fillRect(barX + 3, barY + 3, fillW, barH - 6, GxEPD_BLACK);
            }
        }

        display.drawLine(ox + 20, oy + 144, ox + ow - 20, oy + 144, GxEPD_BLACK);

        fontMgr.drawTextCentered(display, "◀ ▶ +/-1      ▲▼ +/-10", oy + 168, FONT_SIZE_SMALL, GxEPD_BLACK);
        fontMgr.drawTextCentered(display, "● Jump        ◀ Back", oy + 194, FONT_SIZE_SMALL, GxEPD_BLACK);
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
                fontMgr.drawTextCentered(display, "▲", oy + 65, FONT_SIZE_SMALL, GxEPD_BLACK);
            }
            if (_overlayScrollOffset + itemsPerPage < totalChapters) {
                fontMgr.drawTextCentered(display, "▼", oy + oh - 15, FONT_SIZE_SMALL, GxEPD_BLACK);
            }
        }
    } while (display.nextPage());
}

void AppReader::drawOverlayBookmarks() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    int ow = 340;
    int oh = 350;
    int ox = (display.width() - ow) / 2;
    int oy = (display.height() - oh) / 2;

    display.setPartialWindow(ox, oy, ow, oh);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawRect(ox, oy, ow, oh, GxEPD_BLACK);
        display.drawRect(ox + 2, oy + 2, ow - 4, oh - 4, GxEPD_BLACK);

        fontMgr.drawTextCentered(display, "Bookmarks", oy + 32, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        display.drawLine(ox + 20, oy + 48, ox + ow - 20, oy + 48, GxEPD_BLACK);

        String key = getOriginalFilename(normalizedBookName(_currentBookPath));
        std::vector<BookmarkEntry> bookmarks = BookmarkStore::getInstance().getBookmarks(key);

        if (bookmarks.empty()) {
            fontMgr.drawTextCentered(display, "No bookmarks yet.", oy + 120, FONT_SIZE_BODY, GxEPD_BLACK);
            fontMgr.drawTextCentered(display, "Hold UP while reading", oy + 160, FONT_SIZE_SMALL,
                                     GxEPD_BLACK);
            fontMgr.drawTextCentered(display, "to save a bookmark.", oy + 185, FONT_SIZE_SMALL, GxEPD_BLACK);
        } else {
            int itemsPerPage = 6;
            for (int i = 0; i < itemsPerPage; i++) {
                int idx = _overlayScrollOffset + i;
                if (idx >= (int)bookmarks.size()) break;

                int itemY = oy + 86 + (i * 34);
                char buf[48];
                if (_totalPages > 0) {
                    int pct = (bookmarks[idx].page * 100) / _totalPages;
                    snprintf(buf, sizeof(buf), "Page %d  (%d%%)", bookmarks[idx].page, pct);
                } else {
                    snprintf(buf, sizeof(buf), "Page %d", bookmarks[idx].page);
                }

                if (idx == _overlaySelectedIndex) {
                    display.fillRect(ox + 20, itemY - 20, ow - 40, 28, GxEPD_BLACK);
                    fontMgr.drawTextCentered(display, buf, itemY, FONT_SIZE_BODY, GxEPD_WHITE);
                } else {
                    fontMgr.drawTextCentered(display, buf, itemY, FONT_SIZE_BODY, GxEPD_BLACK);
                }
            }

            if (_overlayScrollOffset > 0) {
                fontMgr.drawTextCentered(display, "▲", oy + 58, FONT_SIZE_SMALL, GxEPD_BLACK);
            }
            if (_overlayScrollOffset + itemsPerPage < (int)bookmarks.size()) {
                fontMgr.drawTextCentered(display, "▼", oy + oh - 52, FONT_SIZE_SMALL, GxEPD_BLACK);
            }
        }

        display.drawLine(ox + 20, oy + oh - 48, ox + ow - 20, oy + oh - 48, GxEPD_BLACK);
        fontMgr.drawTextCentered(display, "▲▼ Scroll      ● Jump", oy + oh - 30, FONT_SIZE_SMALL,
                                 GxEPD_BLACK);
        fontMgr.drawTextCentered(display, "◀ Delete       [◀] Close", oy + oh - 12, FONT_SIZE_SMALL,
                                 GxEPD_BLACK);
    } while (display.nextPage());
}

void AppReader::drawSleepCover() {
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    // Ensure full refresh waveform and disable fast A2 partial refresh
    // to cleanly discharge ink particles and prevent burn-in during deep sleep
    dispMgr.disableFastRefreshA2();
    display.setFullWindow();

    String fileName = normalizedBookName(_currentBookPath);
    int dot = fileName.lastIndexOf('.');
    String baseName = (dot > 0) ? fileName.substring(0, dot) : fileName;

    SleepSettings sleepSettings = SettingsStore::getInstance().loadSleep();
    int screenMode = sleepSettings.screenMode;

    bool drawnCustom = false;
    uint8_t* customBuf = nullptr;
    if (screenMode == SLEEP_SCREEN_CUSTOM) {
        customBuf = (uint8_t*)ps_malloc(48000);
        if (!customBuf) customBuf = (uint8_t*)malloc(48000);
        if (customBuf && BatteryMgr::getInstance().loadCustomScreensaver(customBuf, 48000)) {
            drawnCustom = true;
        }
    }

    if (_isComicMode && _kbReader && _comicPageBuffer && !drawnCustom && screenMode != SLEEP_SCREEN_MINIMAL) {
        if (!_kbReader->readPage(0, _comicPageBuffer)) {
            Serial.println("AppReader: Failed to read KMB cover page 0 from SD.");
        }
    }

    size_t epubCoverSize = 0;
    uint8_t* epubCoverData = nullptr;
    if (!_isComicMode && _epubLoader && !drawnCustom && screenMode != SLEEP_SCREEN_MINIMAL) {
        epubCoverData = _epubLoader->getCoverImageData(&epubCoverSize);
        if (!epubCoverData) {
            epubCoverData = _epubLoader->getRawZipData("cover_main.raw", &epubCoverSize);
        }
    }

    String coverPath = "/covers/" + baseName + ".cover";
    bool hasSmallCover = false;
    std::vector<uint8_t> smallCoverData;
    if (!drawnCustom && screenMode != SLEEP_SCREEN_MINIMAL && !_isComicMode && !epubCoverData) {
        if (SystemFS.exists(coverPath)) {
            File f = SystemFS.open(coverPath, "r");
            if (f) {
                smallCoverData.resize(2400);
                if (f.read(smallCoverData.data(), 2400) == 2400) {
                    hasSmallCover = true;
                }
                f.close();
            }
        }
    }

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        if (drawnCustom && customBuf) {
            display.drawBitmap(0, 0, customBuf, 480, 800, GxEPD_BLACK);
        } else if (screenMode == SLEEP_SCREEN_MINIMAL) {
            String title = baseName;
            title.replace('_', ' ');
            fontMgr.drawTextCenteredBold(display, "KomaBon", 360, FONT_SIZE_HEADER, GxEPD_BLACK);
            fontMgr.drawTextCentered(display, title.c_str(), 420, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        } else {
            // SLEEP_SCREEN_COVER (or fallback if custom wallpaper missing)
            bool coverRendered = false;

            if (_isComicMode && _kbReader && _comicPageBuffer) {
                display.drawBitmap(0, 0, _comicPageBuffer, _kbReader->getWidth(), _kbReader->getHeight(),
                                   GxEPD_BLACK);
                coverRendered = true;
            } else if (!_isComicMode && epubCoverData && epubCoverSize >= 4) {
                uint16_t imgW = epubCoverData[0] | (epubCoverData[1] << 8);
                uint16_t imgH = epubCoverData[2] | (epubCoverData[3] << 8);
                if (epubCoverSize == 4 + ((imgW + 7) / 8) * imgH && imgW <= 800 && imgH <= 1200) {
                    int drawX = (display.width() - imgW) / 2;
                    int drawY = (display.height() - imgH) / 2;
                    if (drawX < 0) drawX = 0;
                    if (drawY < 0) drawY = 0;
                    display.drawBitmap(drawX, drawY, epubCoverData + 4, imgW, imgH, GxEPD_BLACK);
                    coverRendered = true;
                }
            }

            if (!coverRendered) {
                if (hasSmallCover) {
                    const int coverW = 240;
                    const int coverH = 320;
                    const int coverX = (display.width() - coverW) / 2;
                    const int coverY = 160;
                    const uint8_t* data = smallCoverData.data();
                    for (int sy = 0; sy < 160; sy++) {
                        int rowOffset = sy * 15;
                        for (int sx = 0; sx < 120; sx++) {
                            uint8_t byteVal = data[rowOffset + (sx / 8)];
                            if (byteVal & (1 << (7 - (sx % 8)))) {
                                display.fillRect(coverX + sx * 2, coverY + sy * 2, 2, 2, GxEPD_BLACK);
                            }
                        }
                    }
                    display.drawRect(coverX - 1, coverY - 1, coverW + 2, coverH + 2, GxEPD_BLACK);
                } else {
                    String title = baseName;
                    title.replace('_', ' ');
                    fontMgr.drawTextCenteredBold(display, title.c_str(), 390, FONT_SIZE_HEADER, GxEPD_BLACK);
                }
            }
        }

        // Top Status Bar (Wi-Fi, SD, Battery % and icon)
        BatteryMgr::getInstance().drawStatusBar(display, display.width() - 105, 10);

        // Bottom compact reading progress badge in overlay
        char progStr[48];
        if (_totalPages > 0) {
            int pct = (int)(((float)_globalPageNumber / (float)_totalPages) * 100.0f + 0.5f);
            if (pct > 100) pct = 100;
            snprintf(progStr, sizeof(progStr), "%d%% \xB7 Pag. %d di %d", pct, _globalPageNumber,
                     _totalPages);
        } else {
            snprintf(progStr, sizeof(progStr), "Pag. %d", _globalPageNumber);
        }

        display.setFont(&FreeSans9pt8b);
        int16_t bx, by;
        uint16_t bw, bh;
        display.getTextBounds(progStr, 0, 0, &bx, &by, &bw, &bh);

        const int pillW = bw + 28;
        const int pillH = 28;
        const int pillX = (display.width() - pillW) / 2;
        const int pillY = display.height() - pillH - 22;

        display.fillRoundRect(pillX, pillY, pillW, pillH, 6, GxEPD_WHITE);
        display.drawRoundRect(pillX, pillY, pillW, pillH, 6, GxEPD_BLACK);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(pillX + 14, pillY + pillH - 8);
        display.print(progStr);

    } while (display.nextPage());

    if (epubCoverData) free(epubCoverData);
    if (customBuf) free(customBuf);
}
