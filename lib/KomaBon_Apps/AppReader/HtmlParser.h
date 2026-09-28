#pragma once

#include <Arduino.h>
#include <vector>

// Text formatting enums
enum TextStyle {
    STYLE_NORMAL,
    STYLE_BOLD,
    STYLE_ITALIC,
    STYLE_BOLD_ITALIC,
    STYLE_HEADER1,
    STYLE_HEADER2,
    STYLE_HEADER3,
    STYLE_HEADER4
};

enum TextAlign { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT, ALIGN_JUSTIFY };

// Rich text node for formatted content
struct RichTextNode {
    String text;
    TextStyle style;
    TextAlign align;
    bool isListItem;
    bool isBlockStart; // Starts a new paragraph/block
    int indent;

    RichTextNode()
        : style(STYLE_NORMAL), align(ALIGN_LEFT), isListItem(false), isBlockStart(true), indent(0) {}
};

// Table structures
struct TableCell {
    String content;
    int colspan;
    int rowspan;
    bool isHeader;

    TableCell() : colspan(1), rowspan(1), isHeader(false) {}
};

struct TableRow {
    std::vector<TableCell> cells;
};

struct Table {
    std::vector<TableRow> rows;
    int columnCount;

    Table() : columnCount(0) {}
};

// Content node - can be text, table, or an image
enum ContentType { CONTENT_TEXT, CONTENT_TABLE, CONTENT_IMAGE };
struct ImageNode {
    String imagePath;
    int width;
    int height;
};

struct ContentNode {
    ContentType type;
    RichTextNode textNode;
    Table table;
    ImageNode imageNode;

    ContentNode() : type(CONTENT_TEXT) {}
};

class HtmlParser {
public:
    static std::vector<ContentNode> parseHtmlToRichContent(const String& html, const String& chapterDir = "", volatile bool* abortFlag = nullptr);

private:
    static String extractAttribute(const String& tagHtml, const String& tagName, const String& attrName);
    static String htmlUnescape(const String& encoded);
    static Table parseTable(const String& tableHtml);
    static TextStyle getStyleFromTag(String tag);
    static TextAlign getAlignFromStyle(String styleAttr);
};
