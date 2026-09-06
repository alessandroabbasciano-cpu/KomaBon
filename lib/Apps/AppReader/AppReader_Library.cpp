#include "AppReader.h"
#include "DisplayMgr.h"
#include "FontMgr.h"
#include "KomaBonFS.h"
#include "BookOrderLogic.h"
#include "BookMeta.h"
#include "ProgressStore.h"
#include "PageCountStore.h"
#include "Fonts/FreeSans.h"
#include "BatteryMgr.h"
#include "SDMgr.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include <WebMgr.h>

static int textWidthForFont(KomaBonDisplay& display, const char* text, const GFXfont* font) {
    int16_t x1, y1;
    uint16_t w, h;
    display.setFont(font);
    display.setTextSize(1);
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return w;
}

static void drawTextWithFont(KomaBonDisplay& display, const char* text, int x, int y, const GFXfont* font,
                             uint16_t color) {
    display.setFont(font);
    display.setTextColor(color);
    display.setTextSize(1);
    display.setCursor(x, y);
    display.print(text);
}

static String titleFromFilename(String name) {
    name = normalizedBookName(name);
    int dot = name.lastIndexOf('.');
    if (dot > 0) name = name.substring(0, dot);
    name.replace('_', ' ');
    name.trim();
    return name;
}

struct LibraryDirtyRect {
    int x;
    int y;
    int w;
    int h;
};

static LibraryDirtyRect libraryItemRect(int index, int scrollOffset, int screenW) {
    const int HEADER_H = 76;
    const int BACK_ITEM_HEIGHT = 48;
    const int ITEM_HEIGHT = 110;
    if (index < 0) {
        return {14, HEADER_H, screenW - 28, BACK_ITEM_HEIGHT + 4};
    }
    int visibleRow = index - scrollOffset;
    return {14, HEADER_H + BACK_ITEM_HEIGHT + (visibleRow * ITEM_HEIGHT), screenW - 28, ITEM_HEIGHT + 4};
}

static int libraryItemsPerPage(int screenHeight) {
    const int HEADER_H = 76;
    const int BACK_ITEM_HEIGHT = 48;
    const int ITEM_HEIGHT = 110;
    int y = HEADER_H + BACK_ITEM_HEIGHT;
    int count = 0;
    while (y <= screenHeight - 70) {
        count++;
        y += ITEM_HEIGHT;
    }
    return count;
}

static LibraryDirtyRect unionLibraryRect(LibraryDirtyRect a, LibraryDirtyRect b) {
    int x1 = min(a.x, b.x);
    int y1 = min(a.y, b.y);
    int x2 = max(a.x + a.w, b.x + b.w);
    int y2 = max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

void AppReader::scanBooks() {
    _books.clear();

    if (!SDMgr::getInstance().isMounted()) {
        WebMgr::getInstance().sendLog("AppReader: Cannot scan books, SD card not mounted.");
        return;
    }

    std::map<String, String> metadata;
    loadBookMetadata(metadata);

    if (!EbookFS.exists("/covers")) {
        EbookFS.mkdir("/covers");
        WebMgr::getInstance().sendLog("AppReader: Directory /covers created on EbookFS.");
    }

    File root = EbookFS.open("/");
    if (!root || !root.isDirectory()) return;
    File file = root.openNextFile();
    while (file) {
        String fileName = normalizedBookName(file.name());
        String fileNameLower = fileName;
        fileNameLower.toLowerCase();
        if (fileNameLower.endsWith(".epub") || fileNameLower.endsWith(".kmb")) {
            BookEntry entry;
            entry.path = "/" + fileName;
            auto meta = metadata.find(fileName);
            entry.originalName = (meta != metadata.end()) ? meta->second : fileName;
            entry.title = FontMgr::utf8ToLatin1(titleFromFilename(entry.originalName));

            int dot = fileName.lastIndexOf('.');
            entry.baseName = (dot > 0) ? fileName.substring(0, dot) : fileName;
            String thumbPath = "/covers/" + entry.baseName + ".thumb";
            entry.hasCoverThumb = EbookFS.exists(thumbPath);
            entry.coverAttempted = entry.hasCoverThumb;

            _books.push_back(entry);
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();

    {
        ProgressStore& store = ProgressStore::getInstance();
        std::vector<String> present;
        present.reserve(_books.size());
        for (const auto& b : _books)
            present.push_back(b.originalName);
        store.reconcile(present);

        for (auto& b : _books) {
            BookProgress p;
            if (store.get(b.originalName, p)) {
                b.hasProgress = true;
                b.globalPage = p.globalPage;
            }
            b.totalPages = PageCountStore::getInstance().get(b.originalName, _fontSizePt, _fontFamily);
        }
    }

    if (SystemFS.exists("/book_order.json")) {
        File of = SystemFS.open("/book_order.json", "r");
        if (of) {
            DynamicJsonDocument doc(4096);
            DeserializationError err = deserializeJson(doc, of);
            of.close();
            if (!err) {
                JsonArray arr = doc["order"].as<JsonArray>();
                if (!arr.isNull()) {
                    std::vector<String> order;
                    for (JsonVariant v : arr)
                        order.push_back(v.as<String>());
                    applyBookOrderT(order, _books, [](const BookEntry& e, const String& key) {
                        return e.path == "/" + key;
                    });
                }
            }
        }
    }
}

void AppReader::drawBookTile(KomaBonDisplay& display, const BookEntry& book, int x, int y, int w, int h,
                             bool selected, const uint8_t* thumbData) {
    if (thumbData) {
        display.drawBitmap(x, y, thumbData, 60, 80, GxEPD_BLACK);
    } else {
        // Fallback default vector book icon
        display.fillRect(x, y, w, h, GxEPD_WHITE);
        display.drawRoundRect(x, y, w, h, 5, GxEPD_BLACK);
        display.drawRoundRect(x + 3, y + 3, w - 6, h - 6, 3, GxEPD_BLACK);
        display.fillRect(x + 6, y + 6, 5, h - 12, GxEPD_BLACK);

        int pageX = x + 17;
        int pageY = y + 14;
        int pageW = w - 27;
        display.drawFastHLine(pageX, pageY, pageW, GxEPD_BLACK);
        display.drawFastHLine(pageX, pageY + 12, pageW - 7, GxEPD_BLACK);
        display.drawFastHLine(pageX, pageY + 24, pageW, GxEPD_BLACK);
        display.drawFastHLine(pageX, pageY + 36, pageW - 11, GxEPD_BLACK);
    }

    if (selected) {
        display.drawRect(x - 3, y - 3, w + 6, h + 6, GxEPD_BLACK);
        display.drawRect(x - 2, y - 2, w + 4, h + 4, GxEPD_BLACK);
    }
}

void AppReader::updateLibraryScroll() {
    if (_selectedBookIndex < 0) return;

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    int itemsPerPage = libraryItemsPerPage(display.height());
    if (itemsPerPage <= 0) return;

    if (_selectedBookIndex < _libraryScrollOffset) {
        _libraryScrollOffset = _selectedBookIndex;
        _librarySelectionOnlyRedraw = false;
    } else if (_selectedBookIndex >= _libraryScrollOffset + itemsPerPage) {
        _libraryScrollOffset = _selectedBookIndex - itemsPerPage + 1;
        _librarySelectionOnlyRedraw = false;
    }
}

void AppReader::drawLibrary() {
    if (!_booksScanned) {
        scanBooks();
        _booksScanned = true;
    }

    int maxOffset = max(0, (int)_books.size() - 1);
    if (_libraryScrollOffset > maxOffset) _libraryScrollOffset = 0;
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    const int HEADER_H = 76;
    const int BACK_ITEM_HEIGHT = 48;
    const int COVER_WIDTH = 60;
    const int COVER_HEIGHT = 80;
    const int ITEM_HEIGHT = 110;
    const int ITEM_PADDING = 24;

    if (_librarySelectionOnlyRedraw) {
        LibraryDirtyRect dirty =
            unionLibraryRect(libraryItemRect(_previousBookIndex, _libraryScrollOffset, display.width()),
                             libraryItemRect(_selectedBookIndex, _libraryScrollOffset, display.width()));
        LibraryDirtyRect footer = {18, display.height() - 48, display.width() - 36, 46};
        dirty = unionLibraryRect(dirty, footer);
        dirty.x = max(0, dirty.x);
        dirty.y = max(0, dirty.y);
        if (dirty.x + dirty.w > display.width()) dirty.w = display.width() - dirty.x;
        if (dirty.y + dirty.h > display.height()) dirty.h = display.height() - dirty.y;
        display.setPartialWindow(dirty.x, dirty.y, dirty.w, dirty.h);
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
    }
    _librarySelectionOnlyRedraw = false;

    // PRE-LOAD THUMBNAILS TO PROTECT SPI BUS DURING E-INK REFRESH
    std::map<int, std::vector<uint8_t>> thumbCache;
    int preLoadY = HEADER_H + BACK_ITEM_HEIGHT;
    for (size_t idx = (size_t)_libraryScrollOffset; idx < _books.size(); idx++) {
        if (preLoadY > display.height() - 70) break;
        if (_books[idx].hasCoverThumb) {
            String thumbPath = "/covers/" + _books[idx].baseName + ".thumb";
            File f = EbookFS.open(thumbPath, "r");
            if (f) {
                std::vector<uint8_t> buf(640);
                if (f.read(buf.data(), 640) == 640) {
                    thumbCache[idx] = buf;
                }
                f.close();
            }
        }
        preLoadY += ITEM_HEIGHT;
    }

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        drawTextWithFont(display, "Library", 20, 40, &FreeSansBold12pt8b, GxEPD_BLACK);

        char countText[24];
        snprintf(countText, sizeof(countText), "%d books", (int)_books.size());
        fontMgr.drawTextRight(display, countText, display.width() - 20, 48, FONT_SIZE_SMALL, GxEPD_BLACK);

        display.drawFastHLine(20, 56, display.width() - 40, GxEPD_BLACK);
        display.drawFastHLine(20, 58, 72, GxEPD_BLACK);

        int y = HEADER_H;

        bool backSelected = (_selectedBookIndex == -1);
        if (backSelected) {
            display.fillRect(20, y + 6, 5, BACK_ITEM_HEIGHT - 12, GxEPD_BLACK);
            display.drawRoundRect(16, y + 2, display.width() - 32, BACK_ITEM_HEIGHT - 4, 6, GxEPD_BLACK);
        }
        drawTextWithFont(display, "<  Back to Menu", ITEM_PADDING + 14, y + 32,
                         backSelected ? &FreeSansBold12pt8b : &FreeSans12pt8b, GxEPD_BLACK);
        display.drawFastHLine(ITEM_PADDING, y + BACK_ITEM_HEIGHT - 1, display.width() - (ITEM_PADDING * 2),
                              GxEPD_BLACK);
        y += BACK_ITEM_HEIGHT;

        if (_books.empty()) {
            drawTextWithFont(display, "No books found.", 28, y + 54, &FreeSansBold12pt8b, GxEPD_BLACK);
            fontMgr.drawText(display, "Upload EPUBs via web.", 28, y + 88, FONT_SIZE_BODY, GxEPD_BLACK);
        } else {
            for (size_t idx = (size_t)_libraryScrollOffset; idx < _books.size(); idx++) {
                if (y > display.height() - 70) break;

                const auto& book = _books[idx];
                bool isSelected = ((int)idx == _selectedBookIndex);
                if (isSelected) {
                    display.fillRect(20, y + 12, 5, ITEM_HEIGHT - 24, GxEPD_BLACK);
                    display.drawRoundRect(16, y + 4, display.width() - 32, ITEM_HEIGHT - 8, 6, GxEPD_BLACK);
                } else {
                    display.drawFastHLine(ITEM_PADDING, y + ITEM_HEIGHT - 1,
                                          display.width() - (ITEM_PADDING * 2), GxEPD_BLACK);
                }

                int coverW = COVER_WIDTH;
                int coverH = COVER_HEIGHT;
                int coverX = ITEM_PADDING + 12;
                int coverY = y + (ITEM_HEIGHT - coverH) / 2;

                const uint8_t* tData = nullptr;
                if (thumbCache.count(idx) > 0) {
                    tData = thumbCache[idx].data();
                }

                drawBookTile(display, book, coverX, coverY, coverW, coverH, isSelected, tData);

                uint16_t textColor = GxEPD_BLACK;
                String title = book.title;
                const GFXfont* titleFont = isSelected ? &FreeSansBold12pt8b : &FreeSans12pt8b;
                int textX = ITEM_PADDING + COVER_WIDTH + 44;
                int textY = y + (isSelected ? 36 : 34);
                int lineCount = 0;
                const int MAX_LINES = isSelected ? 3 : 2;
                const int LINE_HEIGHT = isSelected ? 27 : 25;
                const int MAX_WIDTH = display.width() - textX - 28;

                int pos = 0;
                while (pos < (int)title.length() && lineCount < MAX_LINES) {
                    String line = "";
                    while (pos < (int)title.length()) {
                        int nextSpace = title.indexOf(' ', pos);
                        if (nextSpace == -1) nextSpace = title.length();
                        String word = title.substring(pos, nextSpace);
                        String testLine = line.length() > 0 ? line + " " + word : word;
                        if (textWidthForFont(display, testLine.c_str(), titleFont) > MAX_WIDTH &&
                            line.length() > 0)
                            break;
                        line = testLine;
                        pos = nextSpace + 1;
                    }
                    if (lineCount == MAX_LINES - 1 && pos < (int)title.length() && line.length() > 3) {
                        line = line.substring(0, line.length() - 3) + "...";
                    }
                    drawTextWithFont(display, line.c_str(), textX, textY, titleFont, textColor);
                    textY += LINE_HEIGHT;
                    lineCount++;
                }

                if (book.hasProgress) {
                    char pageLabel[32];
                    if (book.totalPages > 0) {
                        snprintf(pageLabel, sizeof(pageLabel), "pag. %d/%d", book.globalPage,
                                 book.totalPages);
                    } else {
                        snprintf(pageLabel, sizeof(pageLabel), "pag. %d", book.globalPage);
                    }
                    drawTextWithFont(display, pageLabel, textX, y + ITEM_HEIGHT - 22, &FreeSans9pt8b,
                                     textColor);
                }

                y += ITEM_HEIGHT;
            }
        }

        char pageStr[24];
        if (_selectedBookIndex == -1) {
            snprintf(pageStr, sizeof(pageStr), "Menu");
        } else {
            snprintf(pageStr, sizeof(pageStr), "%d/%d", _selectedBookIndex + 1, (int)_books.size());
        }
        display.drawFastHLine(20, display.height() - 42, display.width() - 40, GxEPD_BLACK);

        fontMgr.drawText(display, "Joy: Move  |  Center: Open  |  Hold Left: Menu", 22, display.height() - 18,
                         FONT_SIZE_SMALL, GxEPD_BLACK);

        fontMgr.drawTextRight(display, pageStr, display.width() - 20, display.height() - 18, FONT_SIZE_SMALL,
                              GxEPD_BLACK);

        BatteryMgr::getInstance().drawStatusBar(display, display.width() - 105, 10);

    } while (display.nextPage());
}