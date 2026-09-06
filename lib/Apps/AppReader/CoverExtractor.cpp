#include "CoverExtractor.h"
#include "AppReader.h"
#include "KBReader.h"
#include "EpubLoader.h"
#include "KomaBonFS.h"
#include <JPEGDEC.h>

// Internal state structure passed to the JPEGDEC callback
struct ThumbState {
    uint8_t thumb[640];
    int srcW;
    int srcH;
};

// Callback to process JPEG MCU blocks on the fly:
// downsamples pixels via nearest-neighbor, thresholds to 1-bit black/white,
// and packs the result into a 60x80 byte buffer (8 bytes per row * 80 rows).
static int thumbDrawCallback(JPEGDRAW* pDraw) {
    ThumbState* state = (ThumbState*)pDraw->pUser;
    uint8_t* pixels = (uint8_t*)pDraw->pPixels; // Decoded in EIGHT_BIT_GRAYSCALE mode

    for (int y = 0; y < pDraw->iHeight; y++) {
        int sy = pDraw->y + y;
        if (sy >= state->srcH) continue;
        int ty = sy * 80 / state->srcH;

        for (int x = 0; x < pDraw->iWidth; x++) {
            int sx = pDraw->x + x;
            if (sx >= state->srcW) continue;
            int tx = sx * 60 / state->srcW;

            uint8_t luma = pixels[y * pDraw->iWidth + x];
            if (luma < 128) { // 1-bit threshold: black pixel = bit 1
                int dstByte = ty * 8 + (tx / 8);
                int dstBit = 7 - (tx % 8);
                state->thumb[dstByte] |= (1 << dstBit);
            }
        }
    }
    return 1; // Continue decoding next MCU block
}

bool CoverExtractor::processNextCover(std::vector<BookEntry>& books) {
    for (auto& book : books) {
        if (book.hasCoverThumb || book.coverAttempted) continue;

        book.coverAttempted = true; // Mark attempted to avoid repetitive processing loops
        String thumbPath = "/covers/" + book.baseName + ".thumb";
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

                    // Nearest-neighbor bit-to-bit reduction
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

                if (imgData) {
                    JPEGDEC* jpeg = new JPEGDEC();

                    if (jpeg->openRAM(imgData, imgSize, thumbDrawCallback)) {
                        int imgW = jpeg->getWidth();
                        int imgH = jpeg->getHeight();

                        // Pre-scale during IDCT decoding to reduce CPU load and PSRAM usage
                        int scale = 0;
                        if (imgW / 8 >= 60 && imgH / 8 >= 80)
                            scale = JPEG_SCALE_EIGHTH;
                        else if (imgW / 4 >= 60 && imgH / 4 >= 80)
                            scale = JPEG_SCALE_QUARTER;
                        else if (imgW / 2 >= 60 && imgH / 2 >= 80)
                            scale = JPEG_SCALE_HALF;

                        ThumbState* state = new ThumbState();
                        memset(state->thumb, 0, 640);
                        state->srcW = imgW >> scale;
                        state->srcH = imgH >> scale;

                        jpeg->setUserPointer(state);
                        jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);

                        if (jpeg->decode(0, 0, scale)) {
                            File f = EbookFS.open(thumbPath, "w");
                            if (f) {
                                f.write(state->thumb, 640);
                                f.close();
                                generated = true;
                            }
                        }
                        delete state;
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