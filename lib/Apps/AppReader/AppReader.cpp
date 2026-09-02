#include "AppReader.h"
#include "DisplayMgr.h"
#include "InputMgr.h"
#include "FontMgr.h"
#include "AppMgr.h"
#include "icon_reader.h"
#include "KomaBonFS.h"
#include "BookOrderLogic.h"
#include "BookMeta.h"
#include "ProgressStore.h"
#include "PageCountStore.h"
#include "WebMgr.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
// Local FreeSans with Latin-1 Supplement (0x20-0xFF) so Portuguese/European book
// titles render correctly in the library list.
#include "Fonts/FreeSans.h"
#include <map>

static String normalizedBookName(const String& path) {
    String name = path;
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    slash = name.lastIndexOf('\\');
    if (slash >= 0) name = name.substring(slash + 1);
    return name;
}

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

// How many book rows fit below the header/back item, given the same vertical
// budget the draw loop in drawLibrary() uses. Kept in sync with that loop's
// "if (y > display.height() - 70) break;" condition.
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

AppReader::AppReader() {
    _state = VIEW_LIBRARY;
    _selectedBookIndex = 0;
    _booksScanned = false;
    _librarySelectionOnlyRedraw = false;
    _resumeSavedBookOnStart = false;
    _previousBookIndex = 0;
    _libraryScrollOffset = 0;
    _epubLoader = nullptr;
    _textRenderer = nullptr;
    _kbReader = nullptr;  // NEW
    _isComicMode = false; // NEW
    _currentChapter = 0;
    _needsRedraw = true;
    _totalPages = 0;
    _countingActive = false;
    _countRenderer = nullptr;
    _countChapter = 0;
    _countPointer = {0, 0};
    _countPagesSoFar = 0;
    _currentPageRender = {0, 0, false, 0, 0};
    _currentPageRenderValid = false;
    _pageTurnsSinceRefresh = 0;
    _refreshEveryNPages = 10;       // Default to full refresh every 10 pages
    _fontSizePt = 9;                // Default body size (small)
    _fontFamily = READER_FONT_SANS; // Default family (system sans-serif)
    _readingFirstDraw = true;
    loadSettings();
}

void AppReader::loadSettings() {
    // Try EbookFS first (where uploadfs puts files), then SystemFS
    File file;
    if (EbookFS.exists("/reader_config.json")) {
        file = EbookFS.open("/reader_config.json", "r");
    } else if (SystemFS.exists("/reader_config.json")) {
        file = SystemFS.open("/reader_config.json", "r");
    }

    if (file) {
        DynamicJsonDocument doc(512);
        if (!deserializeJson(doc, file)) {
            if (doc.containsKey("refreshFrequency")) _refreshEveryNPages = doc["refreshFrequency"];
            if (doc.containsKey("fontSize")) {
                int pt = doc["fontSize"];
                _fontSizePt = (pt >= 18) ? 18 : (pt >= 12 ? 12 : 9);
            }
            if (doc.containsKey("fontFamily")) {
                int fam = doc["fontFamily"];
                _fontFamily =
                    (fam >= READER_FONT_SANS && fam <= READER_FONT_OPEN_SANS) ? fam : READER_FONT_SANS;
            }
        }
        file.close();
    }
    // No config file is fine - just use defaults
}

AppReader::~AppReader() {
    closeBook(false);
    if (_epubLoader) delete _epubLoader;
    if (_textRenderer) delete _textRenderer;
    if (_kbReader) delete _kbReader;
}

bool AppReader::hasBootResume() {
    ProgressStore& store = ProgressStore::getInstance();
    if (!store.resumeOnBoot()) return false;
    String last = store.lastBook();
    if (last.length() == 0) return false;
    return findFilenameForOriginal(last).length() > 0;
}

void AppReader::resumeSavedBookOnStart() {
    _resumeSavedBookOnStart = true;
}

void AppReader::start() {
    if (WiFi.getMode() != WIFI_OFF) {
        WebMgr::getInstance().stop();
        delay(50);
        WiFi.disconnect(false);
        WiFi.mode(WIFI_OFF);
        Serial.println("AppReader: WiFi powered down");
    }

    // Pick up any settings (font size, refresh interval) changed via the web UI
    // while we were away.
    loadSettings();
    if (_textRenderer) {
        _textRenderer->setFontSize(_fontSizePt);
        _textRenderer->setFontFamily(_fontFamily);
    }

    _state = VIEW_LIBRARY;
    _booksScanned = false;
    _librarySelectionOnlyRedraw = false;
    _needsRedraw = true;
    InputMgr::getInstance().setCallback(std::bind(&AppReader::handleInput, this, std::placeholders::_1));

    if (_resumeSavedBookOnStart) {
        _resumeSavedBookOnStart = false;
        if (!openSavedProgress()) {
            markProgressInactive();
        }
    }
}

void AppReader::stop() {
    closeBook();
    InputMgr::getInstance().clearCallback();
}

const uint8_t* AppReader::getIconImage() {
    return icon_reader_160x160;
}

void AppReader::scanBooks() {
    _books.clear();
    std::map<String, String> metadata;
    loadBookMetadata(metadata);

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
            // Convert to Latin-1 once at load time: the library list draws and
            // measures these bytes directly (bypassing FontMgr::drawText), and
            // the WebUI reads titles from books_meta.json, so UTF-8 is
            // preserved where it matters and collapsed where the display needs it.
            entry.originalName = (meta != metadata.end()) ? meta->second : fileName;
            entry.title = FontMgr::utf8ToLatin1(titleFromFilename(entry.originalName));
            _books.push_back(entry);
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();

    // v1.8.0: one read of the progress store for the whole library (not one
    // per book), plus pruning of entries whose .epub is gone and clearing of
    // `pending` for imported entries whose file has now arrived.
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
            // Only known once a book has been read all the way through at the
            // current font settings (see startTotalPagesCounting) — scanning
            // every unread book here would be exactly the slow-open problem
            // this feature is careful to avoid.
            b.totalPages = PageCountStore::getInstance().get(b.originalName, _fontSizePt, _fontFamily);
        }
    }

    // v1.2.0: apply manual order from SystemFS /book_order.json.
    // Same merge rule as WebMgr's /api/books: ordered entries that still
    // exist first, remaining books appended in FS enumeration order.
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

void AppReader::drawBookTile(KomaBonDisplay& display, int x, int y, int w, int h, bool selected) {
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

    if (selected) {
        display.fillRect(x + w - 9, y + 8, 4, h - 16, GxEPD_BLACK);
    }
}

void AppReader::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;
    Serial.printf("AppReader::handleInput - action: %d, state: %d\n", action, _state);
    if (_state == VIEW_LIBRARY) {
        // Index -1 = "Back to Menu", 0+ = books
        int maxIndex = (int)_books.size() - 1;
        if (action == INPUT_NEXT) {
            _previousBookIndex = _selectedBookIndex;
            _selectedBookIndex++;
            if (_selectedBookIndex > maxIndex) _selectedBookIndex = -1; // Wrap to Back option
            _librarySelectionOnlyRedraw = _booksScanned;
            updateLibraryScroll();
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _previousBookIndex = _selectedBookIndex;
            _selectedBookIndex--;
            if (_selectedBookIndex < -1) _selectedBookIndex = maxIndex; // Wrap to last book
            _librarySelectionOnlyRedraw = _booksScanned;
            updateLibraryScroll();
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_selectedBookIndex == -1) {
                // Back to main menu
                markProgressInactive();
                AppMgr::getInstance().switchTo(0);
            } else if (!_books.empty() && _selectedBookIndex >= 0) {
                openBook(_books[_selectedBookIndex].path.c_str());
            }
        } else if (action == INPUT_BACK) {
            // KEY3: dedicated Back button. From the library, go straight to
            // the main menu without needing to navigate to the Back item.
            markProgressInactive();
            AppMgr::getInstance().switchTo(0);
        } else if (action == INPUT_GO_TO_MAIN_MENU) {
            // KEY1 long press: go to main menu from library
            Serial.println("AppReader: INPUT_GO_TO_MAIN_MENU -> switching to main menu");
            markProgressInactive();
            AppMgr::getInstance().switchTo(0);
        }
    } else if (_state == VIEW_READING) {
        if (action == INPUT_NEXT)
            nextPage();
        else if (action == INPUT_PREV)
            prevPage();
        else if (action == INPUT_SELECT) {
            closeBook();
            _state = VIEW_LIBRARY;
            // Force drawLibrary() to rescan: the book just closed may have
            // finished its total page count while it was open, and the list
            // scanned on the way in here is now stale for it.
            _booksScanned = false;
            _librarySelectionOnlyRedraw = false;
            _needsRedraw = true;
        } else if (action == INPUT_BACK) {
            // KEY3: dedicated Back button. Return to the library from the
            // reading view, same destination as INPUT_SELECT here.
            closeBook();
            _state = VIEW_LIBRARY;
            _booksScanned = false;
            _librarySelectionOnlyRedraw = false;
            _needsRedraw = true;
        } else if (action == INPUT_GO_TO_MAIN_MENU) {
            // KEY1 long press: go directly to main menu from reading view
            Serial.println("AppReader: INPUT_GO_TO_MAIN_MENU from READING -> switching to main menu");
            closeBook();
            _state = VIEW_LIBRARY;
            _booksScanned = false;
            _librarySelectionOnlyRedraw = false;
            _needsRedraw = true;
            markProgressInactive();
            AppMgr::getInstance().switchTo(0);
        }
    }
}

bool AppReader::openBook(const String& path, bool restoreProgress) {
    String fullPath = "/ebooks" + path;
    closeBook(false);
    _currentBookPath = path;

    String pathLower = path;
    pathLower.toLowerCase();

    // 1. ENGINE ROUTING
    if (pathLower.endsWith(".kmb")) {
        Serial.println("AppReader: KMB detected, starting COMIC engine.");
        _isComicMode = true;
        _kbReader = new KBReader();

        if (!_kbReader->open(fullPath.c_str())) {
            delete _kbReader;
            _kbReader = nullptr;
            return false;
        }
        _totalPages = _kbReader->getPageCount();
        _globalPageNumber = 1;
        _currentPageRenderValid = false;

    } else {
        Serial.println("AppReader: EPUB detected, starting TEXT engine.");
        _isComicMode = false;
        _epubLoader = new EpubLoader();

        if (!_epubLoader->open(fullPath.c_str())) {
            delete _epubLoader;
            _epubLoader = nullptr;
            return false;
        }

        if (!_textRenderer) {
            DisplayMgr& dispMgr = DisplayMgr::getInstance();
            KomaBonDisplay& display = dispMgr.getDisplay();
            _textRenderer = new TextRenderer(display.width(), display.height(), _fontSizePt, _epubLoader);
        }
        _textRenderer->setFontSize(_fontSizePt);
        _textRenderer->setFontFamily(_fontFamily);
        _textRenderer->calculateDimensions();
        _globalPageNumber = 1;
        _currentPageRenderValid = false;
        startTotalPagesCounting();
    }

    // 2. STATE RESTORATION
    int restoreChapter = 0;
    PagePointer restorePointer = {0, 0};
    int restorePage = 1;
    String progressKey = getOriginalFilename(normalizedBookName(path));
    bool restored =
        restoreProgress && loadBookProgress(progressKey, restoreChapter, restorePointer, restorePage);

    if (_isComicMode) {
        if (restored) {
            _globalPageNumber = max(1, restorePage);
            if (_globalPageNumber > _totalPages) _globalPageNumber = _totalPages;
        }
    } else {
        loadChapter(restored ? restoreChapter : 0);
        if (restored && restoreChapter != _currentChapter) {
            Serial.printf("AppReader: saved chapter %d unavailable, starting at %d\n", restoreChapter,
                          _currentChapter);
        }
        if (restored && restoreChapter == _currentChapter) {
            int maxNode = (int)_currentRichContent.size();
            if (restorePointer.nodeIndex >= 0 && restorePointer.nodeIndex <= maxNode &&
                restorePointer.charOffset >= 0) {
                _currentPagePointer = restorePointer;
                _globalPageNumber = max(1, restorePage);
                _currentPageRenderValid = false;
            }
        }
    }

    _state = VIEW_READING;
    saveReadingProgress(true);
    flushProgress();
    _needsRedraw = true;
    return true;
}

bool AppReader::openSavedProgress() {
    ProgressStore& store = ProgressStore::getInstance();
    String last = store.lastBook();
    if (last.length() == 0) return false;

    // The stored key is the original (untruncated) name; map it back to the
    // file actually on flash.
    String filename = findFilenameForOriginal(last);
    if (filename.length() == 0) return false;

    return openBook("/" + filename, true);
}

bool AppReader::loadBookProgress(const String& originalName, int& chapter, PagePointer& pointer,
                                 int& globalPage) {
    BookProgress saved;
    if (!ProgressStore::getInstance().get(originalName, saved)) return false;

    chapter = saved.chapter;
    pointer.nodeIndex = saved.nodeIndex;
    pointer.charOffset = saved.charOffset;
    globalPage = saved.globalPage;
    return true;
}

void AppReader::saveReadingProgress(bool resumeOnBoot) {
    if (_currentBookPath.length() == 0 || _state != VIEW_READING) return;

    // Only marks as dirty; flushProgress() handles the actual saving.
    _progressDirty = true;
    _progressResumeOnBoot = resumeOnBoot;
    _lastProgressChangeMs = millis();
}

void AppReader::flushProgress() {
    if (!_progressDirty) return;
    _progressDirty = false;

    if (_currentBookPath.length() == 0 || _state != VIEW_READING) return;

    String key = getOriginalFilename(normalizedBookName(_currentBookPath));
    if (key.length() == 0) return;

    BookProgress p;
    p.chapter = _currentChapter;
    p.nodeIndex = _currentPagePointer.nodeIndex;
    p.charOffset = _currentPagePointer.charOffset;
    p.globalPage = _globalPageNumber;

    ProgressStore& store = ProgressStore::getInstance();
    store.set(key, p);
    store.setLast(key, _progressResumeOnBoot);
}

void AppReader::markProgressInactive() {
    ProgressStore::getInstance().setResumeOnBoot(false);
}

void AppReader::closeBook(bool markInactive) {
    if (markInactive && _state == VIEW_READING) {
        saveReadingProgress(false);
    }
    flushProgress();

    if (_epubLoader) {
        _epubLoader->close();
        delete _epubLoader;
        _epubLoader = nullptr;
    }
    if (_textRenderer) {
        delete _textRenderer;
        _textRenderer = nullptr;
    }
    if (_kbReader) { // NEW
        _kbReader->close();
        delete _kbReader;
        _kbReader = nullptr;
    }

    _isComicMode = false; // NEW
    _pageHistory.clear();
    _currentPageRenderValid = false;
    _countingActive = false;
    _countChapterContent.clear();
    if (_countRenderer) {
        delete _countRenderer;
        _countRenderer = nullptr;
    }
}

// Kicks off (or resumes from cache) the total page count for the book that
// just opened in _epubLoader/_currentBookPath. A cached total from a previous
// full count at the same font settings resolves this instantly; otherwise
// updateTotalPagesCount() walks the book from update(), a bounded slice at a
// time, until it reaches the end — resuming from the last completed chapter
// if an earlier session left a checkpoint (see updateTotalPagesCount).
void AppReader::startTotalPagesCounting() {
    _totalPages = 0;
    _countingActive = false;
    _countChapterContent.clear();
    _countChapter = 0;
    _countPointer = {0, 0};
    _countPagesSoFar = 0;
    if (_countRenderer) {
        delete _countRenderer;
        _countRenderer = nullptr;
    }

    if (!_epubLoader || _currentBookPath.length() == 0) return;

    String key = getOriginalFilename(normalizedBookName(_currentBookPath));
    int cached = PageCountStore::getInstance().get(key, _fontSizePt, _fontFamily);
    if (cached > 0) {
        _totalPages = cached;
        return;
    }

    PageCountCheckpoint checkpoint;
    if (PageCountStore::getInstance().getCheckpoint(key, _fontSizePt, _fontFamily, checkpoint)) {
        _countChapter = checkpoint.chapter;
        _countPagesSoFar = checkpoint.pagesSoFar;
    }
    _countingActive = true;
}

// Advances the total-page count by a time-boxed slice. Uses its own
// EpubLoader chapter reads and its own TextRenderer (_countRenderer) so it
// never touches the line cache or content the reading view is showing —
// paginating a chapter for counting is otherwise the exact same measurement
// nextPage() already does with draw=false.
//
// Standby (long-press KEY2) and the idle-sleep timeout both go straight to
// esp_deep_sleep_start() (see BatteryMgr::enterIdleSleep) without running
// closeBook() first, so a count in progress can be cut off at any moment with
// no chance to save. A checkpoint is written after every completed chapter
// instead, so the next session resumes close to where this one left off
// rather than recounting the whole book from chapter 0 again.
void AppReader::updateTotalPagesCount() {
    if (!_epubLoader) {
        _countingActive = false;
        return;
    }

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();

    if (!_countRenderer) {
        _countRenderer = new TextRenderer(display.width(), display.height(), _fontSizePt, _epubLoader);
        _countRenderer->setFontFamily(_fontFamily);
    }

    String key = getOriginalFilename(normalizedBookName(_currentBookPath));

    unsigned long budgetEnd = millis() + TOTAL_PAGES_BUDGET_MS;
    while (millis() < budgetEnd) {
        if (_countChapterContent.empty()) {
            if (_countChapter >= _epubLoader->getChapterCount()) {
                int total = max(1, _countPagesSoFar);
                _totalPages = total;
                PageCountStore::getInstance().set(key, _fontSizePt, _fontFamily, total);
                _countingActive = false;
                delete _countRenderer;
                _countRenderer = nullptr;
                return;
            }
            _countChapterContent = _epubLoader->getChapterContentRich(_countChapter);
            _countPointer = {0, 0};
            if (_countChapterContent.empty()) {
                _countChapter++;
                PageCountCheckpoint checkpoint;
                checkpoint.chapter = _countChapter;
                checkpoint.pagesSoFar = _countPagesSoFar;
                PageCountStore::getInstance().setCheckpoint(key, _fontSizePt, _fontFamily, checkpoint);
                continue;
            }
            _countPagesSoFar++; // First page of this chapter begins
        }

        RenderResult r = _countRenderer->renderRichPageDynamic(
            display, _countChapterContent, _countPointer.nodeIndex, _countPointer.charOffset, 0, 0, false);
        if (r.pageFull) {
            _countPagesSoFar++;
            _countPointer.nodeIndex = r.nextNodeIndex;
            _countPointer.charOffset = r.nextCharOffset;
        } else {
            _countChapterContent.clear();
            _countChapter++;
            PageCountCheckpoint checkpoint;
            checkpoint.chapter = _countChapter;
            checkpoint.pagesSoFar = _countPagesSoFar;
            PageCountStore::getInstance().setCheckpoint(key, _fontSizePt, _fontFamily, checkpoint);
        }
    }
}

void AppReader::loadChapter(int chapterIndex) {
    if (!_epubLoader) return;
    if (chapterIndex < 0 || chapterIndex >= _epubLoader->getChapterCount()) return;

    int originalIndex = chapterIndex;
    while (chapterIndex < _epubLoader->getChapterCount()) {
        _currentChapter = chapterIndex;
        _pageHistory.clear();
        _currentPagePointer = {0, 0};
        _currentPageRenderValid = false;

        _currentRichContent = _epubLoader->getChapterContentRich(chapterIndex);
        if (_currentRichContent.size() > 0) {
            if (_textRenderer) _textRenderer->clearCache();
            _needsRedraw = true;
            return;
        }
        chapterIndex++;
    }
    _currentChapter = originalIndex;
    _currentPageRenderValid = false;
    if (_textRenderer) _textRenderer->clearCache();
    _needsRedraw = true;
}

void AppReader::nextPage() {

    if (_isComicMode) {
        if (_globalPageNumber < _totalPages) {
            _globalPageNumber++;
            saveReadingProgress(true);
            _needsRedraw = true;
        }
        return;
    }

    if (!_textRenderer) return;

    RenderResult result = _currentPageRender;
    if (!_currentPageRenderValid) {
        DisplayMgr& dispMgr = DisplayMgr::getInstance();
        KomaBonDisplay& display = dispMgr.getDisplay();
        int currentPageNum = _pageHistory.size();
        result =
            _textRenderer->renderRichPageDynamic(display, _currentRichContent, _currentPagePointer.nodeIndex,
                                                 _currentPagePointer.charOffset, currentPageNum, 0, false);
    }

    if (result.pageFull) {
        // Save current position to history before advancing
        _pageHistory.push_back(_currentPagePointer);

        // Continue from the exact node/character where rendering stopped.
        _currentPagePointer.nodeIndex = result.nextNodeIndex;
        _currentPagePointer.charOffset = result.nextCharOffset;

        // Increment global page counter
        _globalPageNumber++;

        // Clear cache since we're moving to a new page
        _textRenderer->clearCache();
        _currentPageRenderValid = false;
        saveReadingProgress(true);
        _needsRedraw = true;
    } else {
        // End of chapter - advance to next
        if (_currentChapter < _epubLoader->getChapterCount() - 1) {
            // Save current chapter state to history
            _pageHistory.push_back(_currentPagePointer);
            _globalPageNumber++; // Next page in next chapter
            loadChapter(_currentChapter + 1);
            saveReadingProgress(true);
        }
        // If at end of book, do nothing
    }
}

void AppReader::prevPage() {

    if (_isComicMode) {
        if (_globalPageNumber > 1) {
            _globalPageNumber--;
            saveReadingProgress(true);
            _needsRedraw = true;
        }
        return;
    }

    if (!_pageHistory.empty()) {
        _currentPagePointer = _pageHistory.back();
        _pageHistory.pop_back();
        if (_globalPageNumber > 1) _globalPageNumber--; // Decrement global page counter
        if (_textRenderer) _textRenderer->clearCache();
        _currentPageRenderValid = false;
        saveReadingProgress(true);
        _needsRedraw = true;
    } else {
        // Go to previous chapter
        if (_currentChapter > 0) {
            // NOTE: Going to the "last page" of the previous chapter is tricky
            // because we don't know where it starts without rendering it.
            // For now, we go to the start of the previous chapter.
            if (_globalPageNumber > 1) _globalPageNumber--; // Decrement for prev chapter
            _currentPageRenderValid = false;
            prevChapter();
            saveReadingProgress(true);
        }
    }
}

void AppReader::nextChapter() {
    if (!_epubLoader) return;
    if (_currentChapter < _epubLoader->getChapterCount() - 1) loadChapter(_currentChapter + 1);
}

void AppReader::prevChapter() {
    if (!_epubLoader) return;
    if (_currentChapter > 0) {
        int tryChapter = _currentChapter - 1;
        while (tryChapter >= 0) {
            String chapterText = _epubLoader->getChapterContent(tryChapter);
            if (chapterText.length() > 0) {
                loadChapter(tryChapter);
                return;
            }
            tryChapter--;
        }
    }
}

void AppReader::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;
    if (_state == VIEW_LIBRARY)
        drawLibrary();
    else
        drawReading();
}

// Keeps the selected book row inside the visible window, scrolling the list
// when the selection moves past its top or bottom edge. The "Back to Menu"
// row (-1) sits above the scrolling area and is always visible, so it leaves
// the current window untouched.
void AppReader::updateLibraryScroll() {
    if (_selectedBookIndex < 0) return;

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    int itemsPerPage = libraryItemsPerPage(display.height());
    if (itemsPerPage <= 0) return;

    if (_selectedBookIndex < _libraryScrollOffset) {
        _libraryScrollOffset = _selectedBookIndex;
        _librarySelectionOnlyRedraw = false; // Window shifted: repaint the whole list
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
    // The book count can shrink between scans (book deleted via web UI while
    // the reader was open); keep the scroll window from pointing past the end.
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

    // Use Partial Refresh for Library interactions
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

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        drawTextWithFont(display, "Library", 20, 40, &FreeSansBold12pt8b, GxEPD_BLACK);
        char countText[24];
        snprintf(countText, sizeof(countText), "%d books", (int)_books.size());
        fontMgr.drawTextRight(display, countText, display.width() - 20, 38, FONT_SIZE_SMALL, GxEPD_BLACK);
        display.drawFastHLine(20, 56, display.width() - 40, GxEPD_BLACK);
        display.drawFastHLine(20, 58, 72, GxEPD_BLACK);

        int y = HEADER_H;

        // === "Back to Menu" option (index -1) ===
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

        // === Book list ===
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
                drawBookTile(display, coverX, coverY, coverW, coverH, isSelected);
                if (isSelected) {
                    display.drawRect(coverX - 3, coverY - 3, coverW + 6, coverH + 6, GxEPD_BLACK);
                    display.drawRect(coverX - 2, coverY - 2, coverW + 4, coverH + 4, GxEPD_BLACK);
                }

                uint16_t textColor = GxEPD_BLACK;

                // Draw book title with word wrapping
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

                // v1.8.0: saved position. No percentage: paginating the whole
                // book is too slow to do on open, so a percentage would be
                // made up.
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

        // Page indicator (14px) - show current selection
        char pageStr[24];
        if (_selectedBookIndex == -1) {
            snprintf(pageStr, sizeof(pageStr), "Menu");
        } else {
            snprintf(pageStr, sizeof(pageStr), "%d/%d", _selectedBookIndex + 1, (int)_books.size());
        }
        display.drawFastHLine(20, display.height() - 42, display.width() - 40, GxEPD_BLACK);

        // Testo aggiornato per riflettere i comandi del nuovo Joystick a 5 vie
        fontMgr.drawText(display, "Joy: Move  |  Center: Open  |  Hold Left: Menu", 22, display.height() - 18,
                         FONT_SIZE_SMALL, GxEPD_BLACK);

        fontMgr.drawTextRight(display, pageStr, display.width() - 20, display.height() - 18, FONT_SIZE_SMALL,
                              GxEPD_BLACK);

    } while (display.nextPage());
}

void AppReader::drawReading() {
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

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // 1. CONTENT RENDERING
        if (_isComicMode) {
            // Calculate buffer size needed for the 1bpp image
            size_t bufferSize = (_kbReader->getWidth() + 7) / 8 * _kbReader->getHeight();
            uint8_t* pageBuffer = (uint8_t*)ps_malloc(bufferSize);

            if (pageBuffer) {
                // Read and decompress directly into buffer
                if (_kbReader->readPage(_globalPageNumber - 1, pageBuffer)) {
                    // Draw decompressed bitmap to screen
                    display.drawBitmap(0, 0, pageBuffer, _kbReader->getWidth(), _kbReader->getHeight(),
                                       GxEPD_BLACK);
                }
                free(pageBuffer);
            }
        } else {
            _currentPageRender = _textRenderer->renderRichPageDynamic(
                display, _currentRichContent, _currentPagePointer.nodeIndex, _currentPagePointer.charOffset,
                currentPageNum, _globalPageNumber, true);
            _currentPageRenderValid = true;
        }

        // 2. FOOTER RENDERING
        display.setFont(NULL);
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

        // Solid white background behind text to ensure readability over comic art
        if (_isComicMode) {
            display.fillRect(cursorX - 2, cursorY - fh - 2, fw + 4, fh + 4, GxEPD_WHITE);
        }

        display.setCursor(cursorX, cursorY);
        display.print(footerText);

    } while (display.nextPage());
}

void AppReader::update() {
    // Library rendering is static unless input changes selection.

    // Spend a small time-boxed slice counting more of the open book's total
    // pages, if it isn't already known. Runs between draw() calls only (see
    // updateTotalPagesCount), so it never overlaps an actual page render.
    if (_countingActive) updateTotalPagesCount();

    // Commit a deferred reading position once the page has been still for a
    // while. Page turns only mark it dirty (see saveReadingProgress), so a
    // burst of turns costs one write instead of one per page.
    if (_progressDirty && (millis() - _lastProgressChangeMs) >= PROGRESS_FLUSH_DELAY_MS) {
        flushProgress();
    }
}

void AppReader::applyFontSize(int pt) {
    int normalized = (pt >= 18) ? 18 : (pt >= 12 ? 12 : 9);
    _fontSizePt = normalized;
    if (_textRenderer) _textRenderer->setFontSize(normalized);

    // Re-render the current page from its saved start pointer at the new size.
    // The pointer is a content position (node + char offset), so it's font-size
    // independent; the renderer recomputes where this page ends and the next
    // begins, keeping word-wrap and page breaks consistent.
    _currentPageRenderValid = false;
    _readingFirstDraw = true; // Full refresh to clear the old layout cleanly
    _pageTurnsSinceRefresh = 0;
    _needsRedraw = true;

    // The total page count is font-size dependent; re-derive it for the new
    // size (a no-op if a cached total already exists at this size).
    startTotalPagesCounting();
}

void AppReader::applyFontFamily(int family) {
    int normalized =
        (family >= READER_FONT_SANS && family <= READER_FONT_OPEN_SANS) ? family : READER_FONT_SANS;
    _fontFamily = normalized;
    if (_textRenderer) _textRenderer->setFontFamily(normalized);

    // Re-render the current page from its saved start pointer with the new
    // family. Same rationale as applyFontSize: the pointer is a content
    // position, so pagination just recomputes with the new glyph metrics.
    _currentPageRenderValid = false;
    _readingFirstDraw = true; // Full refresh to clear the old layout cleanly
    _pageTurnsSinceRefresh = 0;
    _needsRedraw = true;

    // Same reasoning as applyFontSize: the total is specific to this family.
    startTotalPagesCounting();
}

void AppReader::forceRedraw() {
    _librarySelectionOnlyRedraw = false; // Repaint the whole library view
    _currentPageRenderValid = false;
    _readingFirstDraw = true; // Repaint the whole reading view
    _needsRedraw = true;
}