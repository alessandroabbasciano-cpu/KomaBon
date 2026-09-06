#include "AppReader.h"
#include "CoverExtractor.h"
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
#include "Fonts/FreeSans.h"
#include <map>

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
    _kbReader = nullptr;
    _isComicMode = false;
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
    _refreshEveryNPages = 10;
    _fontSizePt = 9;
    _fontFamily = READER_FONT_SANS;
    _readingFirstDraw = true;
    _progressDirty = false;
    _progressResumeOnBoot = false;
    _lastProgressChangeMs = 0;

    loadSettings();
}

void AppReader::loadSettings() {
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

void AppReader::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;
    Serial.printf("AppReader::handleInput - action: %d, state: %d\n", action, _state);
    if (_state == VIEW_LIBRARY) {
        int maxIndex = (int)_books.size() - 1;
        if (action == INPUT_NEXT) {
            _previousBookIndex = _selectedBookIndex;
            _selectedBookIndex++;
            if (_selectedBookIndex > maxIndex) _selectedBookIndex = -1;
            _librarySelectionOnlyRedraw = _booksScanned;
            updateLibraryScroll();
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _previousBookIndex = _selectedBookIndex;
            _selectedBookIndex--;
            if (_selectedBookIndex < -1) _selectedBookIndex = maxIndex;
            _librarySelectionOnlyRedraw = _booksScanned;
            updateLibraryScroll();
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_selectedBookIndex == -1) {
                markProgressInactive();
                AppMgr::getInstance().switchTo(0);
            } else if (!_books.empty() && _selectedBookIndex >= 0) {
                openBook(_books[_selectedBookIndex].path.c_str());
            }
        } else if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU) {
            markProgressInactive();
            AppMgr::getInstance().switchTo(0);
        }
    } else if (_state == VIEW_READING) {
        if (action == INPUT_NEXT)
            nextPage();
        else if (action == INPUT_PREV)
            prevPage();
        else if (action == INPUT_SELECT || action == INPUT_BACK) {
            closeBook();
            _state = VIEW_LIBRARY;
            _booksScanned = false;
            _librarySelectionOnlyRedraw = false;
            _needsRedraw = true;
        } else if (action == INPUT_GO_TO_MAIN_MENU) {
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

    if (pathLower.endsWith(".kmb")) {
        Serial.println("AppReader: KMB detected, starting COMIC engine.");
        _isComicMode = true;
        _kbReader = new KBReader();

        if (!_kbReader->open(path.c_str())) {
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
    if (_kbReader) {
        _kbReader->close();
        delete _kbReader;
        _kbReader = nullptr;
    }

    _isComicMode = false;
    _pageHistory.clear();
    _currentPageRenderValid = false;
    _countingActive = false;
    _countChapterContent.clear();
    if (_countRenderer) {
        delete _countRenderer;
        _countRenderer = nullptr;
    }
}

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
            _countPagesSoFar++;
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
        _pageHistory.push_back(_currentPagePointer);
        _currentPagePointer.nodeIndex = result.nextNodeIndex;
        _currentPagePointer.charOffset = result.nextCharOffset;
        _globalPageNumber++;
        _textRenderer->clearCache();
        _currentPageRenderValid = false;
        saveReadingProgress(true);
        _needsRedraw = true;
    } else {
        if (_currentChapter < _epubLoader->getChapterCount() - 1) {
            _pageHistory.push_back(_currentPagePointer);
            _globalPageNumber++;
            loadChapter(_currentChapter + 1);
            saveReadingProgress(true);
        }
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
        if (_globalPageNumber > 1) _globalPageNumber--;
        if (_textRenderer) _textRenderer->clearCache();
        _currentPageRenderValid = false;
        saveReadingProgress(true);
        _needsRedraw = true;
    } else {
        if (_currentChapter > 0) {
            if (_globalPageNumber > 1) _globalPageNumber--;
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

        if (_isComicMode) {
            size_t bufferSize = (_kbReader->getWidth() + 7) / 8 * _kbReader->getHeight();
            uint8_t* pageBuffer = (uint8_t*)ps_malloc(bufferSize);

            if (pageBuffer) {
                if (_kbReader->readPage(_globalPageNumber - 1, pageBuffer)) {
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

        if (_isComicMode) {
            display.fillRect(cursorX - 2, cursorY - fh - 2, fw + 4, fh + 4, GxEPD_WHITE);
        }

        display.setCursor(cursorX, cursorY);
        display.print(footerText);

    } while (display.nextPage());
}

void AppReader::update() {
    if (_state == VIEW_LIBRARY) {
        if (CoverExtractor::processNextCover(_books)) {
            _librarySelectionOnlyRedraw = false;
            _needsRedraw = true;
        }
    }

    if (_countingActive) updateTotalPagesCount();

    if (_progressDirty && (millis() - _lastProgressChangeMs) >= PROGRESS_FLUSH_DELAY_MS) {
        flushProgress();
    }
}

void AppReader::applyFontSize(int pt) {
    int normalized = (pt >= 18) ? 18 : (pt >= 12 ? 12 : 9);
    _fontSizePt = normalized;
    if (_textRenderer) _textRenderer->setFontSize(normalized);

    _currentPageRenderValid = false;
    _readingFirstDraw = true;
    _pageTurnsSinceRefresh = 0;
    _needsRedraw = true;

    startTotalPagesCounting();
}

void AppReader::applyFontFamily(int family) {
    int normalized =
        (family >= READER_FONT_SANS && family <= READER_FONT_OPEN_SANS) ? family : READER_FONT_SANS;
    _fontFamily = normalized;
    if (_textRenderer) _textRenderer->setFontFamily(normalized);

    _currentPageRenderValid = false;
    _readingFirstDraw = true;
    _pageTurnsSinceRefresh = 0;
    _needsRedraw = true;

    startTotalPagesCounting();
}

void AppReader::forceRedraw() {
    _librarySelectionOnlyRedraw = false;
    _currentPageRenderValid = false;
    _readingFirstDraw = true;
    _needsRedraw = true;
}