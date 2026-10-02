#include "AppReader.h"
#include "AppMgr.h"
#include "KomaBonFS.h"
#include "ProgressStore.h"
#include "SDMgr.h"
#include "BookMeta.h"
#include "DisplayMgr.h"
#include "BatteryMgr.h"
#include <WiFi.h>
#include <ArduinoJson.h>

AppReader::AppReader() {
    _resumeSavedBookOnStart = false;
    _epubLoader = nullptr;
    _textRenderer = nullptr;
    _kbReader = nullptr;
    _isComicMode = false;
    _isRTL = false;
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

    _overlaySelectedIndex = 0;
    _overlayScrollOffset = 0;
    _settingsChanged = false;
    _state = VIEW_READING;

    loadSettings();
}

AppReader::~AppReader() {
    closeBook(false);
    if (_epubLoader) delete _epubLoader;
    if (_textRenderer) delete _textRenderer;
    if (_kbReader) delete _kbReader;
}

void AppReader::loadSettings() {
    File file;
    if (SystemFS.exists("/reader_config.json")) {
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

bool AppReader::hasBootResume() {
    ProgressStore& store = ProgressStore::getInstance();
    return store.resumeOnBoot() && (store.lastBook().length() > 0);
}
void AppReader::resumeSavedBookOnStart() {
    _resumeSavedBookOnStart = true;
}

bool AppReader::handleSleep() {
    if (_state == VIEW_READING) {
        enterSleepMode();
        return true;
    }
    return false;
}

void AppReader::enterSleepMode() {
    Serial.println("AppReader: Entering deep sleep from reading mode...");
    saveReadingProgress(true);
    flushProgress();
    drawSleepCover();
    BatteryMgr::getInstance().prepareAndEnterDeepSleep();
}

void AppReader::start() {
    DisplayMgr::getInstance().disableFastRefreshA2();
    // Force offline mode to guarantee battery efficiency
    if (WiFi.getMode() != WIFI_OFF) {
        delay(50);
        WiFi.disconnect(false);
        WiFi.mode(WIFI_OFF);
        delay(300);
        Serial.println("AppReader: Wi-Fi powered down strictly for reading session.");
    }

    loadSettings();
    if (_textRenderer) {
        KomaBonGuard guard(_epubMutex);
        _textRenderer->setFontSize(_fontSizePt);
        _textRenderer->setFontFamily(_fontFamily);
    }

    _needsRedraw = true;
    _state = VIEW_READING;
    InputMgr::getInstance().setCallback(std::bind(&AppReader::handleInput, this, std::placeholders::_1));
    if (_resumeSavedBookOnStart) {
        _resumeSavedBookOnStart = false;
        if (!openSavedProgress()) markProgressInactive();
    }
}

void AppReader::stop() {
    closeBook();
    InputMgr::getInstance().clearCallback();
}

void AppReader::update() {
    if (_progressDirty && (millis() - _lastProgressChangeMs) >= PROGRESS_FLUSH_DELAY_MS) {
        flushProgress();
    }
}
void AppReader::forceRedraw() {
    _currentPageRenderValid = false;
    _readingFirstDraw = true;
    _needsRedraw = true;
}

bool AppReader::openBook(const String& path, bool restoreProgress) {
    if (!SDMgr::getInstance().isMounted()) return false;

    String fullPath = "/ebooks" + path;
    closeBook(false);
    _currentBookPath = path;
    _isRTL = false;

    String pathLower = path;
    pathLower.toLowerCase();

    if (pathLower.endsWith(".kmb")) {
        _isComicMode = true;
        _kbReader = new KBReader();

        if (!_kbReader->open(path.c_str())) {
            delete _kbReader;
            _kbReader = nullptr;
            return false;
        }

        size_t bufferSize = (_kbReader->getWidth() + 7) / 8 * _kbReader->getHeight();
        _comicPageBuffer = (uint8_t*)ps_malloc(bufferSize);

        if (!_comicPageBuffer) {
            Serial.println("AppReader: FATAL - PSRAM allocation failed for KMB buffer.");
            delete _kbReader;
            _kbReader = nullptr;
            return false;
        }

        _totalPages = _kbReader->getPageCount();
        _globalPageNumber = 1;
        _currentPageRenderValid = false;

    } else {
        _isComicMode = false;
        KomaBonGuard guard(_epubMutex);
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
    }

    int restoreChapter = 0;
    PagePointer restorePointer = {0, 0};
    int restorePage = 1;
    bool restoreRtl = false;
    String progressKey = getOriginalFilename(normalizedBookName(path));
    bool restored = restoreProgress &&
                    loadBookProgress(progressKey, restoreChapter, restorePointer, restorePage, restoreRtl);
    if (restored) _isRTL = restoreRtl;

    if (_isComicMode) {
        if (restored) {
            _globalPageNumber = std::max(1, restorePage);
            if (_globalPageNumber > _totalPages) _globalPageNumber = _totalPages;
        }
    } else {
        loadChapter(restored ? restoreChapter : 0);
        if (restored && restoreChapter == _currentChapter) {
            int maxNode = (int)_currentRichContent.size();
            if (restorePointer.nodeIndex >= 0 && restorePointer.nodeIndex <= maxNode &&
                restorePointer.charOffset >= 0) {
                _currentPagePointer = restorePointer;
                _globalPageNumber = std::max(1, restorePage);
                _currentPageRenderValid = false;
            }
        }
    }

    _state = VIEW_READING;
    saveReadingProgress(true);
    flushProgress();
    _needsRedraw = true;

    if (!_isComicMode) startTotalPagesCounting();

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
                                 int& globalPage, bool& rtl) {
    BookProgress saved;
    if (!ProgressStore::getInstance().get(originalName, saved)) return false;

    chapter = saved.chapter;
    pointer.nodeIndex = saved.nodeIndex;
    pointer.charOffset = saved.charOffset;
    globalPage = saved.globalPage;
    rtl = saved.rtl;
    return true;
}

void AppReader::saveReadingProgress(bool resumeOnBoot) {
    if (_currentBookPath.length() == 0) return;
    _progressDirty = true;
    _progressResumeOnBoot = resumeOnBoot;
    _lastProgressChangeMs = millis();
}

void AppReader::flushProgress() {
    if (_currentBookPath.length() == 0) return;
    if (!_progressDirty) return;
    _progressDirty = false;

    String key = getOriginalFilename(normalizedBookName(_currentBookPath));
    if (key.length() == 0) return;

    BookProgress p;
    p.chapter = _currentChapter;
    p.nodeIndex = _currentPagePointer.nodeIndex;
    p.charOffset = _currentPagePointer.charOffset;
    p.globalPage = _globalPageNumber;
    p.rtl = _isRTL;

    ProgressStore& store = ProgressStore::getInstance();
    store.set(key, p);
    store.setLast(key, _progressResumeOnBoot);
}

void AppReader::markProgressInactive() {
    ProgressStore::getInstance().setResumeOnBoot(false);
}

void AppReader::closeBook(bool markInactive) {
    if (markInactive) saveReadingProgress(false);
    _killPageCountTask = true;
    _countingActive = false;
    if (_pageCountTaskHandle != nullptr) {
        int timeout = 50;
        while (_pageCountTaskHandle != nullptr && timeout > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            timeout--;
        }
        if (_pageCountTaskHandle != nullptr) {
            vTaskDelete(_pageCountTaskHandle);
            _pageCountTaskHandle = nullptr;
        }
    }

    flushProgress();
    _currentBookPath = ""; // Prevent stale progress writes on double-calls

    KomaBonGuard guard(_epubMutex);

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
    if (_comicPageBuffer) {
        free(_comicPageBuffer);
        _comicPageBuffer = nullptr;
    }

    _isComicMode = false;
    _pageHistory.clear();
    _currentPageRenderValid = false;
    _countChapterContent.clear();
    if (_countRenderer) {
        delete _countRenderer;
        _countRenderer = nullptr;
    }
}
