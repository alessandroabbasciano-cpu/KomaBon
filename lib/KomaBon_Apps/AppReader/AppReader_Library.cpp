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
    const int HEADER_H = 60;
    const int BACK_ITEM_HEIGHT = 42;
    const int ITEM_HEIGHT = 88;
    if (index < 0) {
        return {12, HEADER_H, screenW - 24, BACK_ITEM_HEIGHT + 4};
    }
    int visibleRow = index - scrollOffset;
    return {12, HEADER_H + BACK_ITEM_HEIGHT + (visibleRow * ITEM_HEIGHT), screenW - 24, ITEM_HEIGHT + 4};
}

static int libraryItemsPerPage(int screenHeight) {
    const int HEADER_H = 60;
    const int BACK_ITEM_HEIGHT = 42;
    const int ITEM_HEIGHT = 88;
    int y = HEADER_H + BACK_ITEM_HEIGHT;
    int count = 0;
    while (y <= screenHeight - 50) {
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

    if (!SDMgr::getInstance().isMounted()) {
        Serial.println("AppReader: Cannot scan books, SD card not mounted.");
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

    int maxOffset = std::max(0, (int)_books.size() - 1);
    if (_libraryScrollOffset > maxOffset) _libraryScrollOffset = 0;
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();

    const int HEADER_H = 60;
    const int BACK_ITEM_HEIGHT = 42;
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

    std::map<int, std::vector<uint8_t>> thumbCache;
    int preLoadY = HEADER_H + BACK_ITEM_HEIGHT;
    for (size_t idx = (size_t)_libraryScrollOffset; idx < _books.size(); idx++) {
        if (preLoadY > display.height() - 50) break;
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

        drawTextWithFont(display, "Library", 16, 36, &FreeSansBold12pt8b, GxEPD_BLACK);

        char countText[24];
        snprintf(countText, sizeof(countText), "%d books", (int)_books.size());
        fontMgr.drawTextRight(display, countText, display.width() - 16, 38, FONT_SIZE_SMALL, GxEPD_BLACK);

        display.drawFastHLine(16, 46, display.width() - 32, GxEPD_BLACK);

        int y = HEADER_H;

        bool backSelected = (_selectedBookIndex == -1);
        if (backSelected) {
            display.fillRect(14, y + 5, 4, BACK_ITEM_HEIGHT - 10, GxEPD_BLACK);
        }
        drawTextWithFont(display, "<  Back to Menu", ITEM_PADDING + 10, y + 28,
                         backSelected ? &FreeSansBold12pt8b : &FreeSans12pt8b, GxEPD_BLACK);
        y += BACK_ITEM_HEIGHT;

        if (_books.empty()) {
            drawTextWithFont(display, "No books found.", 20, y + 45, &FreeSansBold12pt8b, GxEPD_BLACK);
            fontMgr.drawText(display, "Upload books via web interface.", 20, y + 70, FONT_SIZE_BODY,
                             GxEPD_BLACK);
        } else {
            for (size_t idx = (size_t)_libraryScrollOffset; idx < _books.size(); idx++) {
                if (y > display.height() - 50) break;

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
                    drawTextWithFont(display, author.c_str(), textX, y + 26, &FreeSans9pt8b, textColor);
                }

                const GFXfont* titleFont = &FreeSansBold12pt8b;
                int currentLineY = dashPos != -1 ? (y + 52) : (y + 42);
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

                    if (textWidthForFont(display, testLine.c_str(), titleFont) > maxTextWidth &&
                        currentLine.length() > 0) {
                        drawTextWithFont(display, currentLine.c_str(), textX, currentLineY, titleFont,
                                         textColor);
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
                    drawTextWithFont(display, currentLine.c_str(), textX, currentLineY, titleFont, textColor);
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
                        drawTextWithFont(display, infoStr, barX, barY - 12, &FreeSans9pt8b, textColor);
                    } else {
                        char pageStr[16];
                        snprintf(pageStr, sizeof(pageStr), "p. %d", book.globalPage);
                        drawTextWithFont(display, pageStr, barX, barY - 12, &FreeSans9pt8b, textColor);
                    }
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
        display.drawFastHLine(12, display.height() - 40, display.width() - 24, GxEPD_BLACK);

        fontMgr.drawText(display, "Joy: Move  |  Center: Open  |  Hold Left: Menu", 16, display.height() - 16,
                         FONT_SIZE_SMALL, GxEPD_BLACK);

        fontMgr.drawTextRight(display, pageStr, display.width() - 16, display.height() - 16, FONT_SIZE_SMALL,
                              GxEPD_BLACK);

        BatteryMgr::getInstance().drawStatusBar(display, display.width() - 105, 6);

    } while (display.nextPage());
}