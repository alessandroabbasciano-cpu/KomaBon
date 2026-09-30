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

    BookEntry()
        : hasProgress(false), globalPage(1), totalPages(0), hasCoverThumb(false), coverAttempted(false) {}
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
    int _selectedBookIndex;
    bool _booksScanned;
    bool _librarySelectionOnlyRedraw;
    int _previousBookIndex;
    int _libraryScrollOffset;
    bool _needsRedraw;

    void scanBooks();
    void drawLibrary();
    void updateLibraryScroll();
    void drawBookTile(KomaBonDisplay& display, const BookEntry& book, int x, int y, int w, int h,
                      bool selected, const uint8_t* thumbData = nullptr);
};

#endif
