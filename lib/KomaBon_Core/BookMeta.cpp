#include "BookMeta.h"
#include "KomaBonFS.h"
#include "Lock.h"
#include <ArduinoJson.h>
#include <Arduino.h>

static const char* BOOKS_META_PATH = "/books_meta.json";

static KomaBonMutex g_metaMutex;

static size_t metaCapacityFor(size_t fileSize) {
    size_t cap = fileSize * 2 + 1024;
    if (cap > 24576) cap = 24576;
    return cap;
}

static bool openMetaForRead(File& file) {
    if (EbookFS.exists(BOOKS_META_PATH)) {
        file = EbookFS.open(BOOKS_META_PATH, FILE_READ);
    } else if (SystemFS.exists(BOOKS_META_PATH)) {
        file = SystemFS.open(BOOKS_META_PATH, FILE_READ);
    }
    return (bool)file;
}

void loadBookMetadata(std::map<String, String>& metadata) {
    KomaBonGuard guard(g_metaMutex);
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
    KomaBonGuard guard(g_metaMutex);
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
    KomaBonGuard guard(g_metaMutex);
    std::map<String, String> metadata;
    loadBookMetadata(metadata);

    for (const auto& kv : metadata) {
        if (kv.second == originalName) {
            if (EbookFS.exists("/" + kv.first)) return kv.first;
        }
    }

    if (EbookFS.exists("/" + originalName)) return originalName;
    return "";
}

static size_t existingMetaSize() {
    File metaFile;
    if (!openMetaForRead(metaFile)) return 0;
    size_t size = metaFile.size();
    metaFile.close();
    return size;
}

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
    if (doc.overflowed()) {
        Serial.println("BookMeta: document exceeded capacity — write refused");
        return false;
    }

    File metaFile = EbookFS.open(BOOKS_META_PATH, FILE_WRITE);
    if (!metaFile) {
        Serial.println("Failed to save metadata on EbookFS");
        return false;
    }
    serializeJson(doc, metaFile);
    metaFile.close();
    return true;
}

void saveBookMetadata(const String& truncatedName, const String& originalName) {
    KomaBonGuard guard(g_metaMutex);
    size_t extra = truncatedName.length() + originalName.length() + 64;
    DynamicJsonDocument doc(metaCapacityFor(existingMetaSize()) + extra);
    loadMetaDoc(doc);

    doc[truncatedName] = originalName;

    if (writeMetaDoc(doc)) {
        Serial.printf("Saved metadata: %s -> %s\n", truncatedName.c_str(), originalName.c_str());
    }
}

void removeBookMetadata(const String& truncatedName) {
    KomaBonGuard guard(g_metaMutex);
    size_t existingSize = existingMetaSize();
    if (existingSize == 0) return;

    DynamicJsonDocument doc(metaCapacityFor(existingSize));
    if (!loadMetaDoc(doc)) return;

    doc.remove(truncatedName);
    writeMetaDoc(doc);
}