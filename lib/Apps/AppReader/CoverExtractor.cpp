#include "CoverExtractor.h"
#include "AppReader.h"
#include "KBReader.h"
#include "EpubLoader.h"
#include "KomaBonFS.h"

bool CoverExtractor::processNextCover(std::vector<BookEntry>& books) {
    for (auto& book : books) {
        if (book.coverAttempted) continue;

        String thumbPath = "/covers/" + book.baseName + ".thumb";
        String coverPath = "/covers/" + book.baseName + ".cover";

        if (book.hasCoverThumb && SystemFS.exists(thumbPath) && SystemFS.exists(coverPath)) {
            book.coverAttempted = true;
            continue;
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
                    uint8_t mainCover[2400] = {0};

                    for (int ty = 0; ty < 80; ty++) {
                        int sy = ty * h / 80;
                        for (int tx = 0; tx < 60; tx++) {
                            int sx = tx * w / 60;
                            int srcByte = sy * ((w + 7) / 8) + (sx / 8);
                            int srcBit = 7 - (sx % 8);
                            if ((pageBuf[srcByte] & (1 << srcBit)) != 0) {
                                thumb[ty * 8 + (tx / 8)] |= (1 << (7 - (tx % 8)));
                            }
                        }
                    }

                    for (int ty = 0; ty < 160; ty++) {
                        int sy = ty * h / 160;
                        for (int tx = 0; tx < 120; tx++) {
                            int sx = tx * w / 120;
                            int srcByte = sy * ((w + 7) / 8) + (sx / 8);
                            int srcBit = 7 - (sx % 8);
                            if ((pageBuf[srcByte] & (1 << srcBit)) != 0) {
                                mainCover[ty * 15 + (tx / 8)] |= (1 << (7 - (tx % 8)));
                            }
                        }
                    }

                    File f1 = SystemFS.open(thumbPath, "w");
                    if (f1) {
                        f1.write(thumb, 640);
                        f1.close();
                    }

                    File f2 = SystemFS.open(coverPath, "w");
                    if (f2) {
                        f2.write(mainCover, 2400);
                        f2.close();
                    }

                    generated = true;
                }
                if (pageBuf) free(pageBuf);
            }
            delete kb;

        } else {
            EpubLoader* epub = new EpubLoader();
            String fullPath = "/ebooks" + book.path;

            if (epub->open(fullPath.c_str())) {
                size_t thumbSize = 0, coverSize = 0;
                uint8_t* thumbData = epub->getFontData("cover_thumb.raw", &thumbSize);
                uint8_t* coverData = epub->getFontData("cover_main.raw", &coverSize);

                if (thumbData && thumbSize == 640) {
                    File f = SystemFS.open(thumbPath, "w");
                    if (f) {
                        f.write(thumbData, 640);
                        f.close();
                    }
                    generated = true;
                }

                if (coverData && coverSize == 2400) {
                    File f = SystemFS.open(coverPath, "w");
                    if (f) {
                        f.write(coverData, 2400);
                        f.close();
                    }
                }

                if (thumbData) free(thumbData);
                if (coverData) free(coverData);
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