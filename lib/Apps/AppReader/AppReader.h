#ifndef APP_READER_H
#define APP_READER_H

#include "BaseApp.h"
#include "EpubLoader.h"
#include "TextRenderer.h"
#include "../../KomaBon_Core/InputMgr.h"
#include <vector>
#include <map>

enum ReaderState { VIEW_LIBRARY, VIEW_READING };

struct BookEntry {
    String path;         // Full path to file
    String title;        // Display title
    String originalName; // v1.8.0: key used by ProgressStore (original filename)
    bool hasProgress;    // v1.8.0: true when a saved position exists
    int globalPage;      // v1.8.0: saved page, shown in the library list
    int totalPages;      // Cached total page count; 0 when not yet known

    BookEntry() : hasProgress(false), globalPage(1), totalPages(0) {}
};

class AppReader : public App {
  public:
    AppReader();
    virtual ~AppReader();

    // App Interface
    void start() override;
    void stop() override;
    void update() override; // Main loop: Input handling
    void draw() override;   // Display handling

    // Icon
    const uint8_t* getIconImage() override;
    const char* getName() override {
        return "eReader";
    }

    // The reader page occupies the whole screen: the system battery indicator
    // would overlay the text. In the library view, there is no conflict.
    bool allowsSystemStatusIndicator() override {
        return _state != VIEW_READING;
    }

    bool hasBootResume();
    void resumeSavedBookOnStart();
    void handleInput(InputAction action);
    void forceRedraw() override;

    // Apply a new reading font size (9/12/18pt) live. Safe to call from the
    // main loop; re-paginates the current page from the saved position.
    void applyFontSize(int pt) override;

    // Apply a new reading font family (see ReaderFontFamily) live. Safe to
    // call from the main loop; re-paginates the current page.
    void applyFontFamily(int family) override;

  private:
    ReaderState _state;

    // Library
    std::vector<BookEntry> _books;
    int _selectedBookIndex;
    bool _booksScanned;
    bool _librarySelectionOnlyRedraw;
    bool _resumeSavedBookOnStart;
    int _previousBookIndex;
    // Index of the first book drawn in the list. The list only shows as many
    // items as fit on screen, so moving selection past the visible window
    // scrolls it (see updateLibraryScroll()).
    int _libraryScrollOffset;
    void scanBooks();
    void drawLibrary();
    void updateLibraryScroll();
    void drawBookTile(KomaBonDisplay& display, int x, int y, int w, int h, bool selected);

    // Settings
    int _refreshEveryNPages;
    int _pageTurnsSinceRefresh;
    int _fontSizePt;        // Reading body font size in points (9/12/18)
    int _fontFamily;        // Reading font family (see ReaderFontFamily)
    bool _readingFirstDraw; // Forces a full refresh on the next reading draw
    void loadSettings();

    // Reading
    EpubLoader* _epubLoader;
    TextRenderer* _textRenderer;
    String _currentBookPath;
    int _currentChapter;
    int _globalPageNumber; // Runtime tracking of global page (1-indexed)
    bool _needsRedraw;

    // Total page count, for the reading footer and the library list. Paginating
    // a whole book up front would stall opening a large one, so it's counted a
    // little at a time from update() instead, using a renderer of its own so it
    // never disturbs the page actually on screen. See startTotalPagesCounting().
    int _totalPages; // 0 until known for the currently open book
    bool _countingActive;
    TextRenderer* _countRenderer;
    int _countChapter;
    std::vector<ContentNode> _countChapterContent;
    PagePointer _countPointer;
    int _countPagesSoFar;
    static const unsigned long TOTAL_PAGES_BUDGET_MS = 15;
    void startTotalPagesCounting();
    void updateTotalPagesCount();

    // Dynamic Pagination
    std::vector<ContentNode> _currentRichContent;
    PagePointer _currentPagePointer;
    std::vector<PagePointer> _pageHistory; // Stores start of each page for current chapter
    RenderResult _currentPageRender;
    bool _currentPageRenderValid;

    bool openBook(const String& path, bool restoreProgress = true);
    bool openSavedProgress();
    // v1.8.0: keyed by original filename via ProgressStore, not by path.
    bool loadBookProgress(const String& originalName, int& chapter, PagePointer& pointer, int& globalPage);

    // Marks the current position as dirty (to be saved). The actual writing
    // to flash happens in flushProgress(), which is called by update() when
    // the reader is idle and whenever the book is closed (including standby).
    // Saving on every page turn would rewrite the entire reader_progress.json
    // hundreds of times per reading session, wearing out the flash memory.
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

#endif