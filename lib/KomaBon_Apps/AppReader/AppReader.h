#ifndef APP_READER_H
#define APP_READER_H

#include "BaseApp.h"
#include "EpubLoader.h"
#include "TextRenderer.h"
#include "KBReader.h"
#include "../../KomaBon_Core/InputMgr.h"
#include "../../KomaBon_Core/Lock.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <vector>
#include <map>

enum ReaderState { VIEW_LIBRARY, VIEW_READING, VIEW_OVERLAY_SETTINGS, VIEW_OVERLAY_TOC };

// Utility function to extract the bare filename from a path (handling both / and \)
inline String normalizedBookName(const String& path) {
    String name = path;
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    slash = name.lastIndexOf('\\');
    if (slash >= 0) name = name.substring(slash + 1);
    return name;
}

struct BookEntry {
    String path;         // Full path to file
    String title;        // Display title
    String originalName; // Key used by ProgressStore (original filename)
    String baseName;     // Base filename without extension for thumbnail caching
    bool hasProgress;    // True when a saved position exists
    int globalPage;      // Current saved page, shown in library list
    int totalPages;      // Cached total page count (0 when unknown)
    bool hasCoverThumb;  // True if thumbnail already exists on storage
    bool coverAttempted; // True if the extraction engine already evaluated this book

    BookEntry()
        : hasProgress(false), globalPage(1), totalPages(0), hasCoverThumb(false), coverAttempted(false) {}
};

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
        return "Bookshelf";
    }

    bool allowsSystemStatusIndicator() override {
        return _state == VIEW_LIBRARY;
    }

    bool hasBootResume();
    void resumeSavedBookOnStart();
    void handleInput(InputAction action);
    void forceRedraw() override;

    void applyFontSize(int pt) override;
    void applyFontFamily(int family) override;

  private:
    ReaderState _state;

    // Library State and Navigation
    std::vector<BookEntry> _books;
    int _selectedBookIndex;
    bool _booksScanned;
    bool _librarySelectionOnlyRedraw;
    bool _resumeSavedBookOnStart;
    int _previousBookIndex;
    int _libraryScrollOffset;

    void scanBooks();
    void drawLibrary();
    void updateLibraryScroll();
    void drawBookTile(KomaBonDisplay& display, const BookEntry& book, int x, int y, int w, int h,
                      bool selected, const uint8_t* thumbData = nullptr);

    // Settings
    int _refreshEveryNPages;
    int _pageTurnsSinceRefresh;
    int _fontSizePt;
    int _fontFamily;
    bool _readingFirstDraw;
    void loadSettings();

    // Reading Engine
    bool _isComicMode;
    KBReader* _kbReader;
    EpubLoader* _epubLoader;
    TextRenderer* _textRenderer;
    String _currentBookPath;
    int _currentChapter;
    int _globalPageNumber;
    bool _needsRedraw;

    // Persistent DMA-ready buffer for KMB raw page data allocated in PSRAM
    uint8_t* _comicPageBuffer = nullptr;

    // Thread-Safety and FreeRTOS Multi-Core Pagination
    Book32Mutex _epubMutex;
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
    bool _settingsChanged; // NEW: Tracks unsaved changes in the overlay
    void openSettingsOverlay();
    void openTOCOverlay();
    void closeOverlay();
    void drawOverlaySettings();
    void drawOverlayTOC();

    bool openBook(const String& path, bool restoreProgress = true);
    bool openSavedProgress();
    bool loadBookProgress(const String& originalName, int& chapter, PagePointer& pointer, int& globalPage);

    void saveReadingProgress(bool resumeOnBoot);
    void flushProgress();
    bool _progressDirty = false;
    bool _progressResumeOnBoot = false;
    unsigned long _lastProgressChangeMs = 0;
    static const unsigned long PROGRESS_FLUSH_DELAY_MS = 4000;

    void markProgressInactive();
    void closeBook(bool markInactive = true);
    void loadChapter(int chapterIndex);
    void nextPage();
    void prevPage();
    void nextChapter();
    void prevChapter();
    void drawReading();
};

#endif // APP_READER_H