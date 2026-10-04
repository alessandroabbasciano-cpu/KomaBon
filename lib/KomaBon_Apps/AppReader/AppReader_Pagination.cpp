#include "AppReader.h"
#include "DisplayMgr.h"
#include "PageCountStore.h"
#include "BookMeta.h"
#include "BookmarkStore.h"

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
    int cached = PageCountStore::getInstance().get(key, _fontSizePt, _fontFamily + _margin * 16);
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
        _countRenderer->setMargin(_margin);
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
                PageCountStore::getInstance().set(key, _fontSizePt, _fontFamily + _margin * 16, total);
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

void AppReader::applyMargin(int margin) {
    int normalized = SettingsStore::clampMargin(margin);
    {
        KomaBonGuard guard(_epubMutex);
        _margin = normalized;
        if (_textRenderer) _textRenderer->setMargin(normalized);
        _currentPageRenderValid = false;
        _readingFirstDraw = true;
        _pageTurnsSinceRefresh = 0;
        _needsRedraw = true;
    }
    startTotalPagesCounting();
}

void AppReader::applyJustify(bool justify) {
    {
        KomaBonGuard guard(_epubMutex);
        _justifyText = justify;
        if (_textRenderer) _textRenderer->setJustify(justify);
        _currentPageRenderValid = false;
        _readingFirstDraw = true;
        _pageTurnsSinceRefresh = 0;
        _needsRedraw = true;
    }
}

void AppReader::goToPage(int targetPage) {
    if (targetPage < 1) targetPage = 1;
    if (_totalPages > 0 && targetPage > _totalPages) targetPage = _totalPages;

    if (_isComicMode) {
        _globalPageNumber = targetPage;
        saveReadingProgress(true);
        flushProgress();
        forceRedraw();
        return;
    }

    KomaBonGuard guard(_epubMutex);
    if (!_epubLoader || !_textRenderer) return;

    // In EPUB mode, find which chapter contains targetPage
    int targetChapter = 0;
    if (!_chapterStartPages.empty()) {
        for (int i = (int)_chapterStartPages.size() - 1; i >= 0; i--) {
            if (targetPage >= _chapterStartPages[i]) {
                targetChapter = i;
                break;
            }
        }
    } else {
        // If chapter start mapping is not ready yet, approximate by percentage
        int totalChaps = _epubLoader->getChapterCount();
        if (_totalPages > 0 && totalChaps > 0) {
            targetChapter = ((targetPage - 1) * totalChaps) / _totalPages;
            if (targetChapter >= totalChaps) targetChapter = totalChaps - 1;
        }
    }

    loadChapter(targetChapter);

    int startPageForChap = 1;
    if (targetChapter < (int)_chapterStartPages.size()) {
        startPageForChap = _chapterStartPages[targetChapter];
    }
    _globalPageNumber = startPageForChap;

    // Advance within the chapter until we hit targetPage or end of chapter
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();

    while (_globalPageNumber < targetPage) {
        RenderResult r = _textRenderer->renderRichPageDynamic(
            display, _currentRichContent, _currentPagePointer.nodeIndex, _currentPagePointer.charOffset,
            _pageHistory.size(), 0, false);

        if (r.pageFull) {
            _pageHistory.push_back(_currentPagePointer);
            _currentPagePointer.nodeIndex = r.nextNodeIndex;
            _currentPagePointer.charOffset = r.nextCharOffset;
            _globalPageNumber++;
        } else {
            break; // End of chapter reached
        }
    }

    if (_textRenderer) _textRenderer->clearCache();
    _currentPageRenderValid = false;
    saveReadingProgress(true);
    flushProgress();
    forceRedraw();
}

bool AppReader::isCurrentPageBookmarked() {
    if (_currentBookPath.length() == 0) return false;
    String key = getOriginalFilename(normalizedBookName(_currentBookPath));
    return BookmarkStore::getInstance().isBookmarked(key, _globalPageNumber);
}

static const int BOOKMARK_RIBBON_X = 24;
static const int BOOKMARK_RIBBON_W = 18;
static const int BOOKMARK_RIBBON_H = 26;
static const int BOOKMARK_RIBBON_CUT = 7;

void AppReader::drawBookmarkIndicator(KomaBonDisplay& display) {
    if (isCurrentPageBookmarked()) {
        int x = BOOKMARK_RIBBON_X;
        int w = BOOKMARK_RIBBON_W;
        int h = BOOKMARK_RIBBON_H;
        int cut = BOOKMARK_RIBBON_CUT;
        display.fillRect(x, 0, w, h, GxEPD_BLACK);
        display.fillTriangle(x, h, x + w, h, x + w / 2, h - cut, GxEPD_WHITE);
    }
}

void AppReader::toggleCurrentBookmark() {
    if (_currentBookPath.length() == 0) return;
    String key = getOriginalFilename(normalizedBookName(_currentBookPath));

    BookmarkEntry entry;
    entry.page = _globalPageNumber;
    entry.chapter = _currentChapter;
    entry.nodeIndex = _currentPagePointer.nodeIndex;
    entry.charOffset = _currentPagePointer.charOffset;

    bool added = BookmarkStore::getInstance().toggleBookmark(key, entry);
    Serial.printf("Bookmark: page %d %s for %s\n", _globalPageNumber, added ? "ADDED" : "REMOVED",
                  key.c_str());

    // Instant redraw of the ribbon indicator on e-ink (top-left margin)
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    KomaBonDisplay& display = dispMgr.getDisplay();
    int x = BOOKMARK_RIBBON_X;
    int w = BOOKMARK_RIBBON_W;
    int h = BOOKMARK_RIBBON_H;
    int cut = BOOKMARK_RIBBON_CUT;

    display.setPartialWindow(x - 2, 0, w + 4, h + 2);
    display.firstPage();
    do {
        display.fillRect(x - 2, 0, w + 4, h + 2, GxEPD_WHITE);
        if (added) {
            display.fillRect(x, 0, w, h, GxEPD_BLACK);
            display.fillTriangle(x, h, x + w, h, x + w / 2, h - cut, GxEPD_WHITE);
        }
    } while (display.nextPage());
}
