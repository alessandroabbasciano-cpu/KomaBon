#include "TextRenderer.h"
#include "WordFitLogic.h"
#include <JPEGDEC.h>

static KomaBonDisplay* g_jpegDisplay = nullptr;
static int g_jpegX = 0;
static int g_jpegY = 0;

static int drawJpegCallback(JPEGDRAW* pDraw) {
    if (!g_jpegDisplay) return 0;

    // FIX: Properly calculate bytes per row for 1-bit packed image data.
    // Adds +7 before dividing to round up, preventing severe buffer underruns.
    int bytesPerRow = (pDraw->iWidth + 7) / 8;
    int bufferSize = bytesPerRow * pDraw->iHeight;

    uint8_t* invertedPixels = (uint8_t*)malloc(bufferSize);
    // Return 1 instead of 0 to allow decoder to safely continue even if memory fails
    if (!invertedPixels) return 1;

    uint8_t* src = (uint8_t*)pDraw->pPixels;
    for (int i = 0; i < bufferSize; i++) {
        invertedPixels[i] = ~src[i];
    }

    g_jpegDisplay->drawBitmap(g_jpegX + pDraw->x, g_jpegY + pDraw->y, invertedPixels, pDraw->iWidth,
                              pDraw->iHeight, GxEPD_BLACK);
    free(invertedPixels);
    return 1;
}

TextRenderer::TextRenderer(int width, int height, int fontSize, EpubLoader* epubLoader) {
    _width = width;
    _height = height;
    _epubLoader = epubLoader;

    if (fontSize >= 18)
        _fontSize = 18;
    else if (fontSize >= 12)
        _fontSize = 12;
    else
        _fontSize = 9;
    _cachedPage = -1;
    _lastGFXFont = nullptr;
    memset(_gfxCharWidths, 0, sizeof(_gfxCharWidths));
    calculateDimensions();
}

void TextRenderer::setFontSize(int size) {
    int normalized = (size >= 18) ? 18 : (size >= 12 ? 12 : 9);
    if (normalized == _fontSize) return;
    _fontSize = normalized;
    _lastGFXFont = nullptr;
    clearCache();
    calculateDimensions();
}

void TextRenderer::setFontFamily(int family) {
    int normalized =
        (family >= READER_FONT_SANS && family <= READER_FONT_OPEN_SANS) ? family : READER_FONT_SANS;
    if (normalized == _fontFamily) return;
    _fontFamily = normalized;
    _lastGFXFont = nullptr;
    clearCache();
    calculateDimensions();
}

void TextRenderer::calculateDimensions() {
    int lh = 0;
    getGFXFont(STYLE_NORMAL, lh);
    _lineHeight = lh > 0 ? lh : 24;
}

void TextRenderer::clearCache() {
    _lineCache.clear();
    _cachedPage = -1;
    _hasCachedResult = false;
}

const GFXfont* TextRenderer::getGFXFont(TextStyle style, int& lineHeight) {
    const GFXfont* normal;
    const GFXfont* bold;
    const GFXfont* h4;
    const GFXfont* h3;
    const GFXfont* h2;
    const GFXfont* h1;

#define B32_FONT_SET(NORMAL9, NORMAL12, NORMAL18, BOLD9, BOLD12, BOLD18, BOLD24)                             \
    switch (_fontSize) {                                                                                     \
        case 18:                                                                                             \
            normal = &NORMAL18;                                                                              \
            bold = &BOLD18;                                                                                  \
            h4 = &BOLD18;                                                                                    \
            h3 = &BOLD18;                                                                                    \
            h2 = &BOLD24;                                                                                    \
            h1 = &BOLD24;                                                                                    \
            break;                                                                                           \
        case 12:                                                                                             \
            normal = &NORMAL12;                                                                              \
            bold = &BOLD12;                                                                                  \
            h4 = &BOLD12;                                                                                    \
            h3 = &BOLD18;                                                                                    \
            h2 = &BOLD18;                                                                                    \
            h1 = &BOLD24;                                                                                    \
            break;                                                                                           \
        case 9:                                                                                              \
        default:                                                                                             \
            normal = &NORMAL9;                                                                               \
            bold = &BOLD9;                                                                                   \
            h4 = &BOLD9;                                                                                     \
            h3 = &BOLD12;                                                                                    \
            h2 = &BOLD18;                                                                                    \
            h1 = &BOLD24;                                                                                    \
            break;                                                                                           \
    }

    switch (_fontFamily) {
        case READER_FONT_MERRIWEATHER:
            B32_FONT_SET(Merriweather_Regular9pt8b, Merriweather_Regular12pt8b, Merriweather_Regular18pt8b,
                         Merriweather_Bold9pt8b, Merriweather_Bold12pt8b, Merriweather_Bold18pt8b,
                         Merriweather_Bold24pt8b)
            break;
        case READER_FONT_LITERATA:
            B32_FONT_SET(Literata_Regular9pt8b, Literata_Regular12pt8b, Literata_Regular18pt8b,
                         Literata_Bold9pt8b, Literata_Bold12pt8b, Literata_Bold18pt8b, Literata_Bold24pt8b)
            break;
        case READER_FONT_SOURCE_SERIF:
            B32_FONT_SET(SourceSerif4_Regular9pt8b, SourceSerif4_Regular12pt8b, SourceSerif4_Regular18pt8b,
                         SourceSerif4_Bold9pt8b, SourceSerif4_Bold12pt8b, SourceSerif4_Bold18pt8b,
                         SourceSerif4_Bold24pt8b)
            break;
        case READER_FONT_GELASIO:
            B32_FONT_SET(Gelasio_Regular9pt8b, Gelasio_Regular12pt8b, Gelasio_Regular18pt8b,
                         Gelasio_Bold9pt8b, Gelasio_Bold12pt8b, Gelasio_Bold18pt8b, Gelasio_Bold24pt8b)
            break;
        case READER_FONT_OPEN_SANS:
            B32_FONT_SET(OpenSans_Regular9pt8b, OpenSans_Regular12pt8b, OpenSans_Regular18pt8b,
                         OpenSans_Bold9pt8b, OpenSans_Bold12pt8b, OpenSans_Bold18pt8b, OpenSans_Bold24pt8b)
            break;
        case READER_FONT_SANS:
        default:
            B32_FONT_SET(FreeSans9pt8b, FreeSans12pt8b, FreeSans18pt8b, FreeSansBold9pt8b, FreeSansBold12pt8b,
                         FreeSansBold18pt8b, FreeSansBold24pt8b)
            break;
    }
#undef B32_FONT_SET

    const GFXfont* font;
    switch (style) {
        case STYLE_HEADER1:
            font = h1;
            break;
        case STYLE_HEADER2:
            font = h2;
            break;
        case STYLE_HEADER3:
            font = h3;
            break;
        case STYLE_HEADER4:
            font = h4;
            break;
        case STYLE_BOLD:
            font = bold;
            break;
        default:
            font = normal;
            break;
    }

    lineHeight = font->yAdvance + 2;
    return font;
}

RenderResult TextRenderer::renderRichPageDynamic(KomaBonDisplay& display,
                                                 const std::vector<ContentNode>& content, int startNode,
                                                 int startOffset, int pageNum, int pageNumForDisplay,
                                                 bool draw) {
    if (draw) {
        display.setTextColor(GxEPD_BLACK);
    }

    if (draw && _cachedPage == pageNum && !_lineCache.empty() && _hasCachedResult) {
        for (const auto& line : _lineCache) {
            int unused;
            display.setFont(getGFXFont((TextStyle)line.fontSize, unused));
            display.setCursor(line.x, line.y);
            display.print(line.text);
        }
        return _cachedResult;
    }

    _lineCache.clear();
    _cachedPage = pageNum;

    int y = 40;
    int maxY = _height - 40;
    RenderResult result = {0, 0, false, startNode, startOffset};
    int currentNode = startNode;
    int currentOffset = startOffset;

    char lineBuf[256];
    int line_width = 0;
    int x_margin = 35;
    int currentX = x_margin;

    while (currentNode < (int)content.size() && y < maxY) {
        auto& node = content[currentNode];
        if (node.type == CONTENT_TEXT) {
            int nodeLineHeight = 0;
            const GFXfont* font = getGFXFont(node.textNode.style, nodeLineHeight);
            display.setFont(font);

            if (font != _lastGFXFont) {
                for (int c = 32; c < 256; c++) {
                    if (c >= font->first && c <= font->last) {
                        _gfxCharWidths[c] = font->glyph[c - font->first].xAdvance;
                    } else {
                        _gfxCharWidths[c] = 0;
                    }
                }
                _lastGFXFont = font;
            }

            if (node.textNode.isBlockStart && currentOffset == 0) {
                if (line_width > 0) {
                    y += nodeLineHeight;
                    line_width = 0;
                }
                currentX = x_margin + node.textNode.indent;

                if (node.textNode.style == STYLE_HEADER1) {
                    y += 30;
                    currentX = 0;
                } else if (node.textNode.style == STYLE_HEADER2) {
                    y += 20;
                    currentX = 0;
                } else if (node.textNode.style == STYLE_HEADER3) {
                    y += 12;
                }
            }

            const char* text = node.textNode.text.c_str();
            int textLen = node.textNode.text.length();
            int pos = currentOffset;

            while (pos < textLen && y < maxY) {
                int line_chars = 0;
                lineBuf[0] = '\0';
                int segment_width = 0;

                if (node.textNode.isListItem && pos == currentOffset) {
                    strcpy(lineBuf, "- ");
                    segment_width = _gfxCharWidths['-'] + _gfxCharWidths[' '];
                }

                while (pos + line_chars < textLen) {
                    int wordStart = pos + line_chars;
                    while (wordStart < textLen && isspace((unsigned char)text[wordStart]))
                        wordStart++;
                    if (wordStart >= textLen) {
                        line_chars = textLen - pos;
                        break;
                    }

                    int wordEnd = wordStart;
                    while (wordEnd < textLen && !isspace((unsigned char)text[wordEnd]))
                        wordEnd++;

                    int wordWidth = 0;
                    for (int k = wordStart; k < wordEnd; k++) {
                        unsigned char c = (unsigned char)text[k];
                        wordWidth += _gfxCharWidths[c];
                    }

                    int spaceWidth = (line_width + segment_width > 0) ? _gfxCharWidths[' '] : 0;
                    int usableWidth = _width - x_margin;

                    if (currentX + line_width + segment_width + spaceWidth + wordWidth > usableWidth &&
                        (line_width + segment_width) > 0) {

                        if (y + nodeLineHeight > maxY) {
                            if (strlen(lineBuf) > 0) {
                                int drawX = currentX + line_width;
                                if (node.textNode.style == STYLE_HEADER1 ||
                                    node.textNode.style == STYLE_HEADER2) {
                                    drawX = (_width - segment_width) / 2;
                                }
                                _lineCache.push_back(
                                    {drawX, y, (int)node.textNode.style, false, String(lineBuf)});
                                if (draw) {
                                    display.setCursor(drawX, y);
                                    display.print(lineBuf);
                                }
                            }

                            int nextOffset = pos + line_chars;
                            result.pageFull = true;
                            result.charsConsumedInLastNode = nextOffset;
                            result.nextNodeIndex = currentNode;
                            result.nextCharOffset = nextOffset;
                            _cachedResult = result;
                            _hasCachedResult = true;
                            return result;
                        }

                        if (segment_width > 0) {
                            _lineCache.push_back(
                                {currentX + line_width, y, (int)node.textNode.style, false, String(lineBuf)});
                            if (draw) {
                                display.setCursor(currentX + line_width, y);
                                display.print(lineBuf);
                            }
                        }

                        y += nodeLineHeight;
                        line_width = 0;
                        currentX = x_margin;
                        segment_width = 0;
                        lineBuf[0] = '\0';

                        spaceWidth = 0;
                    }

                    int bufLeft = (int)sizeof(lineBuf) - 1 - (int)strlen(lineBuf);

                    if (segment_width > 0 || line_width > 0) {
                        if (bufLeft <= 0) break;
                        strcat(lineBuf, " ");
                        segment_width += spaceWidth;
                        bufLeft--;
                    }

                    int wordLen = wordEnd - wordStart;
                    int pixelBudget = usableWidth - (currentX + line_width + segment_width);
                    WordFit fit = fitWordIntoLine(text + wordStart, wordLen, wordWidth, bufLeft, pixelBudget,
                                                  _gfxCharWidths);
                    if (fit.take <= 0) break;

                    strncat(lineBuf, text + wordStart, fit.take);
                    segment_width += fit.width;
                    line_chars = wordStart + fit.take - pos;
                    if (fit.take < wordLen) break;
                }

                if (strlen(lineBuf) > 0) {
                    int drawX = currentX + line_width;
                    if (node.textNode.style == STYLE_HEADER1 || node.textNode.style == STYLE_HEADER2) {
                        drawX = (_width - segment_width) / 2;
                    }
                    _lineCache.push_back({drawX, y, (int)node.textNode.style, false, String(lineBuf)});
                    if (draw) {
                        display.setCursor(drawX, y);
                        display.print(lineBuf);
                    }
                    line_width += segment_width;
                }

                pos += line_chars;
                if (pos < textLen) {
                    y += nodeLineHeight;
                    line_width = 0;
                    currentX = x_margin;
                }
                yield();
            }

            if (pos < textLen && y >= maxY) {
                result.pageFull = true;
                result.charsConsumedInLastNode = pos;
                result.nextNodeIndex = currentNode;
                result.nextCharOffset = pos;
                _cachedResult = result;
                _hasCachedResult = true;
                return result;
            }

            if (node.textNode.isBlockStart && currentNode < (int)content.size() - 1 &&
                content[currentNode + 1].textNode.isBlockStart) {
                y += 8;
            }
            if (node.textNode.style == STYLE_HEADER1) {
                y += 25;
            } else if (node.textNode.style == STYLE_HEADER2) {
                y += 15;
            } else if (node.textNode.style == STYLE_HEADER3) {
                y += 10;
            }
        } else if (node.type == CONTENT_IMAGE) {
            if (_epubLoader) {
                size_t imgSize = 0;
                uint8_t* imgData = _epubLoader->getFileData(node.imageNode.imagePath, &imgSize);

                if (imgData) {
                    JPEGDEC* jpeg = new JPEGDEC();

                    if (jpeg->openRAM(imgData, imgSize, drawJpegCallback)) {
                        int imgW = jpeg->getWidth();
                        int imgH = jpeg->getHeight();

                        if (imgH > (_height * 0.3) && y > 50) {
                            jpeg->close();
                            delete jpeg;
                            free(imgData);

                            result.pageFull = true;
                            result.nextNodeIndex = currentNode;
                            result.nextCharOffset = 0;
                            _cachedResult = result;
                            _hasCachedResult = true;
                            return result;
                        }

                        int availableH = maxY - y;
                        jpeg->setPixelType(ONE_BIT_DITHERED);

                        int scale = 0;
                        if (imgW > _width || imgH > availableH) {
                            if (imgW / 2 <= _width && imgH / 2 <= availableH)
                                scale = JPEG_SCALE_HALF;
                            else if (imgW / 4 <= _width && imgH / 4 <= availableH)
                                scale = JPEG_SCALE_QUARTER;
                            else
                                scale = JPEG_SCALE_EIGHTH;
                        }

                        int actualW = imgW >> scale;
                        int actualH = imgH >> scale;

                        if (actualH > availableH && y > 50) {
                            jpeg->close();
                            delete jpeg;
                            free(imgData);

                            result.pageFull = true;
                            result.nextNodeIndex = currentNode;
                            result.nextCharOffset = 0;
                            _cachedResult = result;
                            _hasCachedResult = true;
                            return result;
                        }

                        int alignedW = (actualW + 15) & ~15;
                        int ditherBufferSize = alignedW * 16;

                        uint8_t* ditherBuffer = (uint8_t*)ps_malloc(ditherBufferSize);
                        if (!ditherBuffer) ditherBuffer = (uint8_t*)malloc(ditherBufferSize);

                        if (ditherBuffer) {
                            g_jpegDisplay = &display;
                            g_jpegX = (_width - actualW) / 2;
                            if (g_jpegX < 0) g_jpegX = 0;
                            g_jpegY = y;

                            if (draw) {
                                jpeg->decodeDither(ditherBuffer, scale);
                            }
                            free(ditherBuffer);
                        }

                        y += actualH + 20;
                        jpeg->close();
                    } else {
                        if (draw) {
                            display.setCursor(currentX, y + 20);
                            display.print("[Unsupported Image]");
                        }
                        y += 40;
                    }

                    delete jpeg;
                    free(imgData);
                } else {
                    y += 40;
                }
            }
            currentX = x_margin;
        }
        currentNode++;
        currentOffset = 0;
        result.nodesConsumed++;
    }

    return result;
}