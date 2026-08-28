#include "BookMeta.h"
#include "KomaBonFS.h"
#include "Lock.h"
#include <ArduinoJson.h>

static const char* BOOKS_META_PATH = "/books_meta.json";

// The file is read by the reader (main loop) and written by the upload and
// delete handlers (web server task). Each function below is a complete
// read-modify-write, so the lock covers the entire function rather than
// just file operations. Recursive because findFilenameForOriginal() calls
// loadBookMetadata(). See Lock.h.
//
// Acquisition order: whoever already holds ProgressStore can take this one
// (which happens during import); the reverse never happens.
static Book32Mutex g_metaMutex;

// Capacity derived from the file, not a fixed 4096: a large library used to
// overflow the fixed document, and a save on top of a truncated document threw
// away everyone else's entries.
static size_t metaCapacityFor(size_t fileSize) {
    size_t cap = fileSize * 2 + 1024;
    if (cap > 24576) cap = 24576;
    return cap;
}

static bool openMetaForRead(File& file) {
    if (SystemFS.exists(BOOKS_META_PATH)) {
        file = SystemFS.open(BOOKS_META_PATH, FILE_READ);
    } else if (EbookFS.exists(BOOKS_META_PATH)) {
        // Older firmware wrote the metadata to the ebook partition.
        file = EbookFS.open(BOOKS_META_PATH, FILE_READ);
    }
    return (bool)file;
}

void loadBookMetadata(std::map<String, String>& metadata) {
    Book32Guard guard(g_metaMutex);
    metadata.clear();

    File file;
    if (!openMetaForRead(file)) return;

    DynamicJsonDocument doc(metaCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || !doc.is<JsonObject>()) return;

    JsonObject obj = doc.as<JsonObject>();
    for (JsonPair pair : obj) {
        metadata[String(pair.key().c_str())] = pair.value().as<String>();
    }
}

String getOriginalFilename(const String& truncatedName) {
    Book32Guard guard(g_metaMutex);
    File file;
    if (!openMetaForRead(file)) return truncatedName;

    DynamicJsonDocument doc(metaCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return truncatedName;

    if (doc.containsKey(truncatedName)) {
        return doc[truncatedName].as<String>();
    }
    return truncatedName;
}

String findFilenameForOriginal(const String& originalName) {
    Book32Guard guard(g_metaMutex);
    std::map<String, String> metadata;
    loadBookMetadata(metadata);

    for (const auto& kv : metadata) {
        if (kv.second == originalName) {
            if (EbookFS.exists("/" + kv.first)) return kv.first;
        }
    }

    // No metadata entry: books that were never truncated are stored under the
    // original name itself.
    if (EbookFS.exists("/" + originalName)) return originalName;
    return "";
}

// --- File Read/Write --------------------------------------------------------
// Writing is always to SystemFS, but reading goes through openMetaForRead(),
// which also knows the legacy location (EbookFS). Without this, on a device
// migrated from old firmware, the first upload would read an empty SystemFS
// and write a file with a single entry; since openMetaForRead() prefers SystemFS,
// that file would hide the one in EbookFS and all previous original names
// would disappear — truncated titles in the library and reading progress
// lacking matching files.

// Size of the existing metadata file, regardless of partition; 0 when none
// exists yet. Used to size the document before reading it — the document must
// be created with the final capacity, because assigning a DynamicJsonDocument
// shrinks the pool to what is already in use and would leave no room for the new entry.
static size_t existingMetaSize() {
    File metaFile;
    if (!openMetaForRead(metaFile)) return 0;
    size_t size = metaFile.size();
    metaFile.close();
    return size;
}

// `doc` must be pre-sized by the caller.
static bool loadMetaDoc(DynamicJsonDocument& doc) {
    File metaFile;
    if (!openMetaForRead(metaFile)) return false;

    DeserializationError error = deserializeJson(doc, metaFile);
    metaFile.close();
    if (error) {
        Serial.println("Failed to parse metadata, creating new");
        doc.clear();
        return false;
    }
    return true;
}

static bool writeMetaDoc(const DynamicJsonDocument& doc) {
    // Same lesson as ProgressStore: it is better to refuse writing than to
    // replace a good file with a truncated one, which would erase the entries of
    // all other books.
    if (doc.overflowed()) {
        Serial.println("BookMeta: document exceeded capacity — write refused");
        return false;
    }

    File metaFile = SystemFS.open(BOOKS_META_PATH, FILE_WRITE);
    if (!metaFile) {
        Serial.println("Failed to save metadata");
        return false;
    }
    serializeJson(doc, metaFile);
    metaFile.close();
    return true;
}

void saveBookMetadata(const String& truncatedName, const String& originalName) {
    Book32Guard guard(g_metaMutex);
    size_t extra = truncatedName.length() + originalName.length() + 64;
    DynamicJsonDocument doc(metaCapacityFor(existingMetaSize()) + extra);
    loadMetaDoc(doc); // file missing or unreadable: start empty

    doc[truncatedName] = originalName;

    if (writeMetaDoc(doc)) {
        Serial.printf("Saved metadata: %s -> %s\n", truncatedName.c_str(), originalName.c_str());
    }
}

void removeBookMetadata(const String& truncatedName) {
    Book32Guard guard(g_metaMutex);
    size_t existingSize = existingMetaSize();
    if (existingSize == 0) return; // nothing recorded yet

    DynamicJsonDocument doc(metaCapacityFor(existingSize));
    if (!loadMetaDoc(doc)) return;

    doc.remove(truncatedName);
    writeMetaDoc(doc);
}