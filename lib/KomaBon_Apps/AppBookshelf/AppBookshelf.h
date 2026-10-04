#ifndef APP_BOOKSHELF_H
#define APP_BOOKSHELF_H

#include "BaseApp.h"
#include "../../KomaBon_Core/InputMgr.h"
#include "../../KomaBon_Core/DisplayMgr.h"
#include <vector>

struct BookEntry {
    String path;
    String title;
    String originalName;
    String baseName;
    bool hasProgress;
    int globalPage;
    int totalPages;
    bool hasCoverThumb;
    bool coverAttempted;
    bool rtl;

    BookEntry()
        : hasProgress(false), globalPage(1), totalPages(0), hasCoverThumb(false), coverAttempted(false),
          rtl(false) {}
};

enum BookshelfItemType { ITEM_BOOK, ITEM_SERIES, ITEM_VOLUME };

struct BookshelfItem {
    BookshelfItemType type;
    String name;                  // Display title, series name, or volume label
    int count;                    // Number of books/chapters if series or volume
    int bookIndex;                // Index in _books if ITEM_BOOK (-1 if series/volume)
    int coverBookIndex;           // Index in _books to borrow cover thumbnail from
    std::vector<int> bookIndices; // Indices of books in this series or volume

    BookshelfItem() : type(ITEM_BOOK), count(0), bookIndex(-1), coverBookIndex(-1) {}
};

class AppBookshelf : public App {
  public:
    AppBookshelf();
    virtual ~AppBookshelf() {}

    void start() override;
    void stop() override;
    void update() override;
    void draw() override;
    void forceRedraw() override;

    const uint8_t* getIconImage() override;
    const char* getName() override {
        return "Bookshelf";
    }

    bool allowsSystemStatusIndicator() override {
        return true;
    }
    void handleInput(InputAction action);

    // Allow external callers (e.g. WebMgr) to force a rescan on next draw
    void invalidateLibrary();

  private:
    std::vector<BookEntry> _books;
    std::vector<BookshelfItem> _rootItems;   // Main library items (single books + series folders)
    std::vector<BookshelfItem> _activeItems; // Currently displayed items (root or inside a series)

    String _currentSeriesFilter; // Empty if in root, otherwise series name
    String _currentVolumeFilter; // Empty if in series/root, otherwise volume label (e.g. "Vol. 01")
    int _savedRootSelectedIndex; // Restores cursor position when exiting a series
    int _savedRootScrollOffset;
    int _savedSeriesSelectedIndex; // Restores cursor position when exiting a volume
    int _savedSeriesScrollOffset;

    int _selectedBookIndex;
    bool _booksScanned;
    bool _librarySelectionOnlyRedraw;
    int _previousBookIndex;
    int _libraryScrollOffset;
    bool _needsRedraw;

    void scanBooks();
    void buildLibraryStructure();
    void enterSeries(const String& seriesName);
    void enterVolume(const String& volumeLabel, const std::vector<int>& bookIndices);
    void exitCurrentLevel();

    void drawLibrary();
    void updateLibraryScroll();
    void drawBookTile(KomaBonDisplay& display, const BookEntry& book, int x, int y, int w, int h,
                      bool selected, const uint8_t* thumbData = nullptr);
    void drawFolderTile(KomaBonDisplay& display, int x, int y, int w, int h, bool selected,
                        const uint8_t* thumbData = nullptr);
};

#endif
