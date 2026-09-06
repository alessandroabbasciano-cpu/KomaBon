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

// --- Helper Functions & State ---

static const char* BOOK_ORDER_PATH = "/book_order.json";
static const char* IMPORT_TMP_PATH = "/import.tmp";
static const size_t IMPORT_MAX_BYTES = 64 * 1024;

static void loadBookOrder(std::vector<String>& order) {
    order.clear();
    File f = SystemFS.open(BOOK_ORDER_PATH, FILE_READ);
    if (!f) return;
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
    File f = SystemFS.open(BOOK_ORDER_PATH, FILE_WRITE);
    if (f) {
        serializeJson(doc, f);
        f.close();
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

    int schema = doc["book32"]["schema"] | 0;
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

void setupBookEndpoints(AsyncWebServer* server) {
    server->on("/api/books", HTTP_GET, [](AsyncWebServerRequest* request) {
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

        std::vector<String> epubs, fonts;
        File root = EbookFS.open("/");
        if (root && root.isDirectory()) {
            File file = root.openNextFile();
            while (file) {
                String name = file.name();
                if (hasExtensionCI(name, ".epub") || hasExtensionCI(name, ".kmb"))
                    epubs.push_back(name);
                else if (hasExtensionCI(name, ".ttf"))
                    fonts.push_back(name);
                file.close();
                file = root.openNextFile();
            }
            root.close();
        }

        std::vector<String> order;
        loadBookOrder(order);
        applyBookOrder(order, epubs);
        for (const String& f : fonts)
            epubs.push_back(f);

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        response->print("{\"books\":[");
        bool first = true;
        for (const String& name : epubs) {
            File f = EbookFS.open("/" + name, FILE_READ);
            size_t sz = f ? f.size() : 0;
            if (f) f.close();
            if (!first) response->print(",");
            first = false;
            response->printf("{\"name\":\"%s\",\"filename\":\"%s\",\"size\":%u}",
                             jsonEscape(getOriginalFilename(name)).c_str(), jsonEscape(name).c_str(),
                             (unsigned)sz);
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
                    String body = "{\"ok\":true,\"name\":\"" + jsonEscape(g_uploadState.finalName) + "\"}";
                    request->send(200, "application/json", body);
                    break;
                }
                case UploadStatus::BadExtension:
                    request->send(415, "application/json",
                                  "{\"ok\":false,\"error\":\"unsupported file type\"}");
                    break;
                case UploadStatus::UnsafeName:
                    request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid filename\"}");
                    break;
                case UploadStatus::NoSpace:
                    request->send(507, "application/json",
                                  "{\"ok\":false,\"error\":\"out of storage space\"}");
                    break;
                case UploadStatus::WriteFailed:
                default:
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

                size_t freeBytes = EbookFS_totalBytes() - EbookFS_usedBytes();
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
                g_uploadState.file = EbookFS.open(g_uploadState.tempPath, FILE_WRITE);
                if (!g_uploadState.file) {
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }
                g_uploadState.status = UploadStatus::Ok;
            }

            if (g_uploadState.owner != request || g_uploadState.status != UploadStatus::Ok) return;

            if (g_uploadState.file && len) {
                if (g_uploadState.file.write(data, len) != len) {
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
        if (!isSafeBookName(filename)) {
            request->send(400, "text/plain", "Invalid name");
            return;
        }

        String path = "/" + filename;
        if (EbookFS.exists(path)) {
            if (EbookFS.remove(path)) {
                removeBookProgress(filename);
                removeFromBookOrder(filename);
                removeBookMetadata(filename);

                // Clear page counts so re-uploaded books get a fresh count calculation
                if (EbookFS.exists("/page_totals.json")) EbookFS.remove("/page_totals.json");

                int dot = filename.lastIndexOf('.');
                String base = (dot > 0) ? filename.substring(0, dot) : filename;
                const char* derivedExts[] = {".thumb", ".cover", ".cover2"};
                for (const char* ext : derivedExts) {
                    String derived = "/covers/" + base + ext;
                    if (EbookFS.exists(derived)) EbookFS.remove(derived);
                }
                request->send(200, "text/plain", "Deleted");
            } else {
                request->send(500, "text/plain", "Delete failed");
            }
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });

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
        std::map<String, String> metadata;
        loadBookMetadata(metadata);

        std::vector<String> order;
        loadBookOrder(order);

        size_t capacity =
            1024 + ProgressStore::getInstance().count() * 224 + metadata.size() * 160 + order.size() * 96;
        DynamicJsonDocument doc(capacity);

        JsonObject header = doc.createNestedObject("book32");
        header["schema"] = PROGRESS_SCHEMA_CURRENT;
        header["version"] = SYSTEM_VERSION;

        ProgressStore::getInstance().fillExportJson(doc.createNestedObject("progress"));

        JsonObject meta = doc.createNestedObject("meta");
        for (const auto& kv : metadata)
            meta[kv.second] = kv.second;

        JsonArray arr = doc.createNestedArray("order");
        for (const String& filename : order)
            arr.add(getOriginalFilename(filename));

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        response->addHeader("Content-Disposition", "attachment; filename=\"book32-state.json\"");
        serializeJson(doc, *response);
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