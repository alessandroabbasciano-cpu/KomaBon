#ifndef EPUB_LOADER_H
#define EPUB_LOADER_H

#include <Arduino.h>
#include <vector>
#include <map>
#include <unzipLIB.h>

// Text formatting enums
#include "HtmlParser.h"

class EpubLoader {
  public:
    EpubLoader();
    ~EpubLoader();
    bool open(const char* path);
    void close();

    // Metadata getters
    String getTitle();
    String getAuthor();
    String getPublisher();
    String getLanguage();
    String getPublicationDate();
    String getISBN();

    // Content getters
    int getChapterCount();
    String getChapterContent(int index); // Legacy plain text
    std::vector<ContentNode>
    getChapterContentRich(int index, volatile bool* abortFlag = nullptr); // Rich formatted content

    // Extracted for lock-free background pagination
    String getChapterRawHtml(int index, String& outChapterDir);

    uint8_t* getCoverImageData(size_t* outSize); // Fetch cover image bytes

    // File fetching
    uint8_t* getFileData(String path, size_t* outSize);
    uint8_t* getRawZipData(const String& path, size_t* outSize);

  private:
    String readFileFromZip(const char* path);

    // Metadata
    String bookTitle;
    String bookAuthor;
    String bookPublisher;
    String bookLanguage;
    String bookPubDate;
    String bookISBN;

    // Paths
    String epubPath;
    String opfPath;
    String rootDir;   // Directory of the OPF file
    String coverHref; // Path to the cover image inside the ZIP

    struct SpineItem {
        String id;
        String href;
    };

    std::vector<SpineItem> spine;
    std::map<String, String> manifest; // id -> href

    // Allocate UNZIP in PSRAM to avoid memory issues with the 41KB internal buffer
    UNZIP* zip;

    // Helper to parse XML for specific attribute
    String extractAttribute(const String& xml, const String& tag, const String& attr);
    // Helper to extract metadata from OPF
    String extractMetadata(const String& xml, const String& tag);

    // Helper to read file from zip

    bool parseContainer();
    bool parseOpf();
};

#endif
