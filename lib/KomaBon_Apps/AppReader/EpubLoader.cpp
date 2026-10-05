#include "EpubLoader.h"
#include "KomaBonFS.h"
#include "FontMgr.h"
#include <LittleFS.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <algorithm>

#ifndef ZIP_SUCCESS
#define ZIP_SUCCESS 0
#endif

void* myOpen(const char* filename, int32_t* size) {
    String fullPath = filename;
    if (!fullPath.startsWith("/littlefs") && !fullPath.startsWith("/ebooks"))
        fullPath = "/littlefs" + fullPath;
    int fd = open(fullPath.c_str(), O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return NULL;
    }
    *size = st.st_size;
    return (void*)(intptr_t)(fd + 1);
}

void myClose(void* p) {
    int fd = (int)(intptr_t)p - 1;
    if (fd >= 0) {
        close(fd);
    }
}

int32_t myRead(void* p, uint8_t* buffer, int32_t length) {
    int fd = (int)(intptr_t)p - 1;
    if (fd < 0 || !buffer || length <= 0) return -1;
    return (int32_t)read(fd, buffer, length);
}

int32_t mySeek(void* p, int32_t position, int iType) {
    int fd = (int)(intptr_t)p - 1;
    if (fd < 0) return -1;
    return (int32_t)lseek(fd, position, iType);
}

EpubLoader::EpubLoader() {
    zip = (UNZIP*)ps_malloc(sizeof(UNZIP));
    if (!zip)
        zip = new (std::nothrow) UNZIP();
    else
        new (zip) UNZIP();
}

EpubLoader::~EpubLoader() {
    close();
    if (zip) {
        zip->~UNZIP();
        free(zip);
        zip = nullptr;
    }
}

bool EpubLoader::open(const char* path) {
    epubPath = String(path);
    if (zip->openZIP(path, myOpen, myClose, myRead, mySeek) != ZIP_SUCCESS) return false;
    if (!parseContainer()) {
        close();
        return false;
    }
    if (!parseOpf()) {
        close();
        return false;
    }
    return true;
}

void EpubLoader::close() {
    if (zip) zip->closeZIP();
    spine.clear();
    manifest.clear();
    coverHref = "";
}

String EpubLoader::getTitle() {
    return bookTitle;
}

int EpubLoader::getChapterCount() {
    return spine.size();
}

String EpubLoader::getChapterContent(int index) {
    if (index < 0 || index >= (int)spine.size()) return "";
    String href = spine[index].href;
    String fullPath = rootDir + href;
    if (fullPath.startsWith("./")) fullPath = fullPath.substring(2);
    String content = readFileFromZip(fullPath.c_str());
    if (content.length() == 0) return "";

    String clean;
    clean.reserve(content.length());
    bool inTag = false, skipContent = false;
    String currentTag;

    for (int i = 0; i < (int)content.length(); i++) {
        char c = content.charAt(i);
        if (c == '<') {
            inTag = true;
            currentTag = "";
            int j = i + 1;
            while (j < (int)content.length() && content.charAt(j) != '>' && content.charAt(j) != ' ' &&
                   j - i < 20) {
                currentTag += (char)tolower(content.charAt(j));
                j++;
            }

            if (currentTag == "p" || currentTag == "/p" || currentTag == "div" || currentTag == "/div" ||
                currentTag == "br" || currentTag == "br/" || currentTag.startsWith("h")) {
                if (clean.length() > 0 && clean.charAt(clean.length() - 1) != '\n') clean += "\n";
            } else if (currentTag == "li") {
                if (clean.length() > 0 && clean.charAt(clean.length() - 1) != '\n') clean += "\n";
                clean += "• ";
            } else if (currentTag == "img" || currentTag == "svg" || currentTag == "figure" ||
                       currentTag == "image") {
                // Skip
            } else if (currentTag == "/figure" || currentTag == "/svg") {
                // Skip
            } else if (currentTag == "script" || currentTag == "style" || currentTag == "head")
                skipContent = true;
            else if (currentTag == "/script" || currentTag == "/style" || currentTag == "/head")
                skipContent = false;
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag && !skipContent) {
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
            clean += c;
        }
    }

    clean.replace("¶Ç8", " -- ");
    clean.replace("¶ÇÖ", "'");
    clean.replace("¶Çö", "'");
    clean.replace("¶Ç£", "\"");
    clean.replace("¶Ç¥", "\"");
    clean.replace("¶Çª", "-");
    clean.replace("¶ÇÜ", "...");
    clean.replace("¶Ç", "");

    clean.replace("\xE2\x80\x9C", "\"");
    clean.replace("\xE2\x80\x9D", "\"");
    clean.replace("\xE2\x80\x98", "'");
    clean.replace("\xE2\x80\x99", "'");
    clean.replace("\xE2\x80\x94", " -- ");
    clean.replace("\xE2\x80\x93", " - ");
    clean.replace("\xE2\x80\xA6", "...");

    clean.replace("\n,", ",");
    clean.replace("\n.", ".");
    clean.replace("\n?", "?");
    clean.replace("\n!", "!");
    clean.replace("\n\"", "\"");
    clean.replace("\n'", "'");

    while (clean.indexOf("  ") != -1)
        clean.replace("  ", " ");

    clean.trim();
    return clean;
}

bool EpubLoader::parseContainer() {
    String xml = readFileFromZip("META-INF/container.xml");
    if (xml.length() == 0) return false;
    opfPath = extractAttribute(xml, "rootfile", "full-path");
    if (opfPath.length() == 0) return false;
    int lastSlash = opfPath.lastIndexOf('/');
    if (lastSlash != -1)
        rootDir = opfPath.substring(0, lastSlash + 1);
    else
        rootDir = "";
    return true;
}

bool EpubLoader::parseOpf() {
    String xml = readFileFromZip(opfPath.c_str());
    if (xml.length() == 0) return false;
    bookTitle = extractMetadata(xml, "dc:title");
    if (bookTitle.length() == 0) bookTitle = extractMetadata(xml, "title");

    String epub2CoverId = "";
    int metaPos = 0;
    while (true) {
        int metaStart = xml.indexOf("<meta ", metaPos);
        if (metaStart == -1) break;
        int metaEnd = xml.indexOf(">", metaStart);
        if (metaEnd == -1) break;
        String metaTag = xml.substring(metaStart, metaEnd + 1);
        if (extractAttribute(metaTag, "meta", "name") == "cover") {
            epub2CoverId = extractAttribute(metaTag, "meta", "content");
            break;
        }
        metaPos = metaEnd + 1;
    }

    int manifestStart = xml.indexOf("<manifest");
    int manifestEnd = xml.indexOf("</manifest>");
    if (manifestStart == -1 || manifestEnd == -1) return false;

    String manifestBlock = xml.substring(manifestStart, manifestEnd);
    int pos = 0;
    while (true) {
        int itemStart = manifestBlock.indexOf("<item", pos);
        if (itemStart == -1) break;
        int itemEnd = manifestBlock.indexOf(">", itemStart);
        if (itemEnd == -1) break;

        String itemTag = manifestBlock.substring(itemStart, itemEnd + 1);
        String id = extractAttribute(itemTag, "item", "id");
        String href = extractAttribute(itemTag, "item", "href");
        String properties = extractAttribute(itemTag, "item", "properties");

        if (id.length() > 0 && href.length() > 0) {
            manifest[id] = href;
            String hrefLower = href;
            hrefLower.toLowerCase();
            String idLower = id;
            idLower.toLowerCase();

            bool isImageFile = hrefLower.endsWith(".jpg") || hrefLower.endsWith(".jpeg") ||
                               hrefLower.endsWith(".png") || hrefLower.endsWith(".raw");

            if (properties.indexOf("cover-image") != -1 && isImageFile) {
                coverHref = href;
            } else if (epub2CoverId.length() > 0 && id == epub2CoverId && isImageFile) {
                coverHref = href;
            } else if (coverHref.length() == 0 && isImageFile) {
                static const char* const COVER_KEYWORDS[] = {"cover", "copertina", "couverture", "portada",
                                                             "capa",  "titelbild", "umschlag"};

                const size_t keywordCount = sizeof(COVER_KEYWORDS) / sizeof(COVER_KEYWORDS[0]);

                for (size_t k = 0; k < keywordCount; ++k) {
                    const char* kw = COVER_KEYWORDS[k];
                    if (hrefLower.indexOf(kw) != -1 || idLower.indexOf(kw) != -1) {
                        coverHref = href;
                        break;
                    }
                }
            }
        }
        pos = itemEnd + 1;
    }

    int spineStart = xml.indexOf("<spine"), spineEnd = xml.indexOf("</spine>");
    if (spineStart == -1 || spineEnd == -1) return false;
    String spineBlock = xml.substring(spineStart, spineEnd);
    pos = 0;
    while (true) {
        int itemRefStart = spineBlock.indexOf("<itemref", pos);
        if (itemRefStart == -1) break;
        int itemRefEnd = spineBlock.indexOf(">", itemRefStart);
        if (itemRefEnd == -1) break;
        String itemRefTag = spineBlock.substring(itemRefStart, itemRefEnd + 1);
        String idref = extractAttribute(itemRefTag, "itemref", "idref");
        if (idref.length() > 0 && manifest.count(idref)) {
            SpineItem item;
            item.id = idref;
            item.href = manifest[idref];
            spine.push_back(item);
        }
        pos = itemRefEnd + 1;
    }

    return true;
}

uint8_t* EpubLoader::getRawZipData(const String& path, size_t* outSize) {
    if (path.length() == 0) return nullptr;
    if (zip->locateFile(path.c_str()) != 0) return nullptr;
    if (zip->openCurrentFile() != 0) return nullptr;

    unz_file_info fileInfo;
    zip->getFileInfo(&fileInfo, nullptr, 0, NULL, 0, NULL, 0);
    size_t size = fileInfo.uncompressed_size;

    if (size == 0) {
        zip->closeCurrentFile();
        return nullptr;
    }

    uint8_t* buffer = (uint8_t*)ps_malloc(size + 32);
    if (!buffer) buffer = (uint8_t*)malloc(size + 32);
    if (!buffer) {
        zip->closeCurrentFile();
        return nullptr;
    }

    memset(buffer, 0, size + 32);

    size_t totalRead = 0;
    while (totalRead < size) {
        int toRead = (size - totalRead > 1024) ? 1024 : (size - totalRead);
        int bytesRead = zip->readCurrentFile(buffer + totalRead, toRead);
        if (bytesRead <= 0) break;
        totalRead += bytesRead;
        yield();
    }

    zip->closeCurrentFile();
    *outSize = totalRead;
    return buffer;
}

String EpubLoader::extractAttribute(const String& xml, const String& tag, const String& attr) {
    int attrStart = xml.indexOf(attr + "=\"");
    if (attrStart == -1) attrStart = xml.indexOf(attr + "='");
    if (attrStart == -1) return "";
    int valStart = attrStart + attr.length() + 2;
    char quote = xml.charAt(attrStart + attr.length() + 1);
    int valEnd = xml.indexOf(quote, valStart);
    if (valEnd == -1) return "";
    return xml.substring(valStart, valEnd);
}

String EpubLoader::extractMetadata(const String& xml, const String& tag) {
    int tagStart = xml.indexOf("<" + tag);
    if (tagStart == -1) return "";
    int tagEnd = xml.indexOf(">", tagStart);
    if (tagEnd == -1) return "";
    int contentEnd = xml.indexOf("</" + tag + ">", tagEnd);
    if (contentEnd == -1) contentEnd = xml.indexOf("</", tagEnd);
    if (contentEnd == -1) return "";
    String content = xml.substring(tagEnd + 1, contentEnd);
    content.trim();
    return content;
}

static const int KOMABON_MAX_ZIP_TEXT_BYTES = 256 * 1024;

String EpubLoader::readFileFromZip(const char* path) {
    if (zip->locateFile(path) != ZIP_SUCCESS) return "";
    if (zip->openCurrentFile() != ZIP_SUCCESS) return "";

    unz_file_info fileInfo;
    char szName[256];
    zip->getFileInfo(&fileInfo, szName, sizeof(szName), NULL, 0, NULL, 0);
    int size = fileInfo.uncompressed_size;

    if (size > KOMABON_MAX_ZIP_TEXT_BYTES) {
        Serial.printf("EpubLoader: %s has %d bytes; truncating to %d\n", path, size,
                      KOMABON_MAX_ZIP_TEXT_BYTES);
        size = KOMABON_MAX_ZIP_TEXT_BYTES;
    }

    char* rawBuffer = (char*)ps_malloc(size + 1);
    if (!rawBuffer) rawBuffer = (char*)malloc(size + 1);

    if (!rawBuffer) {
        Serial.println("EpubLoader: FATAL - Memory allocation failed for chapter text!");
        zip->closeCurrentFile();
        return "";
    }

    int totalRead = 0;
    int remaining = size;
    while (remaining > 0) {
        int toRead = remaining > 2048 ? 2048 : remaining;
        int bytesRead = zip->readCurrentFile((uint8_t*)(rawBuffer + totalRead), toRead);
        if (bytesRead <= 0) break;
        totalRead += bytesRead;
        remaining -= bytesRead;
        yield();
    }

    rawBuffer[totalRead] = '\0';
    String str(rawBuffer);
    free(rawBuffer);
    zip->closeCurrentFile();
    return str;
}

String EpubLoader::getAuthor() {
    return bookAuthor;
}
String EpubLoader::getPublisher() {
    return bookPublisher;
}
String EpubLoader::getLanguage() {
    return bookLanguage;
}
String EpubLoader::getPublicationDate() {
    return bookPubDate;
}
String EpubLoader::getISBN() {
    return bookISBN;
}

std::vector<ContentNode> EpubLoader::getChapterContentRich(int index, volatile bool* abortFlag) {
    if (index < 0 || index >= (int)spine.size()) return std::vector<ContentNode>();
    String chapterDir;
    String content = getChapterRawHtml(index, chapterDir);
    return HtmlParser::parseHtmlToRichContent(content, chapterDir, abortFlag);
}

String EpubLoader::getChapterRawHtml(int index, String& outChapterDir) {
    if (index < 0 || index >= (int)spine.size()) return "";
    String href = spine[index].href;
    String fullPath = rootDir + href;
    if (fullPath.startsWith("./")) fullPath = fullPath.substring(2);
    String content = readFileFromZip(fullPath.c_str());

    outChapterDir = "";
    int slash = href.lastIndexOf('/');
    if (slash != -1) outChapterDir = href.substring(0, slash + 1);

    return content;
}

uint8_t* EpubLoader::getFileData(String path, size_t* outSize) {
    String fullPath = rootDir + path;

    while (fullPath.indexOf("../") != -1) {
        int dotdot = fullPath.indexOf("../");
        if (dotdot == 0) {
            fullPath = fullPath.substring(3);
        } else {
            int prevSlash = fullPath.lastIndexOf('/', dotdot - 2);
            if (prevSlash == -1) {
                fullPath = fullPath.substring(dotdot + 3);
            } else {
                fullPath = fullPath.substring(0, prevSlash + 1) + fullPath.substring(dotdot + 3);
            }
        }
    }

    if (fullPath.startsWith("./")) fullPath = fullPath.substring(2);
    return getRawZipData(fullPath, outSize);
}

uint8_t* EpubLoader::getCoverImageData(size_t* outSize) {
    if (coverHref.length() == 0) return nullptr;
    return getFileData(coverHref, outSize);
}
