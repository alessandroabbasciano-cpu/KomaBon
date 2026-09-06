#include "CoverExtractor.h"
#include "AppReader.h"
#include "KBReader.h"
#include "EpubLoader.h"
#include "KomaBonFS.h"
#include <JPEGDEC.h>

struct ThumbDecodeContext {
    uint8_t thumb[640];
    int scaledW;
    int scaledH;
};

// Callback executed by JPEGDEC for each MCU block in ONE_BIT_DITHERED mode
static int thumbDitherCallback(JPEGDRAW* pDraw) {
    ThumbDecodeContext* ctx = (ThumbDecodeContext*)pDraw->pUser;
    uint8_t* src = (uint8_t*)pDraw->pPixels;
    int bytesPerRow = (pDraw->iWidth + 7) / 8;

    for (int y = 0; y < pDraw->iHeight; y++) {
        int sy = pDraw->y + y;
        if (sy >= ctx->scaledH) continue;
        int ty = (sy * 80) / ctx->scaledH;

        for (int x = 0; x < pDraw->iWidth; x++) {
            int sx = pDraw->x + x;
            if (sx >= ctx->scaledW) continue;
            int tx = (sx * 60) / ctx->scaledW;

            // In JPEGDEC ONE_BIT_DITHERED mode: bit 0 = black pixel, bit 1 = white pixel
            int srcByte = y * bytesPerRow + (x / 8);
            int srcBit = 7 - (x % 8);
            bool isBlack = !(src[srcByte] & (1 << srcBit));

            if (isBlack) {
                int dstByte = ty * 8 + (tx / 8);
                int dstBit = 7 - (tx % 8);
                ctx->thumb[dstByte] |= (1 << dstBit);
            }
        }
    }
    return 1;
}

// Validates that a thumbnail buffer contains black pixels and is not entirely blank
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

        // If a thumbnail file exists, verify that it is valid and not completely white
        if (book.hasCoverThumb && EbookFS.exists(thumbPath)) {
            File testF = EbookFS.open(thumbPath, "r");
            if (testF) {
                uint8_t buf[640];
                size_t bytesRead = testF.read(buf, sizeof(buf));
                testF.close();
                if (bytesRead == 640 && isThumbnailPopulated(buf, 640)) {
                    book.coverAttempted = true;
                    continue;
                }
            }
            // Remove corrupted or completely white thumbnail to force regeneration
            EbookFS.remove(thumbPath);
            book.hasCoverThumb = false;
        }

        book.coverAttempted = true;
        bool generated = false;

        if (book.path.endsWith(".kmb")) {
            // --- KMB COMIC EXTRACTION ---
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

                    File f = EbookFS.open(thumbPath, "w");
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
            // --- EPUB BOOK EXTRACTION ---
            EpubLoader* epub = new EpubLoader();
            String fullPath = "/ebooks" + book.path;

            if (epub->open(fullPath.c_str())) {
                size_t imgSize = 0;
                uint8_t* imgData = epub->getCoverImageData(&imgSize);

                if (imgData && imgSize > 0) {
                    JPEGDEC* jpeg = new JPEGDEC();

                    if (jpeg->openRAM(imgData, imgSize, thumbDitherCallback)) {
                        int imgW = jpeg->getWidth();
                        int imgH = jpeg->getHeight();

                        int scale = 0;
                        if (imgW / 8 >= 60 && imgH / 8 >= 80)
                            scale = JPEG_SCALE_EIGHTH;
                        else if (imgW / 4 >= 60 && imgH / 4 >= 80)
                            scale = JPEG_SCALE_QUARTER;
                        else if (imgW / 2 >= 60 && imgH / 2 >= 80)
                            scale = JPEG_SCALE_HALF;

                        int actualW = imgW >> scale;
                        int actualH = imgH >> scale;
                        int alignedW = (actualW + 15) & ~15;

                        uint8_t* ditherBuffer = (uint8_t*)ps_malloc(alignedW * 16);
                        if (!ditherBuffer) ditherBuffer = (uint8_t*)malloc(alignedW * 16);

                        if (ditherBuffer) {
                            ThumbDecodeContext ctx;
                            memset(ctx.thumb, 0, sizeof(ctx.thumb));
                            ctx.scaledW = actualW;
                            ctx.scaledH = actualH;

                            jpeg->setUserPointer(&ctx);
                            jpeg->setPixelType(ONE_BIT_DITHERED);

                            if (jpeg->decodeDither(ditherBuffer, scale)) {
                                File f = EbookFS.open(thumbPath, "w");
                                if (f) {
                                    f.write(ctx.thumb, 640);
                                    f.close();
                                    generated = true;
                                    Serial.printf("CoverExtractor: Generated thumb for %s\n", book.baseName.c_str());
                                }
                            }
                            free(ditherBuffer);
                        }
                        jpeg->close();
                    }
                    delete jpeg;
                    free(imgData);
                }
            }
            delete epub;
        }

        if (generated) {
            book.hasCoverThumb = true;
            return true;
        }

        return false;
    }
    return false;
}