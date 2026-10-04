#ifndef APP_READER_H
#define APP_READER_H

#include "BaseApp.h"
#include "EpubLoader.h"
#include "TextRenderer.h"
#include "KBReader.h"
#include "../../KomaBon_Core/InputMgr.h"
#include "../../KomaBon_Core/Lock.h"
#include "../../KomaBon_Core/SettingsStore.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>
#include <map>

enum ReaderState { VIEW_READING, VIEW_OVERLAY_SETTINGS, VIEW_OVERLAY_TOC, VIEW_OVERLAY_GOTO };

// Utility function to extract the bare filename from a path (handling both / and \)

class AppReader : public App {
  public:
    AppReader();
    virtual ~AppReader();

    // App Interface Lifecycle
    void start() override;
    void stop() override;
    void update() override;
    void draw() override;

    const uint8_t* getIconImage() override;
    const char* getName() override {
        return "Reader";
    }

    bool allowsSystemStatusIndicator() override {
        return false;
    }
    bool isVisibleInMenu() override {
        return false;
    }

    void handleInput(InputAction action);
    bool hasBootResume();
    void resumeSavedBookOnStart();
    void forceRedraw() override;

    bool handleSleep() override;
    bool isReading() const {
        return _state == VIEW_READING;
    }
    void enterSleepMode();
    void drawSleepCover();

    void applyFontSize(int pt) override;
    void applyFontFamily(int family) override;
    void applyMargin(int margin) override;
    void applyJustify(bool justify) override;

  private:
    ReaderState _state;

    // Settings
    int _refreshEveryNPages;
    bool _resumeSavedBookOnStart;
    int _pageTurnsSinceRefresh;
    int _fontSizePt;
    int _fontFamily;
    int _margin;
    bool _justifyText;
    bool _readingFirstDraw;
    void loadSettings();

    // Reading Engine
    bool _isComicMode;
    bool _isRTL;
    KBReader* _kbReader;
    EpubLoader* _epubLoader;
    TextRenderer* _textRenderer;
    String _currentBookPath;
    String _bookTitle;
    String _currentChapterTitle;
    int _currentChapter;
    int _globalPageNumber;
    bool _needsRedraw;

    // Persistent DMA-ready buffer for KMB raw page data allocated in PSRAM
    uint8_t* _comicPageBuffer = nullptr;

    // Thread-Safety and FreeRTOS Multi-Core Pagination
    KomaBonMutex _epubMutex;
    TaskHandle_t _pageCountTaskHandle = nullptr;
    volatile bool _killPageCountTask = false;

    // Asynchronous Total Page Counting
    int _totalPages;
    volatile bool _countingActive;
    TextRenderer* _countRenderer;
    int _countChapter;
    std::vector<ContentNode> _countChapterContent;
    PagePointer _countPointer;
    int _countPagesSoFar;

    // Vector mapping each chapter index to its absolute starting page number
    std::vector<int> _chapterStartPages;

    static void pageCountTask(void* param);
    void startTotalPagesCounting();
    void updateTotalPagesCount();

    // Dynamic Pagination
    std::vector<ContentNode> _currentRichContent;
    PagePointer _currentPagePointer;
    std::vector<PagePointer> _pageHistory;
    RenderResult _currentPageRender;
    bool _currentPageRenderValid;

    // Overlay Menus
    int _overlaySelectedIndex;
    int _overlayScrollOffset;
    int _gotoTargetPage;   // Target page in Go to Page overlay
    bool _settingsChanged; // NEW: Tracks unsaved changes in the overlay
    void openSettingsOverlay();
    void openTOCOverlay();
    void openGotoOverlay();
    void closeOverlay();
    void drawOverlaySettings();
    void drawOverlayTOC();
    void drawOverlayGoto();

  public:
    bool openBook(const String& path, bool restoreProgress = true);
    bool openSavedProgress();
    bool loadBookProgress(const String& originalName, int& chapter, PagePointer& pointer, int& globalPage,
                          bool& rtl);

    void saveReadingProgress(bool resumeOnBoot);
    void flushProgress();
    bool _progressDirty = false;
    bool _progressResumeOnBoot = false;
    unsigned long _lastProgressChangeMs = 0;
    static const unsigned long PROGRESS_FLUSH_DELAY_MS = 4000;

    void markProgressInactive();
    void closeBook(bool markInactive = true);
    void loadChapter(int chapterIndex);
    void goToPage(int targetPage);
    void nextPage();
    void prevPage();
    void nextChapter();
    void prevChapter();
    void drawReading();
};

#endif // APP_READER_H
