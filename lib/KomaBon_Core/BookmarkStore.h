#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <map>
#include "Lock.h"

struct BookmarkEntry {
    int page = 1;
    int chapter = 0;
    int nodeIndex = 0;
    int charOffset = 0;
};

class BookmarkStore {
  public:
    static BookmarkStore& getInstance();

    void begin();
    std::vector<BookmarkEntry> getBookmarks(const String& originalName);
    bool isBookmarked(const String& originalName, int page);
    bool toggleBookmark(const String& originalName, const BookmarkEntry& entry);
    void addBookmark(const String& originalName, const BookmarkEntry& entry);
    void removeBookmark(const String& originalName, int page);
    void removeBookmarkByIndex(const String& originalName, size_t index);
    void clearBookmarks(const String& originalName);

  private:
    BookmarkStore() {}
    bool load();
    bool save();

    KomaBonMutex _mutex;
    std::map<String, std::vector<BookmarkEntry>> _bookmarks;
    bool _loaded = false;
};
