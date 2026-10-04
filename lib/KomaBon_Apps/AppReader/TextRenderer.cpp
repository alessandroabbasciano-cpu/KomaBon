#include "TextRenderer.h"
#include "WordFitLogic.h"

TextRenderer::TextRenderer(int width, int height, int fontSize, EpubLoader* epubLoader) {
    _width = width;
    _height = height;
    _epubLoader = epubLoader;

    if (fontSize >= 18)
        _fontSize = 18;
    else if (fontSize >= 16)
        _fontSize = 16;
    else if (fontSize >= 14)
        _fontSize = 14;
    else if (fontSize >= 12)
        _fontSize = 12;
    else if (fontSize >= 10)
        _fontSize = 10;
    else
        _fontSize = 8;
    _cachedPage = -1;
    _lastGFXFont = nullptr;
    memset(_gfxCharWidths, 0, sizeof(_gfxCharWidths));
    calculateDimensions();
}

void TextRenderer::setFontSize(int size) {
    int normalized =
        (size >= 18) ? 18 : (size >= 16 ? 16 : (size >= 14 ? 14 : (size >= 12 ? 12 : (size >= 10 ? 10 : 8))));
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
    TrueTypeEngine& ttEngine = TrueTypeEngine::getInstance();

    // Map ReaderFontFamily to Embedded TTF indices:
    // 0: Atkinson Hyperlegible
    // 1: Merriweather
    // 2: Literata
    // 3: Source Serif 4
    // 4: Gelasio
    // 5: Open Sans
    int ttfIndex = 0;
    switch (_fontFamily) {
        case READER_FONT_MERRIWEATHER:
            ttfIndex = 3;
            break; // Merriweather
        case READER_FONT_LITERATA:
            ttfIndex = 1;
            break; // Literata
        case READER_FONT_SOURCE_SERIF:
            ttfIndex = 4;
            break; // Source Serif 4
        case READER_FONT_GELASIO:
            ttfIndex = 5;
            break; // Gelasio
        case READER_FONT_OPEN_SANS:
            ttfIndex = 2;
            break; // Open Sans
        case READER_FONT_SANS:
        default:
            ttfIndex = 0;
            break; // Atkinson Hyperlegible
    }
    ttEngine.setFont(ttfIndex);

    float basePt = (float)_fontSize;
    float targetPt = basePt;
    bool bold = false;

    switch (style) {
        case STYLE_HEADER1:
            targetPt = basePt * 1.75f;
            if (targetPt < 18.0f) targetPt = 18.0f;
            bold = true;
            break;
        case STYLE_HEADER2:
            targetPt = basePt * 1.45f;
            if (targetPt < 15.0f) targetPt = 15.0f;
            bold = true;
            break;
        case STYLE_HEADER3:
            targetPt = basePt * 1.25f;
            if (targetPt < 13.0f) targetPt = 13.0f;
            bold = true;
            break;
        case STYLE_HEADER4:
            targetPt = basePt * 1.10f;
            bold = true;
            break;
        case STYLE_BOLD:
            targetPt = basePt;
            bold = true;
            break;
        default:
            targetPt = basePt;
            bold = false;
            break;
    }

    const GFXfont* font = ttEngine.getGFXFont(targetPt, bold);
    if (!font) {
        // Safe fallback to FreeSans9pt8b if TTF engine fails
        font = &FreeSans9pt8b;
    }

    // Airy proportional line-height (~1.35x)
    lineHeight = (font->yAdvance * 135) / 100;
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

    int defaultLineHeight = 0;
    getGFXFont(STYLE_NORMAL, defaultLineHeight);
    int y = 24 + (defaultLineHeight * 70 / 100);
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

            if (draw) {
                display.setFont(font);
            }

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

                // Allow watchdog to breathe during heavy text processing
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
                if (!node.imageNode.imagePath.endsWith(".raw")) {
                    if (draw) {
                        display.setCursor(currentX, y + 20);
                        display.print("[Legacy image omitted]");
                    }
                    y += 40;
                } else {
                    size_t rawSize = 0;
                    uint8_t* rawData = _epubLoader->getFileData(node.imageNode.imagePath, &rawSize);

                    if (rawData && rawSize >= 4) {
                        uint16_t imgW = rawData[0] | (rawData[1] << 8);
                        uint16_t imgH = rawData[2] | (rawData[3] << 8);

                        int availableH = maxY - y;

                        if (imgH > availableH && y > 50) {
                            free(rawData);
                            result.pageFull = true;
                            result.nextNodeIndex = currentNode;
                            result.nextCharOffset = 0;
                            _cachedResult = result;
                            _hasCachedResult = true;
                            return result;
                        }

                        if (draw) {
                            int drawX = (_width - imgW) / 2;
                            if (drawX < 0) drawX = 0;
                            // Offset by 4 to skip the Little Endian dimension header [W_lo, W_hi, H_lo, H_hi]
                            display.drawBitmap(drawX, y, rawData + 4, imgW, imgH, GxEPD_BLACK);
                        }

                        y += imgH + 20;
                        free(rawData);
                    } else {
                        if (draw) {
                            display.setCursor(currentX, y + 20);
                            display.print("[Image Missing]");
                        }
                        y += 40;
                        if (rawData) free(rawData);
                    }
                }
            } else {
                y += 40;
            }
            currentX = x_margin;
        }
        currentNode++;
        currentOffset = 0;
        result.nodesConsumed++;
    }

    return result;
}