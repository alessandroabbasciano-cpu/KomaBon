#include "WebMgr.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <LittleFS.h>
#include "../KomaBon_Core/KomaBonFS.h"
#include "../KomaBon_Core/FileExt.h"
#include "../KomaBon_Core/SafeName.h"
#include "../KomaBon_Core/UploadGuard.h"
#include "../KomaBon_Core/BookOrderLogic.h"
#include "../KomaBon_Core/BookMeta.h"
#include "../KomaBon_Core/ProgressStore.h"
#include "../KomaBon_Core/PageCountStore.h"
#include <SD.h>
#include "../KomaBon_Core/SDMgr.h"

// --- Helper Functions & State ---

static const char* BOOK_ORDER_PATH = "/book_order.json";
static const char* IMPORT_TMP_PATH = "/import.tmp";
static const size_t IMPORT_MAX_BYTES = 64 * 1024;

static void loadBookOrder(std::vector<String>& order) {
    order.clear();
    File f;
    if (EbookFS.exists(BOOK_ORDER_PATH)) {
        f = EbookFS.open(BOOK_ORDER_PATH, FILE_READ);
    } else if (SystemFS.exists(BOOK_ORDER_PATH)) {
        f = SystemFS.open(BOOK_ORDER_PATH, FILE_READ);
    } else {
        return;
    }

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;
    JsonArray arr = doc["order"].as<JsonArray>();
    for (JsonVariant v : arr)
        order.push_back(v.as<String>());
}

static void saveBookOrder(const std::vector<String>& order) {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("order");
    for (const String& s : order)
        arr.add(s);

    // Atomicity: Write-Ahead Logging (WAL)
    String tmpPath = String(BOOK_ORDER_PATH) + ".tmp";
    File f = EbookFS.open(tmpPath, FILE_WRITE);
    if (!f) {
        Serial.println("WebRoutes_Books: failed to open temporary book_order.tmp for write");
        return;
    }

    size_t bytesWritten = serializeJson(doc, f);
    f.flush();
    f.close();

    if (bytesWritten == 0) {
        EbookFS.remove(tmpPath);
        return;
    }

    // Atomicity: Swap files
    if (!EbookFS.rename(tmpPath, BOOK_ORDER_PATH)) {
        EbookFS.remove(BOOK_ORDER_PATH);
        if (!EbookFS.rename(tmpPath, BOOK_ORDER_PATH)) {
            EbookFS.remove(tmpPath);
        }
    }
}

static void removeFromBookOrder(const String& filename) {
    std::vector<String> order;
    loadBookOrder(order);
    bool changed = false;
    for (auto it = order.begin(); it != order.end();) {
        if (*it == filename) {
            it = order.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) saveBookOrder(order);
}

static void applyBookOrder(const std::vector<String>& order, std::vector<String>& fsNames) {
    applyBookOrderT(order, fsNames, [](const String& item, const String& key) { return item == key; });
}

static String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if ((uint8_t)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

void removeBookProgress(const String& filename) {
    ProgressStore::getInstance().remove(getOriginalFilename(filename));
}

enum class UploadStatus { Idle, Ok, BadExtension, UnsafeName, NoSpace, WriteFailed };

struct UploadState {
    UploadStatus status = UploadStatus::Idle;
    AsyncWebServerRequest* owner = nullptr;
    File file;
    String path;
    String tempPath;
    String finalName;
    String originalName;

    void reset() {
        if (file) file.close();
        status = UploadStatus::Idle;
        owner = nullptr;
        path = "";
        tempPath = "";
        finalName = "";
        originalName = "";
    }
};

static UploadState g_uploadState;

struct ImportState {
    AsyncWebServerRequest* owner = nullptr;
    File file;
    size_t size = 0;
    bool received = false;
    bool tooBig = false;

    void reset() {
        if (file) file.close();
        owner = nullptr;
        size = 0;
        received = false;
        tooBig = false;
    }
};

static ImportState g_importState;

struct ImportOutcome {
    bool ok = false;
    ImportReport report;
    int metaAdded = 0;
    bool orderApplied = false;
    String error;
};

static ImportOutcome applyImportBundle(const char* path) {
    ImportOutcome outcome;

    File f = EbookFS.open(path, FILE_READ);
    if (!f) {
        outcome.error = "Temporary file unreadable";
        return outcome;
    }

    DynamicJsonDocument doc(f.size() * 2 + 2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        outcome.error = String("Invalid JSON: ") + err.c_str();
        return outcome;
    }

    int schema = doc["komabon"]["schema"] | 0;
    if (!isSupportedSchema(schema)) {
        outcome.error = "Unsupported bundle schema";
        return outcome;
    }

    outcome.report = ProgressStore::getInstance().applyImportedJson(doc["progress"].as<JsonObjectConst>());
    if (!outcome.report.ok) {
        outcome.error = outcome.report.error;
        return outcome;
    }

    JsonObjectConst meta = doc["meta"].as<JsonObjectConst>();
    if (!meta.isNull()) {
        std::map<String, String> local;
        loadBookMetadata(local);
        for (JsonPairConst kv : meta) {
            String original = kv.value().as<String>();
            if (original.length() == 0) continue;
            String filename = findFilenameForOriginal(original);
            if (filename.length() == 0) continue;
            if (local.find(filename) != local.end()) continue;
            saveBookMetadata(filename, original);
            outcome.metaAdded++;
        }
    }

    JsonArrayConst order = doc["order"].as<JsonArrayConst>();
    if (!order.isNull()) {
        std::vector<String> localOrder;
        for (JsonVariantConst v : order) {
            String filename = findFilenameForOriginal(v.as<String>());
            if (filename.length() > 0) localOrder.push_back(filename);
        }
        if (!localOrder.empty()) {
            saveBookOrder(localOrder);
            outcome.orderApplied = true;
        }
    }

    outcome.ok = true;
    return outcome;
}

// --- Route Definitions ---

static bool deleteSingleBookInternal(const String& filename) {
    if (!isSafeBookName(filename)) return false;

    String path = "/" + filename;
    if (!EbookFS.exists(path)) return false;
    if (!EbookFS.remove(path)) {
        Serial.printf("[HTTP] DELETE ERROR: Could not remove '%s' from storage\n", path.c_str());
        return false;
    }
    Serial.printf("[HTTP] Book deleted: '%s'\n", filename.c_str());

    removeBookProgress(filename);
    removeFromBookOrder(filename);
    removeBookMetadata(filename);

    int dot = filename.lastIndexOf('.');
    String base = (dot > 0) ? filename.substring(0, dot) : filename;
    const char* derivedExts[] = {".thumb", ".cover", ".cover2"};
    for (const char* ext : derivedExts) {
        String derived = "/covers/" + base + ext;
        if (EbookFS.exists(derived)) EbookFS.remove(derived);
    }
    return true;
}

void setupBookEndpoints(AsyncWebServer* server) {
    server->on("/api/books", HTTP_GET, [](AsyncWebServerRequest* request) {
        KomaBonStorage::ensureReady();

        // Safe download routing without duplicate disposition headers
        if (request->hasParam("name")) {
            String filename = request->getParam("name")->value();
            if (isSafeBookName(filename)) {
                String path = "/" + filename;
                if (EbookFS.exists(path)) {
                    request->send(EbookFS, path, "application/octet-stream", true);
                    return;
                }
            }
            request->send(404, "text/plain", "File not found");
            return;
        }

        struct BookListItem {
            String name;
            size_t size;
        };

        std::vector<BookListItem> epubs, fonts;
        File root = EbookFS.open("/");
        if (root && root.isDirectory()) {
            File file = root.openNextFile();
            while (file) {
                String name = file.name();
                int slash = name.lastIndexOf('/');
                if (slash >= 0) name = name.substring(slash + 1);
                if (hasExtensionCI(name, ".epub") || hasExtensionCI(name, ".kmb"))
                    epubs.push_back({name, file.size()});
                else if (hasExtensionCI(name, ".ttf"))
                    fonts.push_back({name, file.size()});
                file.close();
                file = root.openNextFile();
            }
            root.close();
        }

        Serial.printf("[HTTP] GET /api/books: %u books loaded\n", (unsigned)epubs.size());

        std::vector<String> order;
        loadBookOrder(order);
        applyBookOrderT(order, epubs,
                        [](const BookListItem& item, const String& key) { return item.name == key; });
        for (const auto& f : fonts)
            epubs.push_back(f);

        std::map<String, String> metaMap;
        loadBookMetadata(metaMap);

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        response->print("{\"books\":[");
        bool first = true;
        for (const auto& item : epubs) {
            const String& name = item.name;
            size_t sz = item.size;

            auto itMeta = metaMap.find(name);
            String origName = (itMeta != metaMap.end()) ? itMeta->second : name;

            int totalPages = 0;
            bool isKmb = hasExtensionCI(name, ".kmb");
            if (isKmb) {
                totalPages = PageCountStore::getInstance().getTotal(origName);
                if (totalPages == 0 && sz > 12) {
                    File f = EbookFS.open("/" + name, FILE_READ);
                    if (f) {
                        char magic[5] = {0};
                        f.readBytes(magic, 4);
                        if (strcmp(magic, "KMB1") == 0) {
                            f.seek(10);
                            uint16_t kmbPages = 0;
                            f.read((uint8_t*)&kmbPages, 2);
                            totalPages = kmbPages;
                            PageCountStore::getInstance().set(origName, 0, 0, totalPages);
                        }
                        f.close();
                    }
                }
            } else if (hasExtensionCI(name, ".epub")) {
                totalPages = PageCountStore::getInstance().getTotal(origName);
            }

            int currentPage = 0;
            BookProgress bp;
            if (ProgressStore::getInstance().get(origName, bp)) {
                currentPage = bp.globalPage;
            }

            int percent = 0;
            if (totalPages > 0 && currentPage > 0) {
                percent = (currentPage >= totalPages) ? 100 : (int)((currentPage * 100) / totalPages);
            }

            if (!first) response->print(",");
            first = false;
            response->printf("{\"name\":\"%s\",\"filename\":\"%s\",\"size\":%u,\"page\":%d,\"totalPages\":%d,"
                             "\"percent\":%d}",
                             jsonEscape(origName).c_str(), jsonEscape(name).c_str(), (unsigned)sz,
                             currentPage, totalPages, percent);

            vTaskDelay(pdMS_TO_TICKS(1));
            yield();
        }
        response->print("]}");
        request->send(response);
    });

    AsyncCallbackJsonWebHandler* bookOrderHandler = new AsyncCallbackJsonWebHandler(
        "/api/books/order", [](AsyncWebServerRequest* request, JsonVariant& json) {
            JsonArray arr = json["order"].as<JsonArray>();
            if (arr.isNull()) {
                request->send(400, "text/plain", "Missing 'order' array");
                return;
            }
            std::vector<String> order;
            for (JsonVariant v : arr) {
                String name = v.as<String>();
                if (isSafeBookName(name) && hasExtensionCI(name, ".epub")) {
                    order.push_back(name);
                }
            }
            saveBookOrder(order);
            request->send(200, "application/json", "{\"ok\":true}");
        });
    bookOrderHandler->setMethod(HTTP_POST);
    server->addHandler(bookOrderHandler);

    server->on(
        "/api/books/upload", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            if (g_uploadState.owner != request) {
                if (g_uploadState.owner != nullptr) {
                    request->send(409, "application/json", "{\"ok\":false,\"error\":\"upload in progress\"}");
                } else {
                    request->send(400, "application/json", "{\"ok\":false,\"error\":\"no file provided\"}");
                }
                return;
            }

            switch (g_uploadState.status) {
                case UploadStatus::Ok: {
                    Serial.printf("[HTTP] Upload SUCCESS: '%s'\n", g_uploadState.finalName.c_str());
                    String body = "{\"ok\":true,\"name\":\"" + jsonEscape(g_uploadState.finalName) + "\"}";
                    request->send(200, "application/json", body);
                    break;
                }
                case UploadStatus::BadExtension:
                    Serial.printf("[HTTP] Upload REJECTED: unsupported file extension\n");
                    request->send(415, "application/json",
                                  "{\"ok\":false,\"error\":\"unsupported file type\"}");
                    break;
                case UploadStatus::UnsafeName:
                    Serial.printf("[HTTP] Upload REJECTED: invalid filename\n");
                    request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid filename\"}");
                    break;
                case UploadStatus::NoSpace:
                    Serial.printf("[HTTP] Upload REJECTED: out of storage space\n");
                    request->send(507, "application/json",
                                  "{\"ok\":false,\"error\":\"out of storage space\"}");
                    break;
                case UploadStatus::WriteFailed:
                default:
                    Serial.printf("[HTTP] Upload FAILED (500): storage write failure for '%s'\n",
                                  g_uploadState.finalName.c_str());
                    request->send(500, "application/json",
                                  "{\"ok\":false,\"error\":\"storage write failure\"}");
                    break;
            }
            g_uploadState.reset();
        },
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len,
           bool final) {
            if (index == 0) {
                if (g_uploadState.owner != nullptr && g_uploadState.owner != request) return;

                g_uploadState.reset();
                g_uploadState.owner = request;

                request->onDisconnect([request]() {
                    if (g_uploadState.owner == request) {
                        if (g_uploadState.file) g_uploadState.file.close();
                        if (g_uploadState.tempPath.length()) EbookFS.remove(g_uploadState.tempPath);
                        g_uploadState.reset();
                    }
                });

                String safeName = filename;
                int lastSlash = safeName.lastIndexOf('/');
                if (lastSlash >= 0) safeName = safeName.substring(lastSlash + 1);
                lastSlash = safeName.lastIndexOf('\\');
                if (lastSlash >= 0) safeName = safeName.substring(lastSlash + 1);

                if (safeName.length() > 28) {
                    int dotPos = safeName.lastIndexOf('.');
                    String ext = (dotPos != -1) ? safeName.substring(dotPos) : "";
                    safeName = safeName.substring(0, 28 - ext.length()) + ext;
                }

                KomaBonStorage::ensureReady();
                size_t totalBytes = KomaBonStorage::getTotalBytes();
                size_t usedBytes = KomaBonStorage::getUsedBytes();
                if (totalBytes == 0 && EbookFSPtr == &SD) {
                    SDMgr::getInstance().recover();
                    totalBytes = KomaBonStorage::getTotalBytes();
                    usedBytes = KomaBonStorage::getUsedBytes();
                }

                if (totalBytes == 0) {
                    Serial.println("[HTTP] Upload ERROR: Storage unmounted or unavailable");
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }

                size_t freeBytes = (totalBytes > usedBytes) ? (totalBytes - usedBytes) : 0;
                bool isKmb = hasExtensionCI(safeName, ".kmb");
                UploadVerdict verdict = UploadVerdict::Ok;

                if (isKmb) {
                    if (request->contentLength() > freeBytes)
                        verdict = UploadVerdict::NoSpace;
                    else if (!isSafeBookName(safeName))
                        verdict = UploadVerdict::UnsafeName;
                } else {
                    verdict = checkUpload(safeName, request->contentLength(), freeBytes);
                }

                switch (verdict) {
                    case UploadVerdict::BadExtension:
                        g_uploadState.status = UploadStatus::BadExtension;
                        return;
                    case UploadVerdict::UnsafeName:
                        g_uploadState.status = UploadStatus::UnsafeName;
                        return;
                    case UploadVerdict::NoSpace:
                        g_uploadState.status = UploadStatus::NoSpace;
                        return;
                    case UploadVerdict::Ok:
                        break;
                }

                String testPath = "/" + safeName;
                if (EbookFS.exists(testPath)) {
                    int dotPos = safeName.lastIndexOf('.');
                    String baseName = (dotPos != -1) ? safeName.substring(0, dotPos) : safeName;
                    String ext = (dotPos != -1) ? safeName.substring(dotPos) : "";

                    if (baseName.length() > 20) baseName = baseName.substring(0, 20);

                    int suffix = 1;
                    while (suffix < 100) {
                        safeName = baseName + "_" + String(suffix) + ext;
                        testPath = "/" + safeName;
                        if (!EbookFS.exists(testPath)) break;
                        suffix++;
                    }
                }

                g_uploadState.finalName = safeName;
                g_uploadState.originalName = filename;
                int origSlash = g_uploadState.originalName.lastIndexOf('/');
                if (origSlash >= 0)
                    g_uploadState.originalName = g_uploadState.originalName.substring(origSlash + 1);
                origSlash = g_uploadState.originalName.lastIndexOf('\\');
                if (origSlash >= 0)
                    g_uploadState.originalName = g_uploadState.originalName.substring(origSlash + 1);

                g_uploadState.path = "/" + safeName;
                g_uploadState.tempPath = g_uploadState.path + ".part";
                Serial.printf("[HTTP] Upload START: '%s' (target: '%s', size: %u bytes)\n", filename.c_str(),
                              safeName.c_str(), (unsigned)request->contentLength());

                g_uploadState.file = EbookFS.open(g_uploadState.tempPath, FILE_WRITE);
                if (!g_uploadState.file) {
                    Serial.printf("[HTTP] Upload ERROR: Failed to open '%s' for write on storage\n",
                                  g_uploadState.tempPath.c_str());
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }
                g_uploadState.status = UploadStatus::Ok;
            }

            if (g_uploadState.owner != request || g_uploadState.status != UploadStatus::Ok) return;

            if (g_uploadState.file && len) {
                size_t toWrite = len;
                const uint8_t* ptr = data;
                int retries = 0;
                while (toWrite > 0) {
                    size_t w = g_uploadState.file.write(ptr, toWrite);
                    if (w > 0) {
                        ptr += w;
                        toWrite -= w;
                        retries = 0;
                    } else {
                        retries++;
                        if (retries > 3) {
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(25));
                        yield();
                    }
                    if (toWrite > 0 && w > 0) {
                        vTaskDelay(pdMS_TO_TICKS(10));
                        yield();
                    }
                }

                if (toWrite > 0) {
                    Serial.printf(
                        "[HTTP] Upload ERROR: Write failure at offset %u (attempted %u, wrote %u bytes)\n",
                        (unsigned)index, (unsigned)len, (unsigned)(len - toWrite));
                    g_uploadState.file.close();
                    EbookFS.remove(g_uploadState.tempPath);
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }
                vTaskDelay(pdMS_TO_TICKS(1));
                yield();
            }

            if (final && g_uploadState.file) {
                g_uploadState.file.close();
                if (!EbookFS.rename(g_uploadState.tempPath, g_uploadState.path)) {
                    Serial.printf("[HTTP] Upload ERROR: Failed to rename '%s' to '%s'\n",
                                  g_uploadState.tempPath.c_str(), g_uploadState.path.c_str());
                    EbookFS.remove(g_uploadState.tempPath);
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }
                saveBookMetadata(g_uploadState.finalName, g_uploadState.originalName);
            }
        });

    server->on("/api/books/delete", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("name")) {
            request->send(400, "text/plain", "Missing name param");
            return;
        }

        String filename = request->getParam("name")->value();
        if (deleteSingleBookInternal(filename)) {
            if (EbookFS.exists("/page_totals.json")) EbookFS.remove("/page_totals.json");
            request->send(200, "text/plain", "Deleted");
        } else {
            request->send(500, "text/plain", "Delete failed");
        }
    });

    AsyncCallbackJsonWebHandler* bulkDeleteHandler = new AsyncCallbackJsonWebHandler(
        "/api/library/bulk_delete", [](AsyncWebServerRequest* request, JsonVariant& json) {
            JsonArray arr;
            if (json.is<JsonArray>()) {
                arr = json.as<JsonArray>();
            } else if (json.containsKey("filenames") && json["filenames"].is<JsonArray>()) {
                arr = json["filenames"].as<JsonArray>();
            } else if (json.containsKey("books") && json["books"].is<JsonArray>()) {
                arr = json["books"].as<JsonArray>();
            }

            int deleted = 0;
            int failed = 0;
            for (JsonVariant v : arr) {
                String fname = v.as<String>();
                if (deleteSingleBookInternal(fname)) {
                    deleted++;
                } else {
                    failed++;
                }
                vTaskDelay(pdMS_TO_TICKS(2));
                yield();
            }

            if (deleted > 0 && EbookFS.exists("/page_totals.json")) {
                EbookFS.remove("/page_totals.json");
            }

            Serial.printf("[HTTP] Bulk delete: %d deleted, %d failed\n", deleted, failed);

            AsyncResponseStream* response = request->beginResponseStream("application/json");
            response->printf("{\"status\":\"ok\",\"deleted\":%d,\"failed\":%d}", deleted, failed);
            request->send(response);
        });
    server->addHandler(bulkDeleteHandler);

    server->on("/api/reader/progress", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(1024);

        ProgressStore& store = ProgressStore::getInstance();
        String last = store.lastBook();

        doc["exists"] = last.length() > 0;
        doc["lastBook"] = last;
        doc["displayName"] = last;
        doc["resumeOnBoot"] = store.resumeOnBoot();

        BookProgress p;
        if (last.length() > 0 && store.get(last, p)) {
            doc["chapter"] = p.chapter;
            doc["page"] = p.globalPage;
        }

        serializeJson(doc, *response);
        request->send(response);
    });

    server->on("/api/reader/progress", HTTP_DELETE, [](AsyncWebServerRequest* request) {
        ProgressStore::getInstance().clearAll();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server->on("/api/library/export", HTTP_GET, [](AsyncWebServerRequest* request) {
        AsyncResponseStream* response = request->beginResponseStream("application/json");
        response->addHeader("Content-Disposition", "attachment; filename=\"komabon-state.json\"");

        // 1. Header Stream
        response->print("{\"komabon\":{\"schema\":");
        response->print(PROGRESS_SCHEMA_CURRENT);
        response->print(",\"version\":\"");
        response->print(SYSTEM_VERSION);
        response->print("\"},\"progress\":{");

        // 2. Progress Stream (Direct OOM Bypass)
        ProgressStore::getInstance().streamExportJson(response);

        // 3. Meta Stream
        response->print("},\"meta\":{");
        std::map<String, String> metadata;
        loadBookMetadata(metadata);
        bool firstMeta = true;
        for (const auto& kv : metadata) {
            if (!firstMeta) response->print(",");
            firstMeta = false;
            DynamicJsonDocument k(512), v(512);
            k.set(kv.first);
            v.set(kv.second);
            serializeJson(k, *response);
            response->print(":");
            serializeJson(v, *response);
        }

        // 4. Order Stream
        response->print("},\"order\":[");
        std::vector<String> order;
        loadBookOrder(order);
        bool firstOrder = true;
        for (const String& filename : order) {
            if (!firstOrder) response->print(",");
            firstOrder = false;
            DynamicJsonDocument v(512);
            v.set(getOriginalFilename(filename));
            serializeJson(v, *response);
        }

        response->print("]}");
        request->send(response);
    });

    server->on(
        "/api/library/import", HTTP_POST,
        [](AsyncWebServerRequest* request) {
            if (g_importState.owner != nullptr && g_importState.owner != request) {
                request->send(409, "application/json",
                              "{\"status\":\"error\",\"message\":\"Import in progress\"}");
                return;
            }
            if (g_importState.tooBig) {
                g_importState.reset();
                request->send(413, "application/json",
                              "{\"status\":\"error\",\"message\":\"Bundle > 64KB\"}");
                return;
            }
            if (!g_importState.received) {
                g_importState.reset();
                request->send(400, "application/json",
                              "{\"status\":\"error\",\"message\":\"No file received\"}");
                return;
            }

            ImportOutcome outcome = applyImportBundle(IMPORT_TMP_PATH);
            g_importState.reset();
            EbookFS.remove(IMPORT_TMP_PATH);

            if (!outcome.ok) {
                AsyncResponseStream* r = request->beginResponseStream("application/json");
                DynamicJsonDocument doc(256);
                doc["status"] = "error";
                doc["message"] = outcome.error;
                serializeJson(doc, *r);
                request->send(r);
                return;
            }

            AsyncResponseStream* response = request->beginResponseStream("application/json");
            DynamicJsonDocument doc(256);
            doc["status"] = "ok";
            doc["merged"] = outcome.report.merged;
            doc["added"] = outcome.report.added;
            doc["pending"] = outcome.report.pending;
            doc["skipped"] = outcome.report.skipped;
            doc["orderApplied"] = outcome.orderApplied;
            doc["metaAdded"] = outcome.metaAdded;
            serializeJson(doc, *response);
            request->send(response);
        },
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len,
           bool final) {
            if (index == 0) {
                if (g_importState.owner != nullptr && g_importState.owner != request) return;
                g_importState.reset();
                g_importState.owner = request;

                request->onDisconnect([request]() {
                    if (g_importState.owner == request) {
                        if (g_importState.file) g_importState.file.close();
                        EbookFS.remove(IMPORT_TMP_PATH);
                        g_importState.reset();
                    }
                });

                EbookFS.remove(IMPORT_TMP_PATH);
                g_importState.file = EbookFS.open(IMPORT_TMP_PATH, FILE_WRITE);
            }

            if (g_importState.owner != request) return;

            g_importState.size += len;
            if (g_importState.size > IMPORT_MAX_BYTES) {
                g_importState.tooBig = true;
                if (g_importState.file) g_importState.file.close();
                EbookFS.remove(IMPORT_TMP_PATH);
                return;
            }

            if (g_importState.file && len) g_importState.file.write(data, len);

            if (final) {
                if (g_importState.file) g_importState.file.close();
                g_importState.received = !g_importState.tooBig;
            }
        });
}