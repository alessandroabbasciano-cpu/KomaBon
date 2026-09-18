#include "AppReader.h"
#include "DisplayMgr.h"
#include "PageCountStore.h"
#include "BookMeta.h"

void AppReader::pageCountTask(void* param) {
    AppReader* app = static_cast<AppReader*>(param);
    while (!app->_killPageCountTask && app->_countingActive) {
        app->updateTotalPagesCount();
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    app->_pageCountTaskHandle = nullptr;
    vTaskDelete(NULL);
}

void AppReader::startTotalPagesCounting() {
    if (_pageCountTaskHandle != nullptr) {
        _killPageCountTask = true;
        vTaskDelay(pdMS_TO_TICKS(30));
    }

    _killPageCountTask = false;
    _totalPages = 0;
    _countingActive = false;
    _countChapterContent.clear();
    _countChapter = 0;
    _countPointer = {0, 0};
    _countPagesSoFar = 0;

    // Initialize mapping vector. Chapter 0 always starts at absolute page 1.
    _chapterStartPages.clear();
    _chapterStartPages.push_back(1);

    if (_countRenderer) {
        Book32Guard guard(_epubMutex);
        delete _countRenderer;
        _countRenderer = nullptr;
    }

    if (!_epubLoader || _currentBookPath.length() == 0) return;

    String key = getOriginalFilename(normalizedBookName(_currentBookPath));
    int cached = PageCountStore::getInstance().get(key, _fontSizePt, _fontFamily);
    if (cached > 0) {
        _totalPages = cached;
        // Optimization: Let the background task reconstruct the vector silently
        // even if the total page count is cached, to ensure TOC mapping is active.
    }

    _countingActive = true;
    xTaskCreatePinnedToCore(AppReader::pageCountTask, "PageCountTask", 16384, this, 1, &_pageCountTaskHandle,
                            0);
}

void AppReader::updateTotalPagesCount() {
    if (!_countingActive || _killPageCountTask) return;

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();

    if (!_countRenderer) {
        Book32Guard guard(_epubMutex);
        if (!_epubLoader) return;
        _countRenderer = new TextRenderer(display.width(), display.height(), _fontSizePt, _epubLoader);
        _countRenderer->setFontFamily(_fontFamily);
    }

    String key = getOriginalFilename(normalizedBookName(_currentBookPath));

    if (_countChapterContent.empty()) {
        Book32Guard guard(_epubMutex);
        if (!_epubLoader) return;

        if (_countChapter >= _epubLoader->getChapterCount()) {
            int total = std::max(1, _countPagesSoFar);
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
            // Map the skipped empty chapter to the current page boundary
            _chapterStartPages.push_back(_countPagesSoFar + 1);
            return;
        }
        _countPagesSoFar++;
    }

    RenderResult r;
    {
        Book32Guard guard(_epubMutex);
        if (!_countRenderer) return;
        r = _countRenderer->renderRichPageDynamic(display, _countChapterContent, _countPointer.nodeIndex,
                                                  _countPointer.charOffset, 0, 0, false);
    }

    if (r.pageFull) {
        _countPagesSoFar++;
        _countPointer.nodeIndex = r.nextNodeIndex;
        _countPointer.charOffset = r.nextCharOffset;
    } else {
        _countChapterContent.clear();
        _countChapter++;

        // NEW: Record the absolute starting page for the next chapter mapped in RAM
        _chapterStartPages.push_back(_countPagesSoFar + 1);
    }
}

void AppReader::loadChapter(int chapterIndex) {
    Book32Guard guard(_epubMutex);

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

    Book32Guard guard(_epubMutex);
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

    Book32Guard guard(_epubMutex);

    if (!_pageHistory.empty()) {
        _currentPagePointer = _pageHistory.back();
        _pageHistory.pop_back();
        if (_globalPageNumber > 1) _globalPageNumber--;
        if (_textRenderer) _textRenderer->clearCache();
        _currentPageRenderValid = false;
        saveReadingProgress(true);
        _needsRedraw = true;
    } else if (_currentChapter > 0) {
        int prevChap = _currentChapter - 1;

        while (prevChap >= 0) {
            if (!_epubLoader->getChapterContentRich(prevChap).empty()) break;
            prevChap--;
        }

        if (prevChap >= 0) {
            loadChapter(prevChap);
            DisplayMgr& dispMgr = DisplayMgr::getInstance();
            KomaBonDisplay& display = dispMgr.getDisplay();

            while (true) {
                RenderResult r = _textRenderer->renderRichPageDynamic(
                    display, _currentRichContent, _currentPagePointer.nodeIndex,
                    _currentPagePointer.charOffset, _pageHistory.size(), 0, false);

                if (r.pageFull) {
                    _pageHistory.push_back(_currentPagePointer);
                    _currentPagePointer.nodeIndex = r.nextNodeIndex;
                    _currentPagePointer.charOffset = r.nextCharOffset;
                } else {
                    break;
                }
            }

            if (_globalPageNumber > 1) _globalPageNumber--;
            if (_textRenderer) _textRenderer->clearCache();
            _currentPageRenderValid = false;
            saveReadingProgress(true);
            _needsRedraw = true;
        }
    }
}

void AppReader::nextChapter() {
    Book32Guard guard(_epubMutex);
    if (!_epubLoader) return;
    if (_currentChapter < _epubLoader->getChapterCount() - 1) loadChapter(_currentChapter + 1);
}

void AppReader::prevChapter() {
    Book32Guard guard(_epubMutex);
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

void AppReader::applyFontSize(int pt) {
    Book32Guard guard(_epubMutex);
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
    Book32Guard guard(_epubMutex);
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