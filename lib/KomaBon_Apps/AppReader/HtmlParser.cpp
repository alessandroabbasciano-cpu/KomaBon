#include "HtmlParser.h"
#include "FontMgr.h"

String HtmlParser::extractAttribute(const String& xml, const String& tag, const String& attr) {
    int attrStart = xml.indexOf(attr + "=\"");
    if (attrStart == -1) attrStart = xml.indexOf(attr + "='");
    if (attrStart == -1) return "";
    int valStart = attrStart + attr.length() + 2;
    char quote = xml.charAt(attrStart + attr.length() + 1);
    int valEnd = xml.indexOf(quote, valStart);
    if (valEnd == -1) return "";
    return xml.substring(valStart, valEnd);
}

TextStyle HtmlParser::getStyleFromTag(String tag) {
    tag.toLowerCase();
    if (tag == "b" || tag == "strong") return STYLE_BOLD;
    if (tag == "i" || tag == "em") return STYLE_ITALIC;
    if (tag == "h1") return STYLE_HEADER1;
    if (tag == "h2") return STYLE_HEADER2;
    if (tag == "h3") return STYLE_HEADER3;
    if (tag == "h4") return STYLE_HEADER4;
    return STYLE_NORMAL;
}

TextAlign HtmlParser::getAlignFromStyle(String styleAttr) {
    styleAttr.toLowerCase();
    if (styleAttr.indexOf("text-align:center") != -1 || styleAttr.indexOf("text-align: center") != -1)
        return ALIGN_CENTER;
    if (styleAttr.indexOf("text-align:right") != -1 || styleAttr.indexOf("text-align: right") != -1)
        return ALIGN_RIGHT;
    if (styleAttr.indexOf("text-align:justify") != -1 || styleAttr.indexOf("text-align: justify") != -1)
        return ALIGN_JUSTIFY;
    return ALIGN_LEFT;
}

Table HtmlParser::parseTable(const String& tableHtml) {
    Table table;
    int trPos = 0;
    while (true) {
        int trStart = tableHtml.indexOf("<tr", trPos);
        if (trStart == -1) break;
        int trEnd = tableHtml.indexOf("</tr>", trStart);
        if (trEnd == -1) break;
        String rowHtml = tableHtml.substring(trStart, trEnd + 5);
        TableRow row;
        int cellPos = 0;
        while (true) {
            int tdStart = rowHtml.indexOf("<td", cellPos);
            int thStart = rowHtml.indexOf("<th", cellPos);
            int cellStart = -1;
            bool isHeader = false;
            if (tdStart != -1 && (thStart == -1 || tdStart < thStart)) {
                cellStart = tdStart;
                isHeader = false;
            } else if (thStart != -1) {
                cellStart = thStart;
                isHeader = true;
            }
            if (cellStart == -1) break;
            String cellTag = isHeader ? "th" : "td";
            int cellTagEnd = rowHtml.indexOf(">", cellStart);
            int cellEnd = rowHtml.indexOf("</" + cellTag + ">", cellTagEnd);
            if (cellTagEnd == -1 || cellEnd == -1) break;
            TableCell cell;
            cell.isHeader = isHeader;
            String cellOpenTag = rowHtml.substring(cellStart, cellTagEnd + 1);
            String colspanStr = extractAttribute(cellOpenTag, cellTag, "colspan");
            String rowspanStr = extractAttribute(cellOpenTag, cellTag, "rowspan");
            if (colspanStr.length() > 0) cell.colspan = colspanStr.toInt();
            if (rowspanStr.length() > 0) cell.rowspan = rowspanStr.toInt();
            String cellContent = rowHtml.substring(cellTagEnd + 1, cellEnd);
            String clean;
            bool inTag = false;
            for (int i = 0; i < (int)cellContent.length(); i++) {
                char c = cellContent.charAt(i);
                if (c == '<')
                    inTag = true;
                else if (c == '>')
                    inTag = false;
                else if (!inTag)
                    clean += c;
            }
            clean.trim();
            cell.content = clean;
            row.cells.push_back(cell);
            cellPos = cellEnd + cellTag.length() + 3;
        }
        if (row.cells.size() > 0) {
            table.rows.push_back(row);
            if ((int)row.cells.size() > table.columnCount) table.columnCount = row.cells.size();
        }
        trPos = trEnd + 5;
    }
    return table;
}

int extractIndentFromStyle(String styleAttr) {
    styleAttr.toLowerCase();
    int indentPos = styleAttr.indexOf("text-indent:");
    if (indentPos == -1) indentPos = styleAttr.indexOf("text-indent :");
    if (indentPos != -1) {
        int valStart = styleAttr.indexOf(':', indentPos) + 1;
        int valEnd = styleAttr.indexOf(';', valStart);
        if (valEnd == -1) valEnd = styleAttr.length();
        String val = styleAttr.substring(valStart, valEnd);
        val.trim();
        if (val.endsWith("em")) return val.substring(0, val.length() - 2).toInt() * 20;
        if (val.endsWith("px")) return val.substring(0, val.length() - 2).toInt();
        return val.toInt();
    }
    return 0;
}

static void appendCodepointUtf8(String& out, uint32_t cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += '?';
    }
}

static void decodeHtmlEntities(String& text) {
    if (text.indexOf('&') == -1) return;

    struct Entity {
        const char* name;
        const char* value;
    };
    static const Entity entities[] = {
        {"amp", "&"},       {"lt", "<"},        {"gt", ">"},        {"quot", "\""},     {"apos", "'"},
        {"nbsp", " "},      {"shy", ""},        {"aacute", "\xE1"}, {"agrave", "\xE0"}, {"acirc", "\xE2"},
        {"atilde", "\xE3"}, {"auml", "\xE4"},   {"Aacute", "\xC1"}, {"Agrave", "\xC0"}, {"Acirc", "\xC2"},
        {"Atilde", "\xC3"}, {"Auml", "\xC4"},   {"ccedil", "\xE7"}, {"Ccedil", "\xC7"}, {"eacute", "\xE9"},
        {"egrave", "\xE8"}, {"ecirc", "\xEA"},  {"euml", "\xEB"},   {"Eacute", "\xC9"}, {"Egrave", "\xC8"},
        {"Ecirc", "\xCA"},  {"Euml", "\xCB"},   {"iacute", "\xED"}, {"igrave", "\xEC"}, {"icirc", "\xEE"},
        {"iuml", "\xEF"},   {"Iacute", "\xCD"}, {"Igrave", "\xCC"}, {"Icirc", "\xCE"},  {"Iuml", "\xCF"},
        {"oacute", "\xF3"}, {"ograve", "\xF2"}, {"ocirc", "\xF4"},  {"otilde", "\xF5"}, {"ouml", "\xF6"},
        {"Oacute", "\xD3"}, {"Ograve", "\xD2"}, {"Ocirc", "\xD4"},  {"Otilde", "\xD5"}, {"Ouml", "\xD6"},
        {"uacute", "\xFA"}, {"ugrave", "\xF9"}, {"ucirc", "\xFB"},  {"uuml", "\xFC"},   {"Uacute", "\xDA"},
        {"Ugrave", "\xD9"}, {"Ucirc", "\xDB"},  {"Uuml", "\xDC"},   {"ntilde", "\xF1"}, {"Ntilde", "\xD1"},
        {"laquo", "\xAB"},  {"raquo", "\xBB"},  {"ordf", "\xAA"},   {"ordm", "\xBA"},   {"deg", "\xB0"},
        {"ndash", "-"},     {"mdash", "-"},     {"hellip", "..."},  {"lsquo", "'"},     {"rsquo", "'"},
        {"ldquo", "\""},    {"rdquo", "\""},
    };

    String out;
    out.reserve(text.length());
    int i = 0;
    int len = text.length();
    while (i < len) {
        char c = text.charAt(i);
        if (c != '&') {
            out += c;
            i++;
            continue;
        }
        int semi = text.indexOf(';', i + 1);
        if (semi == -1 || semi - i > 10) {
            out += c;
            i++;
            continue;
        }
        String body = text.substring(i + 1, semi);
        bool handled = false;
        if (body.length() > 1 && body.charAt(0) == '#') {
            uint32_t cp = 0;
            if (body.charAt(1) == 'x' || body.charAt(1) == 'X') {
                cp = (uint32_t)strtoul(body.c_str() + 2, nullptr, 16);
            } else {
                cp = (uint32_t)strtoul(body.c_str() + 1, nullptr, 10);
            }
            if (cp > 0) {
                appendCodepointUtf8(out, cp);
                handled = true;
            }
        } else {
            for (const Entity& e : entities) {
                if (body.equals(e.name)) {
                    out += e.value;
                    handled = true;
                    break;
                }
            }
        }
        if (handled) {
            i = semi + 1;
        } else {
            out += c;
            i++;
        }
    }
    text = out;
}

std::vector<ContentNode> HtmlParser::parseHtmlToRichContent(const String& html, const String& chapterDir,
                                                            volatile bool* abortFlag) {
    std::vector<ContentNode> nodes;
    std::vector<TextStyle> styleStack;
    styleStack.push_back(STYLE_NORMAL);
    TextAlign currentAlign = ALIGN_LEFT;
    int currentIndent = 0;
    bool isListItem = false;
    bool nextIsBlockStart = true;
    String currentText;
    int i = 0;
    while (i < (int)html.length()) {
        if (abortFlag && *abortFlag) break; // Check for task abort request

        char c = html.charAt(i);
        if (c == '<') {
            if (currentText.length() > 0) {
                ContentNode node;
                node.type = CONTENT_TEXT;
                node.textNode.text = currentText;
                node.textNode.style = styleStack.back();
                node.textNode.align = currentAlign;
                node.textNode.isListItem = isListItem;
                node.textNode.indent = currentIndent;
                node.textNode.isBlockStart = nextIsBlockStart;
                nodes.push_back(node);
                currentText = "";
                isListItem = false;
                currentIndent = 0;
                nextIsBlockStart = false;
            }
            int tagEnd = html.indexOf('>', i);
            if (tagEnd == -1) break;
            String fullTag = html.substring(i, tagEnd + 1);
            String tag;
            int spacePos = fullTag.indexOf(' ');
            int closePos = fullTag.indexOf('>');
            if (spacePos != -1 && spacePos < closePos)
                tag = fullTag.substring(1, spacePos);
            else
                tag = fullTag.substring(1, closePos);
            tag.toLowerCase();
            bool isClosing = tag.startsWith("/");
            if (isClosing) tag = tag.substring(1);

            if (tag == "b" || tag == "strong" || tag == "i" || tag == "em") {
                if (!isClosing)
                    styleStack.push_back(getStyleFromTag(tag));
                else if (styleStack.size() > 1)
                    styleStack.pop_back();
            } else if (tag == "h1" || tag == "h2" || tag == "h3" || tag == "h4" || tag == "h5" ||
                       tag == "h6") {
                if (!isClosing) {
                    styleStack.push_back(getStyleFromTag(tag));
                    nextIsBlockStart = true;
                } else {
                    if (styleStack.size() > 1) styleStack.pop_back();
                    nextIsBlockStart = true;
                }
            } else if ((tag == "p" || tag == "div" || tag.startsWith("h")) && !isClosing) {
                nextIsBlockStart = true;
                String styleAttr = extractAttribute(fullTag, tag, "style");
                String classAttr = extractAttribute(fullTag, tag, "class");
                classAttr.toLowerCase();

                if (classAttr.indexOf("chapter-title") != -1 || classAttr.indexOf("chap-title") != -1 ||
                    classAttr.indexOf("section-title") != -1 || classAttr.indexOf("part-title") != -1) {
                    styleStack.push_back(STYLE_HEADER1);
                }

                if (styleAttr.length() > 0) {
                    currentAlign = getAlignFromStyle(styleAttr);
                    currentIndent = extractIndentFromStyle(styleAttr);
                }
                if (tag == "p" && currentIndent == 0 && styleStack.back() == STYLE_NORMAL) {
                    currentIndent = 30;
                }
            } else if (tag == "/p" || tag == "/div" || tag.startsWith("/h")) {
                nextIsBlockStart = true;
                if (styleStack.size() > 1 &&
                    (styleStack.back() == STYLE_HEADER1 || styleStack.back() == STYLE_HEADER2 ||
                     styleStack.back() == STYLE_HEADER3)) {
                    styleStack.pop_back();
                }
            } else if (tag == "li" && !isClosing) {
                isListItem = true;
                nextIsBlockStart = true;
            } else if (tag == "table" && !isClosing) {
                int tableEnd = html.indexOf("</table>", i);
                if (tableEnd != -1) {
                    String tableHtml = html.substring(i, tableEnd + 8);
                    Table table = parseTable(tableHtml);
                    if (table.rows.size() > 0) {
                        ContentNode node;
                        node.type = CONTENT_TABLE;
                        node.table = table;
                        nodes.push_back(node);
                    }
                    i = tableEnd + 8;
                    nextIsBlockStart = true;
                    continue;
                }
            } else if (tag == "script" || tag == "style" || tag == "head" || tag == "figure" ||
                       tag == "figcaption" || tag == "title" || tag == "desc") {
                int skipEnd = html.indexOf("</" + tag + ">", i);
                if (skipEnd != -1) {
                    i = skipEnd + tag.length() + 3;
                    continue;
                }
            } else if (tag == "img" || tag == "image") {
                String src = extractAttribute(fullTag, tag, "src");
                if (src.length() == 0) {
                    src = extractAttribute(fullTag, tag, "href");
                }
                if (src.length() == 0) {
                    src = extractAttribute(fullTag, tag, "xlink:href");
                }

                if (src.length() > 0) {
                    if (currentText.length() > 0) {
                        ContentNode textNodeObj;
                        textNodeObj.type = CONTENT_TEXT;
                        textNodeObj.textNode.text = currentText;
                        textNodeObj.textNode.style = styleStack.back();
                        textNodeObj.textNode.align = currentAlign;
                        textNodeObj.textNode.isListItem = isListItem;
                        textNodeObj.textNode.indent = currentIndent;
                        textNodeObj.textNode.isBlockStart = nextIsBlockStart;
                        nodes.push_back(textNodeObj);

                        currentText = "";
                        isListItem = false;
                        currentIndent = 0;
                    }

                    ContentNode imgNode;
                    imgNode.type = CONTENT_IMAGE;
                    imgNode.imageNode.imagePath = chapterDir + src;

                    String wAttr = extractAttribute(fullTag, tag, "width");
                    String hAttr = extractAttribute(fullTag, tag, "height");
                    imgNode.imageNode.width = (wAttr.length() > 0) ? wAttr.toInt() : 0;
                    imgNode.imageNode.height = (hAttr.length() > 0) ? hAttr.toInt() : 0;

                    nodes.push_back(imgNode);
                    nextIsBlockStart = true;
                }
            }

            else if (tag == "br") {
                currentText += "\n";
            }
            i = tagEnd + 1;
        } else {
            if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
                if (currentText.length() > 0 && currentText.charAt(currentText.length() - 1) != ' ' &&
                    currentText.charAt(currentText.length() - 1) != '\n') {
                    currentText += ' ';
                }
            } else {
                currentText += c;
            }
            i++;
        }
    }
    if (currentText.length() > 0) {
        ContentNode node;
        node.type = CONTENT_TEXT;
        node.textNode.text = currentText;
        node.textNode.style = styleStack.back();
        node.textNode.align = currentAlign;
        node.textNode.isListItem = isListItem;
        node.textNode.indent = currentIndent;
        node.textNode.isBlockStart = nextIsBlockStart;
        nodes.push_back(node);
    }
    for (auto& node : nodes) {
        if (node.type == CONTENT_TEXT) {
            decodeHtmlEntities(node.textNode.text);
            node.textNode.text.replace("¶Ç8", " -- ");
            node.textNode.text.replace("¶ÇÖ", "'");
            node.textNode.text.replace("¶Çö", "'");
            node.textNode.text.replace("¶Ç£", "\"");
            node.textNode.text.replace("¶Ç¥", "\"");
            node.textNode.text.replace("¶Ç", " ");
            node.textNode.text.replace("\xE2\x80\x9C", "\"");
            node.textNode.text.replace("\xE2\x80\x9D", "\"");
            node.textNode.text.replace("\xE2\x80\x98", "'");
            node.textNode.text.replace("\xE2\x80\x99", "'");
            node.textNode.text.replace("\xE2\x80\x94", " -- ");
            node.textNode.text.replace("\xE2\x80\x93", " - ");
            node.textNode.text.replace("\xE2\x80\xA6", "...");
            node.textNode.text.replace("\n,", ",");
            node.textNode.text.replace("\n.", ".");
            node.textNode.text.replace("\n!", "!");
            node.textNode.text.replace("\n?", "?");
            node.textNode.text = FontMgr::utf8ToLatin1(node.textNode.text);
            node.textNode.text.trim();

            if (node.textNode.text == "Unknown" || node.textNode.text == "image" ||
                node.textNode.text == "Image" || node.textNode.text == "[image]") {
                node.textNode.text = "";
            }

            if (node.textNode.isBlockStart && node.textNode.text.length() > 0 &&
                node.textNode.text.length() <= 3) {
                bool isNumeric = true;
                for (int i = 0; i < (int)node.textNode.text.length(); i++) {
                    if (!isdigit(node.textNode.text.charAt(i))) {
                        isNumeric = false;
                        break;
                    }
                }
                if (isNumeric) {
                    node.textNode.style = STYLE_HEADER1;
                }
            }
        }
    }

    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                               [](const ContentNode& n) {
                                   return n.type == CONTENT_TEXT && n.textNode.text.length() == 0;
                               }),
                nodes.end());
    return nodes;
}



