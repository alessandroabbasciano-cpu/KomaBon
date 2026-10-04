#include "AppBookshelf.h"
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
#include <algorithm>
#include "../AppReader/icon_reader.h"
#include "SettingsStore.h"
#include "CoverExtractor.h"

static bool naturalSortCompare(const String& a, const String& b) {
    size_t i = 0, j = 0;
    size_t lenA = a.length(), lenB = b.length();

    while (i < lenA && j < lenB) {
        char cA = a.charAt(i);
        char cB = b.charAt(j);

        if (isdigit(cA) && isdigit(cB)) {
            // Compare continuous sequence of digits numerically
            unsigned long numA = 0;
            while (i < lenA && isdigit(a.charAt(i))) {
                numA = numA * 10 + (a.charAt(i) - '0');
                i++;
            }
            unsigned long numB = 0;
            while (j < lenB && isdigit(b.charAt(j))) {
                numB = numB * 10 + (b.charAt(j) - '0');
                j++;
            }
            if (numA != numB) return numA < numB;
        } else {
            char lowerA = tolower(cA);
            char lowerB = tolower(cB);
            if (lowerA != lowerB) return lowerA < lowerB;
            i++;
            j++;
        }
    }
    return lenA < lenB;
}

static String titleFromFilename(String name) {
    name = normalizedBookName(name);
    int dot = name.lastIndexOf('.');
    if (dot > 0) name = name.substring(0, dot);
    name.replace('_', ' ');
    name.trim();
    return name;
}

// Extracts series/opera name matching Web UI logic (e.g. "DanDaDan 01 C.001 - Title" -> "DanDaDan")
static bool extractSeriesInfo(const String& filename, String& outSeries) {
    String name = normalizedBookName(filename);
    int dot = name.lastIndexOf('.');
    if (dot > 0) name = name.substring(0, dot);

    int len = name.length();
    int splitIdx = -1;

    // Pattern 1: Look for space + number (e.g. "Berserk 01" or "DanDaDan 01 C.001")
    for (int i = 0; i < len - 1; i++) {
        char c = name.charAt(i);
        char next = name.charAt(i + 1);
        if (c == ' ' && isdigit(next)) {
            splitIdx = i;
            break;
        }
        // Pattern 2: " Vol." / " v." / " Tome " / " C."
        if (c == ' ' &&
            (next == 'v' || next == 'V' || next == 't' || next == 'T' || next == 'c' || next == 'C')) {
            String sub = name.substring(i + 1);
            sub.toLowerCase();
            if (sub.startsWith("vol") || sub.startsWith("v.") || sub.startsWith("v ") ||
                sub.startsWith("tome") || sub.startsWith("c.") || sub.startsWith("ch")) {
                splitIdx = i;
                break;
            }
        }
        // Pattern 3: " - " before chapter or volume
        if (c == ' ' && next == '-' && i + 2 < len && name.charAt(i + 2) == ' ') {
            splitIdx = i;
            break;
        }
    }

    if (splitIdx > 1) {
        String series = name.substring(0, splitIdx);
        series.trim();
        while (series.endsWith("-") || series.endsWith("_") || series.endsWith(".")) {
            series = series.substring(0, series.length() - 1);
            series.trim();
        }
        if (series.length() >= 2) {
            outSeries = series;
            return true;
        }
    }
    return false;
}

// Extracts volume label (e.g. "Vol. 01") from remainder of filename after series name
static bool extractVolumeInfo(const String& filename, const String& seriesName, String& outVolume) {
    String name = normalizedBookName(filename);
    int dot = name.lastIndexOf('.');
    if (dot > 0) name = name.substring(0, dot);

    // Strip series prefix
    String rem = name;
    if (rem.startsWith(seriesName)) {
        rem = rem.substring(seriesName.length());
        rem.trim();
    }
    while (rem.startsWith("-") || rem.startsWith("_")) {
        rem = rem.substring(1);
        rem.trim();
    }

    // Look for volume indicators: "01", "Vol. 01", "v01", "Tome 01", etc.
    String remLower = rem;
    remLower.toLowerCase();

    // Check if starts with "vol" or "v." or "tome"
    int numStart = -1;
    if (remLower.startsWith("vol.") || remLower.startsWith("vol ") || remLower.startsWith("vol")) {
        int idx = 3;
        while (idx < (int)rem.length() &&
               (rem.charAt(idx) == '.' || rem.charAt(idx) == ' ' || rem.charAt(idx) == '_'))
            idx++;
        numStart = idx;
    } else if (remLower.startsWith("v.") || remLower.startsWith("v ") ||
               (remLower.startsWith("v") && remLower.length() > 1 && isdigit(remLower.charAt(1)))) {
        int idx = 1;
        while (idx < (int)rem.length() &&
               (rem.charAt(idx) == '.' || rem.charAt(idx) == ' ' || rem.charAt(idx) == '_'))
            idx++;
        numStart = idx;
    } else if (remLower.startsWith("tome")) {
        int idx = 4;
        while (idx < (int)rem.length() &&
               (rem.charAt(idx) == '.' || rem.charAt(idx) == ' ' || rem.charAt(idx) == '_'))
            idx++;
        numStart = idx;
    } else if (rem.length() > 0 && isdigit(rem.charAt(0))) {
        numStart = 0;
    }

    if (numStart >= 0 && numStart < (int)rem.length() && isdigit(rem.charAt(numStart))) {
        int numEnd = numStart;
        while (numEnd < (int)rem.length() && isdigit(rem.charAt(numEnd)))
            numEnd++;
        String volDigits = rem.substring(numStart, numEnd);
        int volNum = volDigits.toInt();
        char buf[32];
        snprintf(buf, sizeof(buf), "Vol. %02d", volNum);
        outVolume = String(buf);
        return true;
    }

    return false;
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

void AppBookshelf::scanBooks() {
    _books.clear();

    if (!SDMgr::getInstance().ensureReady()) {
        Serial.println("AppBookshelf: Cannot scan books, SD card bus not responding.");
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
                b.rtl = p.rtl;
            }

            String pathLower = b.path;
            pathLower.toLowerCase();
            if (!pathLower.endsWith(".kmb")) {
                b.totalPages = PageCountStore::getInstance().get(
                    b.originalName, SettingsStore::getInstance().loadReader().fontSize,
                    SettingsStore::getInstance().loadReader().fontFamily);
            }
        }
    }

    // Natural alphanumeric sort by default (so Vol. 2 comes before Vol. 10, alphabetical by original name)
    std::sort(_books.begin(), _books.end(), [](const BookEntry& a, const BookEntry& b) {
        return naturalSortCompare(a.originalName, b.originalName);
    });

    File of = SystemFS.open("/book_order.json", "r");
    if (of) {
        DynamicJsonDocument doc(4096);
        DeserializationError err = deserializeJson(doc, of);
        of.close();
        if (!err) {
            JsonArray arr = doc["order"].as<JsonArray>();
            if (!arr.isNull() && arr.size() > 0) {
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

    buildLibraryStructure();
}

void AppBookshelf::buildLibraryStructure() {
    _rootItems.clear();
    if (_books.empty()) {
        _activeItems.clear();
        return;
    }

    // 1. Count occurrences of series among KMB/manga files
    std::map<String, std::vector<int>> seriesMap;
    std::vector<String> seriesOrder;

    for (size_t i = 0; i < _books.size(); i++) {
        String pathLower = _books[i].path;
        pathLower.toLowerCase();
        if (pathLower.endsWith(".kmb")) {
            String series;
            if (extractSeriesInfo(_books[i].originalName, series)) {
                if (seriesMap.find(series) == seriesMap.end()) {
                    seriesOrder.push_back(series);
                }
                seriesMap[series].push_back((int)i);
            }
        }
    }

    // 2. Build root items: single books or series folders (if count >= 2)
    std::vector<bool> consumed(_books.size(), false);

    for (const auto& sName : seriesOrder) {
        const auto& indices = seriesMap[sName];
        if (indices.size() >= 2) {
            BookshelfItem item;
            item.type = ITEM_SERIES;
            item.name = sName;
            item.count = indices.size();
            item.bookIndices = indices;
            item.coverBookIndex = indices[0]; // Borrow cover from 1st volume/chapter
            _rootItems.push_back(item);

            for (int idx : indices) {
                consumed[idx] = true;
            }
        }
    }

    for (size_t i = 0; i < _books.size(); i++) {
        if (!consumed[i]) {
            BookshelfItem item;
            item.type = ITEM_BOOK;
            item.name = _books[i].title;
            item.count = 1;
            item.bookIndex = (int)i;
            item.coverBookIndex = (int)i;
            _rootItems.push_back(item);
        }
    }

    // Natural sort root items
    std::sort(_rootItems.begin(), _rootItems.end(), [](const BookshelfItem& a, const BookshelfItem& b) {
        return naturalSortCompare(a.name, b.name);
    });

    if (_currentSeriesFilter.length() > 0) {
        enterSeries(_currentSeriesFilter);
    } else {
        _activeItems = _rootItems;
    }
}

void AppBookshelf::enterSeries(const String& seriesName) {
    _currentSeriesFilter = seriesName;
    _currentVolumeFilter = "";
    _activeItems.clear();

    // Find the series item
    std::vector<int> allIndices;
    for (const auto& rItem : _rootItems) {
        if (rItem.type == ITEM_SERIES && rItem.name == seriesName) {
            allIndices = rItem.bookIndices;
            break;
        }
    }

    // Check if items within this series can be grouped into volumes
    std::map<String, std::vector<int>> volMap;
    std::vector<String> volOrder;
    std::vector<int> ungrouped;

    for (int idx : allIndices) {
        String vol;
        if (extractVolumeInfo(_books[idx].originalName, seriesName, vol)) {
            if (volMap.find(vol) == volMap.end()) {
                volOrder.push_back(vol);
            }
            volMap[vol].push_back(idx);
        } else {
            ungrouped.push_back(idx);
        }
    }

    // Group into volumes if multiple volumes exist or if a volume has multiple chapters
    bool hasMultipleVolumes = (volOrder.size() > 1);
    bool anyVolumeHasMultiple = false;
    for (const auto& vName : volOrder) {
        if (volMap[vName].size() >= 2) {
            anyVolumeHasMultiple = true;
            break;
        }
    }

    if (hasMultipleVolumes || anyVolumeHasMultiple) {
        // Natural sort volume folders
        std::sort(volOrder.begin(), volOrder.end(),
                  [](const String& a, const String& b) { return naturalSortCompare(a, b); });

        for (const auto& vName : volOrder) {
            const auto& vIndices = volMap[vName];
            if (vIndices.size() >= 2 || hasMultipleVolumes) {
                BookshelfItem vItem;
                vItem.type = ITEM_VOLUME;
                vItem.name = vName;
                vItem.count = vIndices.size();
                vItem.bookIndices = vIndices;
                vItem.coverBookIndex = vIndices[0];
                _activeItems.push_back(vItem);
            } else {
                // Single item in a single volume
                BookshelfItem item;
                item.type = ITEM_BOOK;
                item.name = _books[vIndices[0]].title;
                item.count = 1;
                item.bookIndex = vIndices[0];
                item.coverBookIndex = vIndices[0];
                _activeItems.push_back(item);
            }
        }

        // Add any ungrouped items directly
        for (int idx : ungrouped) {
            BookshelfItem item;
            item.type = ITEM_BOOK;
            item.name = _books[idx].title;
            item.count = 1;
            item.bookIndex = idx;
            item.coverBookIndex = idx;
            _activeItems.push_back(item);
        }
    } else {
        // No distinct volumes found; list all books directly
        for (int idx : allIndices) {
            BookshelfItem item;
            item.type = ITEM_BOOK;
            item.name = _books[idx].title;
            item.count = 1;
            item.bookIndex = idx;
            item.coverBookIndex = idx;
            _activeItems.push_back(item);
        }
    }

    _selectedBookIndex = 0;
    _libraryScrollOffset = 0;
    _librarySelectionOnlyRedraw = false;
    _needsRedraw = true;
}

void AppBookshelf::enterVolume(const String& volumeLabel, const std::vector<int>& bookIndices) {
    _currentVolumeFilter = volumeLabel;
    _activeItems.clear();

    for (int idx : bookIndices) {
        BookshelfItem item;
        item.type = ITEM_BOOK;
        item.name = _books[idx].title;
        item.count = 1;
        item.bookIndex = idx;
        item.coverBookIndex = idx;
        _activeItems.push_back(item);
    }

    _selectedBookIndex = 0;
    _libraryScrollOffset = 0;
    _librarySelectionOnlyRedraw = false;
    _needsRedraw = true;
}

void AppBookshelf::exitCurrentLevel() {
    if (_currentVolumeFilter.length() > 0) {
        // Return from Volume to Series level
        _currentVolumeFilter = "";
        enterSeries(_currentSeriesFilter);
        _selectedBookIndex = _savedSeriesSelectedIndex;
        _libraryScrollOffset = _savedSeriesScrollOffset;
        if (_selectedBookIndex >= (int)_activeItems.size())
            _selectedBookIndex = std::max(0, (int)_activeItems.size() - 1);
        _librarySelectionOnlyRedraw = false;
        _needsRedraw = true;
    } else if (_currentSeriesFilter.length() > 0) {
        // Return from Series to Root library
        _currentSeriesFilter = "";
        _activeItems = _rootItems;
        _selectedBookIndex = _savedRootSelectedIndex;
        _libraryScrollOffset = _savedRootScrollOffset;
        if (_selectedBookIndex >= (int)_activeItems.size())
            _selectedBookIndex = std::max(0, (int)_activeItems.size() - 1);
        _librarySelectionOnlyRedraw = false;
        _needsRedraw = true;
    }
}

void AppBookshelf::drawFolderTile(KomaBonDisplay& display, int x, int y, int w, int h, bool selected,
                                  const uint8_t* thumbData) {
    if (thumbData) {
        display.drawBitmap(x, y, thumbData, 60, 80, GxEPD_BLACK);
    } else {
        display.fillRect(x, y, w, h, GxEPD_WHITE);
        display.drawRoundRect(x, y, w, h, 6, GxEPD_BLACK);
        display.drawRoundRect(x + 2, y + 2, w - 4, h - 4, 4, GxEPD_BLACK);

        // Draw cute folder tab icon
        display.fillRect(x + 12, y + 16, 18, 6, GxEPD_BLACK);
        display.drawRect(x + 10, y + 20, w - 20, h - 34, GxEPD_BLACK);
        display.fillRect(x + 12, y + 22, w - 24, h - 38, GxEPD_WHITE);
        display.drawFastHLine(x + 16, y + 32, w - 32, GxEPD_BLACK);
        display.drawFastHLine(x + 16, y + 44, w - 32, GxEPD_BLACK);
    }

    if (selected) {
        display.drawRect(x - 3, y - 3, w + 6, h + 6, GxEPD_BLACK);
        display.drawRect(x - 2, y - 2, w + 4, h + 4, GxEPD_BLACK);
    }
}

void AppBookshelf::drawBookTile(KomaBonDisplay& display, const BookEntry& book, int x, int y, int w, int h,
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

void AppBookshelf::updateLibraryScroll() {
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

void AppBookshelf::drawLibrary() {
    if (!_booksScanned) {
        scanBooks();
        _booksScanned = true;
    }

    if (_activeItems.empty()) {
        _selectedBookIndex = 0;
    } else {
        int maxOffset = std::max(0, (int)_activeItems.size() - 1);
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
    for (size_t idx = (size_t)_libraryScrollOffset; idx < _activeItems.size(); idx++) {
        if (preLoadY + ITEM_HEIGHT > display.height() - 45) break;

        int bIdx = _activeItems[idx].coverBookIndex;
        if (bIdx >= 0 && bIdx < (int)_books.size() && _books[bIdx].hasCoverThumb) {
            String thumbPath = "/covers/" + _books[bIdx].baseName + ".thumb";
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

        // Header text: "Library", "< SeriesName", or "< SeriesName (Vol. 01)"
        if (_currentVolumeFilter.length() > 0) {
            String headerStr = "< " + _currentSeriesFilter + " (" + _currentVolumeFilter + ")";
            if (headerStr.length() > 32) headerStr = headerStr.substring(0, 30) + "...";
            fontMgr.drawTextBold(display, headerStr.c_str(), 16, 36, FONT_SIZE_SUBTITLE, GxEPD_BLACK);

            char countText[24];
            snprintf(countText, sizeof(countText), "%d ch.", (int)_activeItems.size());
            fontMgr.drawTextRight(display, countText, display.width() - 16, 38, FONT_SIZE_SMALL, GxEPD_BLACK);
        } else if (_currentSeriesFilter.length() > 0) {
            String headerStr = "< " + _currentSeriesFilter;
            if (headerStr.length() > 32) headerStr = headerStr.substring(0, 30) + "...";
            fontMgr.drawTextBold(display, headerStr.c_str(), 16, 36, FONT_SIZE_SUBTITLE, GxEPD_BLACK);

            char countText[24];
            snprintf(countText, sizeof(countText), "%d items", (int)_activeItems.size());
            fontMgr.drawTextRight(display, countText, display.width() - 16, 38, FONT_SIZE_SMALL, GxEPD_BLACK);
        } else {
            fontMgr.drawTextBold(display, "Library", 16, 36, FONT_SIZE_SUBTITLE, GxEPD_BLACK);

            char countText[24];
            snprintf(countText, sizeof(countText), "%d books", (int)_books.size());
            fontMgr.drawTextRight(display, countText, display.width() - 16, 38, FONT_SIZE_SMALL, GxEPD_BLACK);
        }

        int y = HEADER_H;

        if (_activeItems.empty()) {
            fontMgr.drawTextBold(display, "No books found.", 20, y + 45, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
            fontMgr.drawText(display, "Upload books via web interface.", 20, y + 70, FONT_SIZE_BODY,
                             GxEPD_BLACK);
        } else {
            for (size_t idx = (size_t)_libraryScrollOffset; idx < _activeItems.size(); idx++) {
                if (y + ITEM_HEIGHT > display.height() - 45) break;

                const auto& item = _activeItems[idx];
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

                uint16_t textColor = GxEPD_BLACK;
                int textX = ITEM_PADDING + COVER_WIDTH + 24;
                int barX = display.width() - 125;
                int maxTextWidth = barX - textX - 10;

                if (item.type == ITEM_SERIES) {
                    drawFolderTile(display, coverX, coverY, coverW, coverH, isSelected, tData);

                    fontMgr.drawText(display, "Manga Series", textX, y + 24, FONT_SIZE_BODY, textColor);
                    fontMgr.drawTextBold(display, item.name.c_str(), textX, y + 48, FONT_SIZE_MENU,
                                         textColor);

                    char seriesCountStr[24];
                    snprintf(seriesCountStr, sizeof(seriesCountStr), "%d items >", item.count);
                    fontMgr.drawText(display, seriesCountStr, barX, y + (ITEM_HEIGHT / 2) - 4, FONT_SIZE_BODY,
                                     textColor);

                } else if (item.type == ITEM_VOLUME) {
                    drawFolderTile(display, coverX, coverY, coverW, coverH, isSelected, tData);

                    fontMgr.drawText(display, _currentSeriesFilter.c_str(), textX, y + 24, FONT_SIZE_BODY,
                                     textColor);
                    fontMgr.drawTextBold(display, item.name.c_str(), textX, y + 48, FONT_SIZE_MENU,
                                         textColor);

                    char volCountStr[24];
                    snprintf(volCountStr, sizeof(volCountStr), "%d ch. >", item.count);
                    fontMgr.drawText(display, volCountStr, barX, y + (ITEM_HEIGHT / 2) - 4, FONT_SIZE_BODY,
                                     textColor);

                } else {
                    const auto& book = _books[item.bookIndex];
                    drawBookTile(display, book, coverX, coverY, coverW, coverH, isSelected, tData);

                    String title = book.title;
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
                        fontMgr.drawTextBold(display, currentLine.c_str(), textX, currentLineY,
                                             FONT_SIZE_MENU, textColor);
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
                                if (book.rtl) {
                                    display.fillRect(barX + barW - 1 - fillW, barY + 1, fillW, barH - 2,
                                                     GxEPD_BLACK);
                                } else {
                                    display.fillRect(barX + 1, barY + 1, fillW, barH - 2, GxEPD_BLACK);
                                }
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
                }

                y += ITEM_HEIGHT;
            }

            int itemsPerPage = libraryItemsPerPage(display.height());
            if ((int)_activeItems.size() > itemsPerPage) {
                int scrollBarX = display.width() - 8;
                int scrollBarY = HEADER_H;
                int scrollBarH = display.height() - scrollBarY - 45;

                display.drawFastVLine(scrollBarX + 1, scrollBarY, scrollBarH, GxEPD_BLACK);

                float progress = (float)_libraryScrollOffset / (_activeItems.size() - itemsPerPage);
                int thumbH = std::max(20, (scrollBarH * itemsPerPage) / (int)_activeItems.size());
                int thumbY = scrollBarY + (int)(progress * (scrollBarH - thumbH));
                display.fillRect(scrollBarX - 1, thumbY, 4, thumbH, GxEPD_BLACK);
            }
        }

        char pageStr[24];
        if (_activeItems.empty()) {
            snprintf(pageStr, sizeof(pageStr), "0/0");
        } else {
            snprintf(pageStr, sizeof(pageStr), "%d/%d", _selectedBookIndex + 1, (int)_activeItems.size());
        }

        const char* hintText = (_currentVolumeFilter.length() > 0)
                                   ? "Joy: Move | Center: Read | Left: Back | Hold Left: Menu"
                                   : ((_currentSeriesFilter.length() > 0)
                                          ? "Joy: Move | Center: Open | Left: Back | Hold Left: Menu"
                                          : "Joy: Move | Center: Select | Hold Left: Menu");
        fontMgr.drawText(display, hintText, 16, display.height() - 16, FONT_SIZE_SMALL, GxEPD_BLACK);

        fontMgr.drawTextRight(display, pageStr, display.width() - 16, display.height() - 16, FONT_SIZE_SMALL,
                              GxEPD_BLACK);

        BatteryMgr::getInstance().drawStatusBar(display, display.width() - 105, 6);

    } while (display.nextPage());
}

AppBookshelf::AppBookshelf()
    : _selectedBookIndex(0), _booksScanned(false), _librarySelectionOnlyRedraw(false), _previousBookIndex(0),
      _libraryScrollOffset(0), _needsRedraw(true), _savedRootSelectedIndex(0), _savedRootScrollOffset(0),
      _savedSeriesSelectedIndex(0), _savedSeriesScrollOffset(0) {}

const uint8_t* AppBookshelf::getIconImage() {
    return icon_reader_160x160;
}

void AppBookshelf::start() {
    DisplayMgr::getInstance().enableFastRefreshA2();
    _needsRedraw = true;
    _booksScanned = false;
    _librarySelectionOnlyRedraw = false;
    _currentSeriesFilter = "";
    _currentVolumeFilter = "";
    InputMgr::getInstance().setCallback(std::bind(&AppBookshelf::handleInput, this, std::placeholders::_1));
}

void AppBookshelf::stop() {
    InputMgr::getInstance().clearCallback();
}

void AppBookshelf::update() {
    if (CoverExtractor::processNextCover(_books)) {
        _librarySelectionOnlyRedraw = false;
        _needsRedraw = true;
    }
}

void AppBookshelf::draw() {
    if (_needsRedraw) {
        _needsRedraw = false;
        drawLibrary();
    }
}

void AppBookshelf::forceRedraw() {
    _librarySelectionOnlyRedraw = false;
    _needsRedraw = true;
}

void AppBookshelf::invalidateLibrary() {
    _booksScanned = false;
    _librarySelectionOnlyRedraw = false;
    _needsRedraw = true;
}

#include "../../KomaBon_Core/AppMgr.h"
#include "../AppReader/AppReader.h"

void AppBookshelf::handleInput(InputAction action) {
    if (action == INPUT_GO_TO_MAIN_MENU) {
        AppMgr::getInstance().switchTo(0);
        return;
    }
    if (action == INPUT_BACK || action == INPUT_LEFT) {
        if (_currentVolumeFilter.length() > 0 || _currentSeriesFilter.length() > 0) {
            exitCurrentLevel();
            return;
        }
        AppMgr::getInstance().switchTo(0);
        return;
    }
    if (_activeItems.empty()) return;
    int maxIndex = (int)_activeItems.size() - 1;
    if (action == INPUT_NEXT) {
        if (!_librarySelectionOnlyRedraw) _previousBookIndex = _selectedBookIndex;
        _selectedBookIndex++;
        if (_selectedBookIndex > maxIndex) _selectedBookIndex = 0;
        _librarySelectionOnlyRedraw = _booksScanned;
        updateLibraryScroll();
        _needsRedraw = true;
    } else if (action == INPUT_PREV) {
        if (!_librarySelectionOnlyRedraw) _previousBookIndex = _selectedBookIndex;
        _selectedBookIndex--;
        if (_selectedBookIndex < 0) _selectedBookIndex = maxIndex;
        _librarySelectionOnlyRedraw = _booksScanned;
        updateLibraryScroll();
        _needsRedraw = true;
    } else if (action == INPUT_SELECT) {
        if (_selectedBookIndex >= 0 && _selectedBookIndex <= maxIndex) {
            // CRITICAL: Make a full copy by value!
            // enterSeries() and enterVolume() call _activeItems.clear(),
            // which would immediately invalidate a reference to _activeItems[_selectedBookIndex],
            // causing heap corruption and Watchdog Timer reset.
            BookshelfItem item = _activeItems[_selectedBookIndex];
            if (item.type == ITEM_SERIES) {
                // Save current root position so we can return to it smoothly
                _savedRootSelectedIndex = _selectedBookIndex;
                _savedRootScrollOffset = _libraryScrollOffset;
                enterSeries(item.name);
            } else if (item.type == ITEM_VOLUME) {
                // Save current series position so we can return to it smoothly
                _savedSeriesSelectedIndex = _selectedBookIndex;
                _savedSeriesScrollOffset = _libraryScrollOffset;
                enterVolume(item.name, item.bookIndices);
            } else {
                AppReader* reader = (AppReader*)AppMgr::getInstance().getAppByName("Reader");
                if (reader && item.bookIndex >= 0 && item.bookIndex < (int)_books.size()) {
                    reader->openBook(_books[item.bookIndex].path.c_str());
                    AppMgr::getInstance().switchTo("Reader");
                }
            }
        }
    }
}
