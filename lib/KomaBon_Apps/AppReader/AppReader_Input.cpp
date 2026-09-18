#include "AppReader.h"
#include "AppMgr.h"

void AppReader::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;

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
        } else if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU || action == INPUT_LEFT) {
            markProgressInactive();
            AppMgr::getInstance().switchTo(0);
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
        else if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU) {
            closeBook();
            _state = VIEW_LIBRARY;
            _booksScanned = false;
            _librarySelectionOnlyRedraw = false;
            _needsRedraw = true;
        }
    } else if (_state == VIEW_OVERLAY_SETTINGS) {
        if (action == INPUT_NEXT) {
            _overlaySelectedIndex = (_overlaySelectedIndex + 1) % 4; // Reduced to 4 items
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _overlaySelectedIndex = (_overlaySelectedIndex - 1 + 4) % 4;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_overlaySelectedIndex == 0) {
                // Cycle Font Size: 9 -> 12 -> 18 -> 9
                if (_fontSizePt == 9)
                    _fontSizePt = 12;
                else if (_fontSizePt == 12)
                    _fontSizePt = 18;
                else
                    _fontSizePt = 9;
                _settingsChanged = true;
                _needsRedraw = true; // Only redraws the overlay
            } else if (_overlaySelectedIndex == 1) {
                // Cycle Font Family: 0 to 5
                _fontFamily = (_fontFamily + 1) % 6;
                _settingsChanged = true;
                _needsRedraw = true;
            } else if (_overlaySelectedIndex == 2) {
                _readingFirstDraw = true;
                _settingsChanged = true; // Force redraw logic on close
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
            Book32Guard guard(_epubMutex);
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

            // Fetch correct global page from the background-mapped vector
            if (_overlaySelectedIndex < (int)_chapterStartPages.size()) {
                _globalPageNumber = _chapterStartPages[_overlaySelectedIndex];
            } else {
                // Failsafe: if the background thread has not reached this chapter yet
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
    _settingsChanged = false; // Reset tracker
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

    // Apply changes only upon closing the overlay to prevent stuttering
    if (_settingsChanged) {
        _settingsChanged = false;
        if (_textRenderer) {
            Book32Guard guard(_epubMutex);
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