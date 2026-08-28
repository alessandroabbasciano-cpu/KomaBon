#pragma once
// KomaBon — how many characters of a word fit into the line under construction.
//
// Rationale: TextRenderer assembled each line into a `char lineBuf[256]` and
// appended the word with strncat() without checking the remaining space.
// Line wrapping only triggers when the line already has content, so the
// first word of a line was always copied in full: a token with more than
// 255 characters (a URL, or text the HTML parser failed to split) wrote past
// the end of the buffer, onto the stack.
//
// The decision is pure arithmetic, so it lives here, separated from drawing,
// and is testable without hardware. Host-testable: tools/tests/test_word_fit.cpp.

#include <cstddef>

struct WordFit {
    int take;  // characters to copy (0 = nothing fits, close the line)
    int width; // width in pixels of those characters
};

// `widths` is the byte-indexed (0-255) width table of the active font.
// `bufLeft` is the free space in the line buffer, already discounting the
// terminator and an optional separator. `pixelBudget` is what remains of the
// useful line width and can be negative.
//
// Guarantees:
//   - take <= bufLeft and take <= wordLen (never overflows the buffer);
//   - take >= 1 whenever bufLeft >= 1 and wordLen >= 1, even with negative
//     budget or zero-width glyphs (characters outside the font's range),
//     preventing the call loop from stalling.
inline WordFit fitWordIntoLine(const char* word, int wordLen, int wordWidth, int bufLeft, int pixelBudget,
                               const unsigned char* widths) {
    if (!word || wordLen <= 0 || bufLeft <= 0) return {0, 0};

    // Normal case: the word fits entirely, with no extra measurement cost.
    if (wordLen <= bufLeft && wordWidth <= pixelBudget) return {wordLen, wordWidth};

    // Word doesn't fit on a line by itself: break by characters.
    int take = 0;
    int fitted = 0;
    while (take < wordLen && take < bufLeft) {
        int charWidth = (int)widths[(unsigned char)word[take]];
        if (take > 0 && fitted + charWidth > pixelBudget) break;
        fitted += charWidth;
        take++;
    }

    if (take <= 0) {
        // Only happens when not even the first character fits in the budget;
        // advancing one character guarantees progress.
        take = 1;
        fitted = (int)widths[(unsigned char)word[0]];
    }
    return {take, fitted};
}