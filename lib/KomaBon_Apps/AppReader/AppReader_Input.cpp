#include "AppReader.h"
#include "AppMgr.h"

void AppReader::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;

    // Global override: Long pressing Center/KEY1 instantly exits the Reader from any state
    if (action == INPUT_GO_TO_MAIN_MENU) {
        if (_state == VIEW_READING || _state == VIEW_OVERLAY_SETTINGS || _state == VIEW_OVERLAY_TOC) {
            closeBook();
        }
        _state = VIEW_LIBRARY;
        _booksScanned = false;
        _librarySelectionOnlyRedraw = false;
        markProgressInactive();
        AppMgr::getInstance().switchTo(0);
        return;
    }

    if (_state == VIEW_LIBRARY) {
        if (action == INPUT_BACK || action == INPUT_LEFT) {
            markProgressInactive();
            AppMgr::getInstance().switchTo(0);
            return;
        }

        if (_books.empty()) return;

        int maxIndex = (int)_books.size() - 1;

        if (action == INPUT_NEXT) {
            _previousBookIndex = _selectedBookIndex;
            _selectedBookIndex++;
            if (_selectedBookIndex > maxIndex) _selectedBookIndex = 0;
            _librarySelectionOnlyRedraw = _booksScanned;
            updateLibraryScroll();
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _previousBookIndex = _selectedBookIndex;
            _selectedBookIndex--;
            if (_selectedBookIndex < 0) _selectedBookIndex = maxIndex;
            _librarySelectionOnlyRedraw = _booksScanned;
            updateLibraryScroll();
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_selectedBookIndex >= 0 && _selectedBookIndex <= maxIndex) {
                openBook(_books[_selectedBookIndex].path.c_str());
            }
        }
    } else if (_state == VIEW_READING) {
        if (action == INPUT_RIGHT)
            nextPage();
        else if (action == INPUT_LEFT)
            prevPage();
        else if (action == INPUT_SELECT)
            openSettingsOverlay();
        else if (action == INPUT_PREV)
            openTOCOverlay();
        else if (action == INPUT_BACK) {
            closeBook();
            _state = VIEW_LIBRARY;
            _booksScanned = false;
            _librarySelectionOnlyRedraw = false;
            _needsRedraw = true;
        }
    } else if (_state == VIEW_OVERLAY_SETTINGS) {
        if (action == INPUT_NEXT) {
            _overlaySelectedIndex = (_overlaySelectedIndex + 1) % 4;
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _overlaySelectedIndex = (_overlaySelectedIndex - 1 + 4) % 4;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_overlaySelectedIndex == 0) {
                if (_fontSizePt == 9)
                    _fontSizePt = 12;
                else if (_fontSizePt == 12)
                    _fontSizePt = 18;
                else
                    _fontSizePt = 9;
                _settingsChanged = true;
                _needsRedraw = true;
            } else if (_overlaySelectedIndex == 1) {
                _fontFamily = (_fontFamily + 1) % 6;
                _settingsChanged = true;
                _needsRedraw = true;
            } else if (_overlaySelectedIndex == 2) {
                _readingFirstDraw = true;
                _settingsChanged = true;
                closeOverlay();
            } else if (_overlaySelectedIndex == 3) {
                closeOverlay();
            }
        } else if (action == INPUT_LEFT || action == INPUT_BACK) {
            closeOverlay();
        }
    } else if (_state == VIEW_OVERLAY_TOC) {
        int total = 0;
        {
            KomaBonGuard guard(_epubMutex);
            if (_epubLoader) total = _epubLoader->getChapterCount();
        }

        if (total == 0) {
            if (action == INPUT_SELECT || action == INPUT_LEFT || action == INPUT_BACK) closeOverlay();
            return;
        }

        if (action == INPUT_NEXT) {
            _overlaySelectedIndex++;
            if (_overlaySelectedIndex >= total) _overlaySelectedIndex = 0;
            if (_overlaySelectedIndex >= _overlayScrollOffset + 7)
                _overlayScrollOffset = _overlaySelectedIndex - 6;
            if (_overlaySelectedIndex < _overlayScrollOffset) _overlayScrollOffset = _overlaySelectedIndex;
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _overlaySelectedIndex--;
            if (_overlaySelectedIndex < 0) _overlaySelectedIndex = total - 1;
            if (_overlaySelectedIndex < _overlayScrollOffset) _overlayScrollOffset = _overlaySelectedIndex;
            if (_overlaySelectedIndex >= _overlayScrollOffset + 7)
                _overlayScrollOffset = _overlaySelectedIndex - 6;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            loadChapter(_overlaySelectedIndex);

            if (_overlaySelectedIndex < (int)_chapterStartPages.size()) {
                _globalPageNumber = _chapterStartPages[_overlaySelectedIndex];
            } else {
                _globalPageNumber = 1;
            }

            closeOverlay();
        } else if (action == INPUT_LEFT || action == INPUT_BACK) {
            closeOverlay();
        }
    }
}

void AppReader::openSettingsOverlay() {
    _state = VIEW_OVERLAY_SETTINGS;
    _overlaySelectedIndex = 0;
    _settingsChanged = false;
    _needsRedraw = true;
}

void AppReader::openTOCOverlay() {
    _state = VIEW_OVERLAY_TOC;
    _overlaySelectedIndex = _currentChapter;
    _overlayScrollOffset = std::max(0, _currentChapter - 3);
    _needsRedraw = true;
}

void AppReader::closeOverlay() {
    _state = VIEW_READING;

    if (_settingsChanged) {
        _settingsChanged = false;
        if (_textRenderer) {
            KomaBonGuard guard(_epubMutex);
            _textRenderer->setFontSize(_fontSizePt);
            _textRenderer->setFontFamily(_fontFamily);
        }
        _currentPageRenderValid = false;
        _readingFirstDraw = true;
        _pageTurnsSinceRefresh = 0;
        startTotalPagesCounting();
    }

    forceRedraw();
}