#include "AppReader.h"
#include "DisplayMgr.h"
#include "FontMgr.h"
#include "KomaBonFS.h"
#include "BookOrderLogic.h"
#include "BookMeta.h"
#include "ProgressStore.h"
#include "PageCountStore.h"
#include "BatteryMgr.h"
#include "SDMgr.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>

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
    const int HEADER_H = 50;
    const int ITEM_HEIGHT = 88;
    int visibleRow = index - scrollOffset;
    return {12, HEADER_H + (visibleRow * ITEM_HEIGHT), screenW - 24, ITEM_HEIGHT + 4};
}

static int libraryItemsPerPage(int screenHeight) {
    const int HEADER_H = 50;
    const int ITEM_HEIGHT = 88;
    int y = HEADER_H;
    int count = 0;
    while (y + ITEM_HEIGHT <= screenHeight - 45) {
        count++;
        y += ITEM_HEIGHT;
    }
    return count;
}

static LibraryDirtyRect unionLibraryRect(LibraryDirtyRect a, LibraryDirtyRect b) {
    int x1 = std::min(a.x, b.x);
    int y1 = std::min(a.y, b.y);
    int x2 = std::max(a.x + a.w, b.x + b.w);
    int y2 = std::max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

void AppReader::scanBooks() {
    _books.clear();

    if (!SDMgr::getInstance().ensureReady()) {
        Serial.println("AppReader: Cannot scan books, SD card bus not responding.");
        return;
    }

    std::map<String, String> metadata;
    loadBookMetadata(metadata);

    File coversDir = SystemFS.open("/covers");
    if (!coversDir) {
        SystemFS.mkdir("/covers");
        Serial.println("AppReader: Directory /covers initialized on SystemFS.");
    } else {
        coversDir.close();
    }

    // Silence the VFS error by proactively creating the JSON file if missing
    if (!SystemFS.exists("/book_order.json")) {
        File of = SystemFS.open("/book_order.json", "w");
        if (of) {
            of.print("{\"order\":[]}");
            of.close();
            Serial.println("AppReader: Initialized empty book_order.json");
        }
    }

    // Closure to safely scan and index a directory
    auto scanDir = [&](const char* dirPath) {
        File root = EbookFS.open(dirPath);
        if (!root && SDMgr::getInstance().ensureReady()) {
            root = EbookFS.open(dirPath);
        }
        if (!root || !root.isDirectory()) return;

        File file = root.openNextFile();
        while (file) {
            if (file.isDirectory()) {
                file = root.openNextFile();
                continue;
            }

            String filePath = file.path();
            if (filePath.isEmpty() || filePath == "null") {
                String fName = file.name();
                if (fName.startsWith("/")) fName = fName.substring(1);
                filePath = String(dirPath);
                if (!filePath.endsWith("/")) filePath += "/";
                filePath += fName;
            }

            String rawName = file.name();
            int slashIdx = rawName.lastIndexOf('/');
            if (slashIdx >= 0) rawName = rawName.substring(slashIdx + 1);

            // Skip hidden OS metadata files that break parsers
            if (rawName.startsWith("._") || rawName.startsWith(".")) {
                file = root.openNextFile();
                continue;
            }

            String fileName = normalizedBookName(rawName);
            String fileNameLower = fileName;
            fileNameLower.toLowerCase();

            if (fileNameLower.endsWith(".epub") || fileNameLower.endsWith(".kmb")) {
                BookEntry entry;
                entry.path = filePath;

                auto meta = metadata.find(fileName);
                entry.originalName = (meta != metadata.end()) ? meta->second : fileName;
                entry.title = FontMgr::utf8ToLatin1(titleFromFilename(entry.originalName));

                int dot = fileName.lastIndexOf('.');
                entry.baseName = (dot > 0) ? fileName.substring(0, dot) : fileName;

                String thumbPath = "/covers/" + entry.baseName + ".thumb";
                entry.hasCoverThumb = SystemFS.exists(thumbPath);
                entry.coverAttempted = entry.hasCoverThumb;

                if (fileNameLower.endsWith(".kmb")) {
                    File kmbFile = EbookFS.open(entry.path.c_str(), "r");
                    if (!kmbFile && SDMgr::getInstance().ensureReady()) {
                        kmbFile = EbookFS.open(entry.path.c_str(), "r");
                    }
                    if (kmbFile) {
                        char magic[5] = {0};
                        kmbFile.readBytes(magic, 4);
                        if (strcmp(magic, "KMB1") == 0) {
                            kmbFile.seek(10);
                            uint16_t pages = 0;
                            kmbFile.read((uint8_t*)&pages, 2);
                            entry.totalPages = pages;
                        }
                        kmbFile.close();
                    }
                }

                _books.push_back(entry);
            }
            file = root.openNextFile();
        }
        root.close();
    };

    // Index both root and standard ebooks folder
    scanDir("/");

    if (!_books.empty()) {
        ProgressStore& store = ProgressStore::getInstance();
        store.begin();

        std::vector<String> present;
        present.reserve(_books.size());
        for (const auto& b : _books) {
            present.push_back(b.originalName);
        }
        store.reconcile(present);

        for (auto& b : _books) {
            BookProgress p;
            if (store.get(b.originalName, p)) {
                b.hasProgress = true;
                b.globalPage = p.globalPage;
            }

            String pathLower = b.path;
            pathLower.toLowerCase();
            if (!pathLower.endsWith(".kmb")) {
                b.totalPages = PageCountStore::getInstance().get(b.originalName, _fontSizePt, _fontFamily);
            }
        }
    }

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

                // Enhanced matching logic for nested paths
                applyBookOrderT(order, _books, [](const BookEntry& e, const String& key) {
                    return e.path.endsWith("/" + key) || e.originalName == key;
                });
            }
        }
    }
}

void AppReader::drawBookTile(KomaBonDisplay& display, const BookEntry& book, int x, int y, int w, int h,
                             bool selected, const uint8_t* thumbData) {
    if (thumbData) {
        display.drawBitmap(x, y, thumbData, 60, 80, GxEPD_BLACK);
    } else {
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

    if (_books.empty()) {
        _selectedBookIndex = 0;
    } else {
        int maxOffset = std::max(0, (int)_books.size() - 1);
        if (_libraryScrollOffset > maxOffset) _libraryScrollOffset = 0;
    }

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    const int HEADER_H = 50;
    const int COVER_WIDTH = 60;
    const int COVER_HEIGHT = 80;
    const int ITEM_HEIGHT = 88;
    const int ITEM_PADDING = 20;

    if (_librarySelectionOnlyRedraw) {
        LibraryDirtyRect dirty =
            unionLibraryRect(libraryItemRect(_previousBookIndex, _libraryScrollOffset, display.width()),
                             libraryItemRect(_selectedBookIndex, _libraryScrollOffset, display.width()));
        LibraryDirtyRect footer = {12, display.height() - 45, display.width() - 24, 42};
        dirty = unionLibraryRect(dirty, footer);
        dirty.x = std::max(0, dirty.x);
        dirty.y = std::max(0, dirty.y);
        if (dirty.x + dirty.w > display.width()) dirty.w = display.width() - dirty.x;
        if (dirty.y + dirty.h > display.height()) dirty.h = display.height() - dirty.y;
        display.setPartialWindow(dirty.x, dirty.y, dirty.w, dirty.h);
    } else {
        display.setPartialWindow(0, 0, display.width(), display.height());
    }
    _librarySelectionOnlyRedraw = false;
    _previousBookIndex = _selectedBookIndex;

    std::map<int, std::vector<uint8_t>> thumbCache;
    int preLoadY = HEADER_H;
    for (size_t idx = (size_t)_libraryScrollOffset; idx < _books.size(); idx++) {
        if (preLoadY + ITEM_HEIGHT > display.height() - 45) break;

        if (_books[idx].hasCoverThumb) {
            String thumbPath = "/covers/" + _books[idx].baseName + ".thumb";
            File f = SystemFS.open(thumbPath, "r");
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

        // Header text re-aligned lower after removing the line
        fontMgr.drawTextBold(display, "Library", 16, 36, FONT_SIZE_SUBTITLE, GxEPD_BLACK);

        char countText[24];
        snprintf(countText, sizeof(countText), "%d books", (int)_books.size());
        fontMgr.drawTextRight(display, countText, display.width() - 16, 38, FONT_SIZE_SMALL, GxEPD_BLACK);

        // Line removed: display.drawFastHLine(16, 42, display.width() - 32, GxEPD_BLACK);

        int y = HEADER_H;

        if (_books.empty()) {
            fontMgr.drawTextBold(display, "No books found.", 20, y + 45, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
            fontMgr.drawText(display, "Upload books via web interface.", 20, y + 70, FONT_SIZE_BODY,
                             GxEPD_BLACK);
        } else {
            for (size_t idx = (size_t)_libraryScrollOffset; idx < _books.size(); idx++) {
                if (y + ITEM_HEIGHT > display.height() - 45) break;

                const auto& book = _books[idx];
                bool isSelected = ((int)idx == _selectedBookIndex);
                if (isSelected) {
                    display.fillRect(14, y + 6, 4, ITEM_HEIGHT - 12, GxEPD_BLACK);
                }

                int coverW = COVER_WIDTH;
                int coverH = COVER_HEIGHT;
                int coverX = ITEM_PADDING + 8;
                int coverY = y + (ITEM_HEIGHT - coverH) / 2;

                const uint8_t* tData = nullptr;
                if (thumbCache.count(idx) > 0) {
                    tData = thumbCache[idx].data();
                }

                drawBookTile(display, book, coverX, coverY, coverW, coverH, isSelected, tData);

                uint16_t textColor = GxEPD_BLACK;
                String title = book.title;
                int textX = ITEM_PADDING + COVER_WIDTH + 24;

                int barX = display.width() - 125;
                int maxTextWidth = barX - textX - 10;

                int dashPos = title.indexOf(" - ");
                String author = "";
                String bookName = title;

                if (dashPos != -1) {
                    author = title.substring(0, dashPos);
                    bookName = title.substring(dashPos + 3);
                }

                if (author.length() > 36) author = author.substring(0, 33) + "...";
                if (dashPos != -1) {
                    fontMgr.drawText(display, author.c_str(), textX, y + 24, FONT_SIZE_BODY, textColor);
                }

                int currentLineY = dashPos != -1 ? (y + 50) : (y + 40);
                int lineHeight = 22;
                int maxLines = 2;
                int lineCount = 0;

                String currentLine = "";
                int pos = 0;
                int bookNameLen = bookName.length();

                while (pos < bookNameLen && lineCount < maxLines) {
                    int nextSpace = bookName.indexOf(' ', pos);
                    if (nextSpace == -1) nextSpace = bookNameLen;
                    String word = bookName.substring(pos, nextSpace);
                    String testLine = currentLine.length() > 0 ? currentLine + " " + word : word;

                    if (fontMgr.getTextWidthBold(testLine.c_str(), FONT_SIZE_MENU) > maxTextWidth &&
                        currentLine.length() > 0) {
                        fontMgr.drawTextBold(display, currentLine.c_str(), textX, currentLineY,
                                             FONT_SIZE_MENU, textColor);
                        currentLineY += lineHeight;
                        lineCount++;
                        currentLine = word;
                        if (lineCount >= maxLines) break;
                    } else {
                        currentLine = testLine;
                    }
                    pos = nextSpace + 1;
                    if (pos > bookNameLen) break;
                }

                if (currentLine.length() > 0 && lineCount < maxLines) {
                    if (pos < bookNameLen && currentLine.length() > 3) {
                        currentLine = currentLine.substring(0, currentLine.length() - 3) + "...";
                    }
                    fontMgr.drawTextBold(display, currentLine.c_str(), textX, currentLineY, FONT_SIZE_MENU,
                                         textColor);
                }

                if (book.hasProgress) {
                    int barY = y + (ITEM_HEIGHT / 2) - 4;
                    int barW = 95;
                    int barH = 6;

                    display.drawRect(barX, barY, barW, barH, GxEPD_BLACK);
                    if (book.totalPages > 0) {
                        float progress = (float)book.globalPage / (float)book.totalPages;
                        if (progress > 1.0f) progress = 1.0f;
                        int fillW = (int)(progress * (barW - 2));
                        if (fillW > 0) {
                            display.fillRect(barX + 1, barY + 1, fillW, barH - 2, GxEPD_BLACK);
                        }
                        char infoStr[24];
                        snprintf(infoStr, sizeof(infoStr), "%d / %d", book.globalPage, book.totalPages);
                        fontMgr.drawText(display, infoStr, barX, barY - 12, FONT_SIZE_BODY, textColor);
                    } else {
                        char pageStr[16];
                        snprintf(pageStr, sizeof(pageStr), "p. %d", book.globalPage);
                        fontMgr.drawText(display, pageStr, barX, barY - 12, FONT_SIZE_BODY, textColor);
                    }
                }

                y += ITEM_HEIGHT;
            }

            int itemsPerPage = libraryItemsPerPage(display.height());
            if ((int)_books.size() > itemsPerPage) {
                int scrollBarX = display.width() - 8;
                int scrollBarY = HEADER_H;
                int scrollBarH = display.height() - scrollBarY - 45;

                display.drawFastVLine(scrollBarX + 1, scrollBarY, scrollBarH, GxEPD_BLACK);

                float progress = (float)_libraryScrollOffset / (_books.size() - itemsPerPage);
                int thumbH = std::max(20, (scrollBarH * itemsPerPage) / (int)_books.size());
                int thumbY = scrollBarY + (int)(progress * (scrollBarH - thumbH));
                display.fillRect(scrollBarX - 1, thumbY, 4, thumbH, GxEPD_BLACK);
            }
        }

        char pageStr[24];
        if (_books.empty()) {
            snprintf(pageStr, sizeof(pageStr), "0/0");
        } else {
            snprintf(pageStr, sizeof(pageStr), "%d/%d", _selectedBookIndex + 1, (int)_books.size());
        }

        // Line removed: display.drawFastHLine(12, display.height() - 40, display.width() - 24, GxEPD_BLACK);

        fontMgr.drawText(display, "Joy: Move  |  Center: Select  |  Hold Left: Menu", 16,
                         display.height() - 16, FONT_SIZE_SMALL, GxEPD_BLACK);

        fontMgr.drawTextRight(display, pageStr, display.width() - 16, display.height() - 16, FONT_SIZE_SMALL,
                              GxEPD_BLACK);

        BatteryMgr::getInstance().drawStatusBar(display, display.width() - 105, 6);

    } while (display.nextPage());
}