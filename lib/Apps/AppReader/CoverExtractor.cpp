#include "CoverExtractor.h"
#include "AppReader.h"
#include "KBReader.h"
#include "EpubLoader.h"
#include "KomaBonFS.h"
#include <JPEGDEC.h>

struct ThumbState {
    uint8_t thumb[640];
    int srcW;
    int srcH;
};

static int thumbDrawCallback(JPEGDRAW* pDraw) {
    ThumbState* state = (ThumbState*)pDraw->pUser;
    uint8_t* pixels = (uint8_t*)pDraw->pPixels;

    for (int ty = 0; ty < 80; ty++) {
        int sy = ty * state->srcH / 80;

        if (sy >= pDraw->y && sy < pDraw->y + pDraw->iHeight) {
            int mcuY = sy - pDraw->y;
            for (int tx = 0; tx < 60; tx++) {
                int sx = tx * state->srcW / 60;

                if (sx >= pDraw->x && sx < pDraw->x + pDraw->iWidth) {
                    int mcuX = sx - pDraw->x;
                    uint8_t luma = pixels[mcuY * pDraw->iWidth + mcuX];

                    if (luma < 128) {
                        int dstByte = ty * 8 + (tx / 8);
                        int dstBit = 7 - (tx % 8);
                        state->thumb[dstByte] |= (1 << dstBit);
                    }
                }
            }
        }
    }
    return 1;
}

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

                    if (jpeg->openRAM(imgData, imgSize, thumbDrawCallback)) {
                        int imgW = jpeg->getWidth();
                        int imgH = jpeg->getHeight();

                        int scale = 0;
                        if (imgW / 8 >= 60 && imgH / 8 >= 80)
                            scale = JPEG_SCALE_EIGHTH;
                        else if (imgW / 4 >= 60 && imgH / 4 >= 80)
                            scale = JPEG_SCALE_QUARTER;
                        else if (imgW / 2 >= 60 && imgH / 2 >= 80)
                            scale = JPEG_SCALE_HALF;

                        ThumbState state;
                        memset(state.thumb, 0, sizeof(state.thumb));
                        state.srcW = imgW >> scale;
                        state.srcH = imgH >> scale;

                        jpeg->setUserPointer(&state);
                        jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);

                        if (jpeg->decode(0, 0, scale)) {
                            File f = EbookFS.open(thumbPath, "w");
                            if (f) {
                                f.write(state.thumb, 640);
                                f.close();
                                generated = true;
                            }
                        }
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