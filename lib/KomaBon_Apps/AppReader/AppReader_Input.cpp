#include "AppReader.h"
#include "AppMgr.h"

void AppReader::handleInput(InputAction action) {
    if (action == INPUT_NONE) return;

    // Global override: Long pressing Center/KEY1 instantly exits the Reader from any state
    if (action == INPUT_GO_TO_MAIN_MENU) {
        closeBook();
        markProgressInactive();
        AppMgr::getInstance().switchTo(0);
        return;
    }

    if (_state == VIEW_READING) {

        if (action == INPUT_RIGHT)
            _isRTL ? prevPage() : nextPage();
        else if (action == INPUT_LEFT)
            _isRTL ? nextPage() : prevPage();
        else if (action == INPUT_SELECT)
            openSettingsOverlay();
        else if (action == INPUT_PREV)
            openTOCOverlay();
        else if (action == INPUT_NEXT)
            openGotoOverlay();
        else if (action == INPUT_SLEEP)
            enterSleepMode();
        else if (action == INPUT_BACK) {
            closeBook();
            AppMgr::getInstance().switchTo("Bookshelf");
            _needsRedraw = true;
        }
    } else if (_state == VIEW_OVERLAY_SETTINGS) {
        if (action == INPUT_NEXT) {
            _overlaySelectedIndex = (_overlaySelectedIndex + 1) % 8;
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _overlaySelectedIndex = (_overlaySelectedIndex - 1 + 8) % 8;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_overlaySelectedIndex == 0) {
                if (_fontSizePt == 8)
                    _fontSizePt = 10;
                else if (_fontSizePt == 10)
                    _fontSizePt = 12;
                else if (_fontSizePt == 12)
                    _fontSizePt = 14;
                else if (_fontSizePt == 14)
                    _fontSizePt = 16;
                else if (_fontSizePt == 16)
                    _fontSizePt = 18;
                else
                    _fontSizePt = 8;
                _settingsChanged = true;
                _needsRedraw = true;
            } else if (_overlaySelectedIndex == 1) {
                _fontFamily = (_fontFamily + 1) % 6;
                _settingsChanged = true;
                _needsRedraw = true;
            } else if (_overlaySelectedIndex == 2) {
                _margin = (_margin + 1) % 3;
                _settingsChanged = true;
                _needsRedraw = true;
            } else if (_overlaySelectedIndex == 3) {
                _justifyText = !_justifyText;
                _settingsChanged = true;
                _needsRedraw = true;
            } else if (_overlaySelectedIndex == 4) {
                _isRTL = !_isRTL;
                _progressDirty = true;
                _settingsChanged = true;
                _needsRedraw = true;
            } else if (_overlaySelectedIndex == 5) {
                openGotoOverlay();
            } else if (_overlaySelectedIndex == 6) {
                _readingFirstDraw = true;
                _settingsChanged = true;
                closeOverlay();
            } else if (_overlaySelectedIndex == 7) {
                closeOverlay();
            }
        } else if (action == INPUT_LEFT || action == INPUT_BACK) {
            closeOverlay();
        }
    } else if (_state == VIEW_OVERLAY_GOTO) {
        int maxPage = (_totalPages > 0) ? _totalPages : 9999;
        if (action == INPUT_RIGHT) {
            _gotoTargetPage = std::min(maxPage, _gotoTargetPage + 1);
            _needsRedraw = true;
        } else if (action == INPUT_LEFT) {
            _gotoTargetPage = std::max(1, _gotoTargetPage - 1);
            _needsRedraw = true;
        } else if (action == INPUT_NEXT) {
            _gotoTargetPage = std::min(maxPage, _gotoTargetPage + 10);
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _gotoTargetPage = std::max(1, _gotoTargetPage - 10);
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            int target = _gotoTargetPage;
            closeOverlay();
            goToPage(target);
        } else if (action == INPUT_BACK) {
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

void AppReader::openGotoOverlay() {
    _state = VIEW_OVERLAY_GOTO;
    _gotoTargetPage = _globalPageNumber;
    if (_gotoTargetPage < 1) _gotoTargetPage = 1;
    if (_totalPages > 0 && _gotoTargetPage > _totalPages) _gotoTargetPage = _totalPages;
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
            _textRenderer->setMargin(_margin);
            _textRenderer->setJustify(_justifyText);
        }
        _currentPageRenderValid = false;
        _readingFirstDraw = true;
        _pageTurnsSinceRefresh = 0;
        startTotalPagesCounting();
    }

    forceRedraw();
}
