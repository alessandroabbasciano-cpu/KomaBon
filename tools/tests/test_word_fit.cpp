// Host test for the line-buffer clamp used by the reader's word wrap.
// Build: g++ -std=c++17 -I ../../lib/KomaBon_Core -o test_word_fit test_word_fit.cpp && ./test_word_fit
#include "WordFitLogic.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using std::string;

// Test widths: 10 px per printable character, 0 for anything out of range
// (what TextRenderer does with glyphs the font lacks).
static unsigned char widths[256];

static int widthOf(const string& s) {
    int w = 0;
    for (unsigned char c : s) w += widths[c];
    return w;
}

int main() {
    memset(widths, 0, sizeof(widths));
    for (int c = 0x20; c < 0x100; c++) widths[c] = 10;
    widths[(unsigned char)'\x01'] = 0;  // missing glyph

    // 1. Word that fits: copied entirely, width preserved.
    {
        string w = "livro";
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), widthOf(w), 100, 200, widths);
        assert(f.take == 5 && f.width == 50);
    }
    // 2. Word larger than buffer: truncated at buffer limit, never above.
    //    (This is the case that corrupted the stack: 300 characters in a 256 buffer.)
    {
        string w(300, 'a');
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), widthOf(w), 255, 100000, widths);
        assert(f.take == 255);
        assert(f.take <= 255);
    }
    // 3. Word wider than line: split at pixel limit.
    {
        string w = "abcdefghij";               // 100 px
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), widthOf(w), 100, 45, widths);
        assert(f.take == 4 && f.width == 40);  // the 5th character would exceed 45
    }
    // 4. Tight buffer and pixels simultaneously: the most restrictive wins.
    {
        string w(50, 'a');
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), widthOf(w), 3, 1000, widths);
        assert(f.take == 3);
    }
    // 5. No buffer space: nothing is copied (caller closes the line).
    {
        string w = "abc";
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), widthOf(w), 0, 1000, widths);
        assert(f.take == 0);
    }
    // 6. Negative budget (line past margin): still advance one character,
    //    otherwise the line composition loop never terminates.
    {
        string w = "abc";
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), widthOf(w), 10, -5, widths);
        assert(f.take == 1 && f.width == 10);
    }
    // 7. Zero-width glyphs: also progress, and all fit.
    {
        string w(20, '\x01');
        WordFit f = fitWordIntoLine(w.c_str(), w.size(), 0, 10, 0, widths);
        assert(f.take == 10 && f.width == 0);
    }
    // 8. Degenerate inputs.
    {
        assert(fitWordIntoLine(nullptr, 5, 0, 10, 10, widths).take == 0);
        assert(fitWordIntoLine("abc", 0, 0, 10, 10, widths).take == 0);
    }
    // 9. General invariant: for any combination, take never exceeds buffer
    //    space or word length, and progresses whenever there is space.
    {
        for (int len = 1; len <= 40; len++) {
            string w(len, 'a');
            for (int buf = 0; buf <= 40; buf += 7) {
                for (int budget = -20; budget <= 200; budget += 13) {
                    WordFit f = fitWordIntoLine(w.c_str(), len, widthOf(w), buf, budget, widths);
                    assert(f.take <= buf);
                    assert(f.take <= len);
                    assert(f.take >= 0);
                    if (buf >= 1) assert(f.take >= 1);
                }
            }
        }
    }
    printf("test_word_fit: all tests passed.\n");
    return 0;
}