#include "BookmarkStore.h"
#include "KomaBonFS.h"
#include <algorithm>

static const char* BOOKMARKS_PATH = "/bookmarks.json";
static const char* BOOKMARKS_TMP_PATH = "/bookmarks.tmp";

BookmarkStore& BookmarkStore::getInstance() {
    static BookmarkStore instance;
    return instance;
}

void BookmarkStore::begin() {
    KomaBonGuard guard(_mutex);
    if (_loaded) return;
    _loaded = true;
    load();
}

bool BookmarkStore::load() {
    _bookmarks.clear();

    File file;
    if (EbookFS.exists(BOOKMARKS_PATH)) {
        file = EbookFS.open(BOOKMARKS_PATH, "r");
    } else if (SystemFS.exists(BOOKMARKS_PATH)) {
        file = SystemFS.open(BOOKMARKS_PATH, "r");
    } else {
        return false;
    }

    size_t sz = file.size();
    size_t cap = sz * 2 + 1024;
    if (cap > 32768) cap = 32768;
    DynamicJsonDocument doc(cap);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("BookmarkStore: deserialize error: %s\n", error.c_str());
        return false;
    }

    JsonObject booksObj = doc["books"].as<JsonObject>();
    for (JsonPair kv : booksObj) {
        String key = kv.key().c_str();
        JsonArray arr = kv.value().as<JsonArray>();
        std::vector<BookmarkEntry> list;
        for (JsonObject b : arr) {
            BookmarkEntry entry;
            entry.page = b["page"] | 1;
            entry.chapter = b["chapter"] | 0;
            entry.nodeIndex = b["nodeIndex"] | 0;
            entry.charOffset = b["charOffset"] | 0;
            list.push_back(entry);
        }
        _bookmarks[key] = list;
    }

    return true;
}

bool BookmarkStore::save() {
    size_t totalEntries = 0;
    for (const auto& kv : _bookmarks) {
        totalEntries += kv.second.size();
    }
    size_t cap = 1024 + totalEntries * 96;
    if (cap > 32768) cap = 32768;

    DynamicJsonDocument doc(cap);
    JsonObject booksObj = doc.createNestedObject("books");
    for (const auto& kv : _bookmarks) {
        if (kv.second.empty()) continue;
        JsonArray arr = booksObj.createNestedArray(kv.first);
        for (const auto& b : kv.second) {
            JsonObject obj = arr.createNestedObject();
            obj["page"] = b.page;
            obj["chapter"] = b.chapter;
            obj["nodeIndex"] = b.nodeIndex;
            obj["charOffset"] = b.charOffset;
        }
    }

    fs::FS& targetFs = EbookFS.exists("/") ? EbookFS : SystemFS;
    File tmp = targetFs.open(BOOKMARKS_TMP_PATH, "w");
    if (!tmp) {
        Serial.println("BookmarkStore: failed to open tmp file for write");
        return false;
    }

    serializeJson(doc, tmp);
    tmp.close();

    targetFs.remove(BOOKMARKS_PATH);
    if (!targetFs.rename(BOOKMARKS_TMP_PATH, BOOKMARKS_PATH)) {
        Serial.println("BookmarkStore: failed to rename tmp to bookmarks.json");
        return false;
    }

    return true;
}

std::vector<BookmarkEntry> BookmarkStore::getBookmarks(const String& originalName) {
    KomaBonGuard guard(_mutex);
    begin();
    auto it = _bookmarks.find(originalName);
    if (it != _bookmarks.end()) {
        return it->second;
    }
    return {};
}

bool BookmarkStore::isBookmarked(const String& originalName, int page) {
    KomaBonGuard guard(_mutex);
    begin();
    auto it = _bookmarks.find(originalName);
    if (it == _bookmarks.end()) return false;
    for (const auto& b : it->second) {
        if (b.page == page) return true;
    }
    return false;
}

bool BookmarkStore::toggleBookmark(const String& originalName, const BookmarkEntry& entry) {
    KomaBonGuard guard(_mutex);
    begin();
    auto& list = _bookmarks[originalName];
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (it->page == entry.page) {
            list.erase(it);
            save();
            return false; // Removed
        }
    }

    // Add and sort by page number
    list.push_back(entry);
    std::sort(list.begin(), list.end(),
              [](const BookmarkEntry& a, const BookmarkEntry& b) { return a.page < b.page; });

    save();
    return true; // Added
}

void BookmarkStore::addBookmark(const String& originalName, const BookmarkEntry& entry) {
    KomaBonGuard guard(_mutex);
    begin();
    auto& list = _bookmarks[originalName];
    for (const auto& b : list) {
        if (b.page == entry.page) return;
    }
    list.push_back(entry);
    std::sort(list.begin(), list.end(),
              [](const BookmarkEntry& a, const BookmarkEntry& b) { return a.page < b.page; });
    save();
}

void BookmarkStore::removeBookmark(const String& originalName, int page) {
    KomaBonGuard guard(_mutex);
    begin();
    auto it = _bookmarks.find(originalName);
    if (it == _bookmarks.end()) return;
    auto& list = it->second;
    for (auto bit = list.begin(); bit != list.end(); ++bit) {
        if (bit->page == page) {
            list.erase(bit);
            save();
            return;
        }
    }
}

void BookmarkStore::removeBookmarkByIndex(const String& originalName, size_t index) {
    KomaBonGuard guard(_mutex);
    begin();
    auto it = _bookmarks.find(originalName);
    if (it == _bookmarks.end()) return;
    if (index < it->second.size()) {
        it->second.erase(it->second.begin() + index);
        save();
    }
}

void BookmarkStore::clearBookmarks(const String& originalName) {
    KomaBonGuard guard(_mutex);
    begin();
    _bookmarks.erase(originalName);
    save();
}
