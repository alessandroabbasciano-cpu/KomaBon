#include "CoverExtractor.h"
#include "AppReader.h"
#include "KBReader.h"
#include "EpubLoader.h"
#include "KomaBonFS.h"

static bool isThumbnailPopulated(const uint8_t* buffer, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (buffer[i] != 0) return true;
    }
    return false;
}

bool CoverExtractor::processNextCover(std::vector<BookEntry>& books) {
    for (auto& book : books) {
        if (book.coverAttempted) continue;

        String thumbPath = "/covers/" + book.baseName + ".thumb";

        // Check internal SystemFS for covers
        if (book.hasCoverThumb && SystemFS.exists(thumbPath)) {
            File testF = SystemFS.open(thumbPath, "r");
            if (testF) {
                uint8_t buf[640];
                size_t bytesRead = testF.read(buf, sizeof(buf));
                testF.close();
                if (bytesRead == 640 && isThumbnailPopulated(buf, 640)) {
                    book.coverAttempted = true;
                    continue;
                }
            }
            SystemFS.remove(thumbPath);
            book.hasCoverThumb = false;
        }

        book.coverAttempted = true;
        bool generated = false;

        if (book.path.endsWith(".kmb")) {
            KBReader* kb = new KBReader();
            if (kb->open(book.path.c_str())) {
                uint16_t w = kb->getWidth();
                uint16_t h = kb->getHeight();
                size_t bufSize = (w + 7) / 8 * h;
                uint8_t* pageBuf = (uint8_t*)ps_malloc(bufSize);

                if (pageBuf && kb->readPage(0, pageBuf)) {
                    uint8_t thumb[640] = {0};
                    for (int ty = 0; ty < 80; ty++) {
                        int sy = ty * h / 80;
                        for (int tx = 0; tx < 60; tx++) {
                            int sx = tx * w / 60;
                            int srcByte = sy * ((w + 7) / 8) + (sx / 8);
                            int srcBit = 7 - (sx % 8);
                            bool isBlack = (pageBuf[srcByte] & (1 << srcBit)) != 0;
                            if (isBlack) {
                                int dstByte = ty * 8 + (tx / 8);
                                int dstBit = 7 - (tx % 8);
                                thumb[dstByte] |= (1 << dstBit);
                            }
                        }
                    }

                    File f = SystemFS.open(thumbPath, "w");
                    if (f) {
                        f.write(thumb, 640);
                        f.close();
                        generated = true;
                    }
                }
                if (pageBuf) free(pageBuf);
            }
            delete kb;

        } else {
            // EPUB logic: "Zero-Decoding" extraction
            EpubLoader* epub = new EpubLoader();
            String fullPath = "/ebooks" + book.path;

            if (epub->open(fullPath.c_str())) {
                size_t thumbSize = 0;
                // FIX: Use getFontData to bypass the OPF rootDir and fetch from the ZIP root
                uint8_t* thumbData = epub->getFontData("cover_thumb.raw", &thumbSize);

                if (thumbData && thumbSize == 640) {
                    File f = SystemFS.open(thumbPath, "w");
                    if (f) {
                        f.write(thumbData, 640);
                        f.close();
                        generated = true;
                    }
                } else {
                    Serial.println("CoverExtractor: 'cover_thumb.raw' not found in optimized EPUB.");
                }

                if (thumbData) free(thumbData);
            }
            delete epub;
        }

        if (generated) {
            book.hasCoverThumb = true;
            return true;
        }
    }
    return false;
}