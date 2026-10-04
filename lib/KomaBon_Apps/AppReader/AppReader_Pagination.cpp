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
        // Wait up to 500ms for the task to cleanly exit and null its own handle
        int timeout = 50;
        while (_pageCountTaskHandle != nullptr && timeout > 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            timeout--;
        }
        if (_pageCountTaskHandle != nullptr) {
            // Force kill if it's deadlocked
            vTaskDelete(_pageCountTaskHandle);
            _pageCountTaskHandle = nullptr;
        }
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
        KomaBonGuard guard(_epubMutex);
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
        KomaBonGuard guard(_epubMutex);
        if (!_epubLoader) return;
        _countRenderer = new TextRenderer(display.width(), display.height(), _fontSizePt, _epubLoader);
        _countRenderer->setFontFamily(_fontFamily);
    }

    String key = getOriginalFilename(normalizedBookName(_currentBookPath));

    if (_countChapterContent.empty()) {
        String rawHtml;
        String chapterDir;
        {
            KomaBonGuard guard(_epubMutex);
            if (!_epubLoader) return;

            if (_countChapter >= _epubLoader->getChapterCount()) {
                int total = std::max(1, _countPagesSoFar);
                _totalPages = total;
                PageCountStore::getInstance().set(key, _fontSizePt, _fontFamily, total);
                _countingActive = false;
                delete _countRenderer;
                _countRenderer = nullptr;
                _needsRedraw = true; // NEW: Update the footer in the UI
                return;
            }

            rawHtml = _epubLoader->getChapterRawHtml(_countChapter, chapterDir);
        }

        if (_killPageCountTask) return;

        // Parse WITHOUT the lock to avoid freezing the UI for seconds on large chapters!
        _countChapterContent = HtmlParser::parseHtmlToRichContent(rawHtml, chapterDir, &_killPageCountTask);
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
        KomaBonGuard guard(_epubMutex);
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
    KomaBonGuard guard(_epubMutex);

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
            // Find chapter title from the first header or prominent text node
            _currentChapterTitle = "";
            for (const auto& node : _currentRichContent) {
                if (node.type == CONTENT_TEXT) {
                    String trimmed = node.textNode.text;
                    trimmed.trim();
                    if (trimmed.length() == 0) continue;

                    // Prefer H1, H2, H3 headers
                    if (node.textNode.style == STYLE_HEADER1 || node.textNode.style == STYLE_HEADER2 ||
                        node.textNode.style == STYLE_HEADER3) {
                        _currentChapterTitle = trimmed;
                        break;
                    }
                    // Fallback to first non-empty text if short enough to be a title
                    if (_currentChapterTitle.length() == 0 && trimmed.length() <= 50) {
                        _currentChapterTitle = trimmed;
                    }
                }
            }

            // Fallback if no header was found
            if (_currentChapterTitle.length() == 0) {
                char buf[32];
                snprintf(buf, sizeof(buf), "Chapter %d", chapterIndex + 1);
                _currentChapterTitle = buf;
            }

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

    KomaBonGuard guard(_epubMutex);
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

    KomaBonGuard guard(_epubMutex);

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

        String dummy;
        while (prevChap >= 0) {
            if (_epubLoader->getChapterRawHtml(prevChap, dummy).length() > 0) break;
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
    KomaBonGuard guard(_epubMutex);
    if (!_epubLoader) return;
    if (_currentChapter < _epubLoader->getChapterCount() - 1) loadChapter(_currentChapter + 1);
}

void AppReader::prevChapter() {
    KomaBonGuard guard(_epubMutex);
    if (!_epubLoader) return;
    if (_currentChapter > 0) {
        int tryChapter = _currentChapter - 1;
        String dummy;
        while (tryChapter >= 0) {
            if (_epubLoader->getChapterRawHtml(tryChapter, dummy).length() > 0) {
                loadChapter(tryChapter);
                return;
            }
            tryChapter--;
        }
    }
}

void AppReader::applyFontSize(int pt) {
    int normalized = SettingsStore::clampFontSize(pt);
    {
        KomaBonGuard guard(_epubMutex);
        _fontSizePt = normalized;
        if (_textRenderer) _textRenderer->setFontSize(normalized);
        _currentPageRenderValid = false;
        _readingFirstDraw = true;
        _pageTurnsSinceRefresh = 0;
        _needsRedraw = true;
    }
    startTotalPagesCounting();
}

void AppReader::applyFontFamily(int family) {
    int normalized =
        (family >= READER_FONT_SANS && family <= READER_FONT_OPEN_SANS) ? family : READER_FONT_SANS;
    {
        KomaBonGuard guard(_epubMutex);
        _fontFamily = normalized;
        if (_textRenderer) _textRenderer->setFontFamily(normalized);
        _currentPageRenderValid = false;
        _readingFirstDraw = true;
        _pageTurnsSinceRefresh = 0;
        _needsRedraw = true;
    }
    startTotalPagesCounting();
}
