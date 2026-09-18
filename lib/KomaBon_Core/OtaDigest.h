#pragma once
// KomaBon OS — SHA-256 & Ed25519 signature parsing for OTA verification.
//
// Pure string handling — no Arduino dependency, host-testable.

#include <cstddef>
#include <cstdint>
#include <cctype>

#define KOMABON_SHA256_HEX_LEN 64
#define KOMABON_ED25519_SIG_HEX_LEN 128
#define KOMABON_ED25519_SIG_LEN 64

namespace komabon_digest_detail {

inline bool isHexDigit(char c) {
    const unsigned char u = (unsigned char)c;
    return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F');
}

inline char lowerAscii(char c) {
    return (char)tolower((unsigned char)c);
}

inline uint8_t hexNibble(char c) {
    c = lowerAscii(c);
    return (c >= 'a') ? (uint8_t)(c - 'a' + 10) : (uint8_t)(c - '0');
}

template <typename S> bool matchesAt(const S& hay, size_t at, const char* needle) {
    const size_t n = hay.length();
    for (size_t i = 0; needle[i] != '\0'; i++) {
        if (at + i >= n) return false;
        if (lowerAscii(hay[at + i]) != lowerAscii(needle[i])) return false;
    }
    return true;
}

template <typename S> inline S sliceOf(const S& s, size_t from, size_t to) {
    return s.substring(from, to);
}

} // namespace komabon_digest_detail

#ifdef _GLIBCXX_STRING
namespace komabon_digest_detail {
inline std::string sliceOf(const std::string& s, size_t from, size_t to) {
    return s.substr(from, to - from);
}
} // namespace komabon_digest_detail
#endif

template <typename S>
bool extractHexField(const S& notes, const char* label, const char* assetName, size_t expectedHexLen,
                     S& out) {
    using namespace komabon_digest_detail;

    size_t labelLen = 0;
    while (label[labelLen] != '\0')
        labelLen++;

    const size_t n = notes.length();
    size_t i = 0;

    while (i < n) {
        size_t lineStart = i;
        while (lineStart < n && (notes[lineStart] == ' ' || notes[lineStart] == '\t')) {
            lineStart++;
        }

        if (matchesAt(notes, lineStart, label)) {
            size_t p = lineStart + labelLen;
            while (p < n && (notes[p] == ' ' || notes[p] == '\t'))
                p++;

            if (p < n && notes[p] == '(') {
                p++;
                size_t nameStart = p;
                while (p < n && notes[p] != ')' && notes[p] != '\n')
                    p++;

                if (p < n && notes[p] == ')') {
                    const size_t nameLen = p - nameStart;
                    bool nameOk = true;
                    size_t k = 0;
                    for (; assetName[k] != '\0'; k++) {
                        if (k >= nameLen || notes[nameStart + k] != assetName[k]) {
                            nameOk = false;
                            break;
                        }
                    }
                    if (nameOk && k != nameLen) nameOk = false;

                    if (nameOk) {
                        p++;
                        while (p < n && (notes[p] == ' ' || notes[p] == '\t'))
                            p++;
                        if (p < n && notes[p] == '=') {
                            p++;
                            while (p < n && (notes[p] == ' ' || notes[p] == '\t'))
                                p++;

                            size_t hexStart = p;
                            while (p < n && isHexDigit(notes[p]))
                                p++;

                            if (p - hexStart == expectedHexLen) {
                                out = sliceOf(notes, hexStart, p);
                                return true;
                            }
                            return false;
                        }
                    }
                }
            }
        }
        while (i < n && notes[i] != '\n')
            i++;
        if (i < n) i++;
    }
    return false;
}

template <typename S> bool extractSha256(const S& notes, const char* assetName, S& out) {
    return extractHexField(notes, "sha256", assetName, KOMABON_SHA256_HEX_LEN, out);
}

template <typename S> bool extractEd25519Signature(const S& notes, const char* assetName, S& out) {
    return extractHexField(notes, "ed25519", assetName, KOMABON_ED25519_SIG_HEX_LEN, out);
}

template <typename S> bool hexDecode(const S& hex, size_t hexLen, uint8_t* out) {
    using namespace komabon_digest_detail;
    if (hex.length() != hexLen) return false;
    for (size_t i = 0; i < hexLen; i++) {
        if (!isHexDigit(hex[i])) return false;
    }
    for (size_t i = 0; i < hexLen / 2; i++) {
        out[i] = (uint8_t)((hexNibble(hex[i * 2]) << 4) | hexNibble(hex[i * 2 + 1]));
    }
    return true;
}

template <typename S> bool sha256Equal(const S& a, const S& b) {
    using namespace komabon_digest_detail;
    if (a.length() != b.length()) return false;
    for (size_t i = 0; i < a.length(); i++) {
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    }
    return true;
}