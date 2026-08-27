// KomaBon — host test for upload admission control.
// Build: g++ -std=c++17 -I lib/KomaBon_Core tools/tests/test_upload_guard.cpp
#include <cassert>
#include <cstdio>
#include <string>
#include "UploadGuard.h"

int main() {
    using std::string;
    const size_t BIG = 10u * 1024u * 1024u;

    // Accepted extensions, including uppercase.
    assert(checkUpload(string("book.epub"), 1000, BIG) == UploadVerdict::Ok);
    assert(checkUpload(string("Book.EPUB"), 1000, BIG) == UploadVerdict::Ok);
    assert(checkUpload(string("font.TTF"), 1000, BIG) == UploadVerdict::Ok);

    // Rejected extensions.
    assert(checkUpload(string("book.pdf"), 1000, BIG) == UploadVerdict::BadExtension);
    assert(checkUpload(string("book"), 1000, BIG) == UploadVerdict::BadExtension);
    assert(checkUpload(string(""), 1000, BIG) == UploadVerdict::BadExtension);

    // Valid extension but unsafe name.
    assert(checkUpload(string("../a.epub"), 1000, BIG) == UploadVerdict::UnsafeName);
    assert(checkUpload(string("dir/a.epub"), 1000, BIG) == UploadVerdict::UnsafeName);
    assert(checkUpload(string("dir\\a.epub"), 1000, BIG) == UploadVerdict::UnsafeName);

    // Extension is checked before name: an unsafe name with an invalid
    // extension reports BadExtension.
    assert(checkUpload(string("../a.pdf"), 1000, BIG) == UploadVerdict::BadExtension);

    // Space: fits exactly with fixed slack plus scaling component.
    assert(checkUpload(string("a.epub"), 1000, 1000 + 1000 / 256 + KOMABON_UPLOAD_SLACK) == UploadVerdict::Ok);
    // Missing 1 byte.
    assert(checkUpload(string("a.epub"), 1000, 1000 + 1000 / 256 + KOMABON_UPLOAD_SLACK - 1) == UploadVerdict::NoSpace);
    // Partition full.
    assert(checkUpload(string("a.epub"), 1000, 0) == UploadVerdict::NoSpace);

    // Large file: this is where the scaled component matters. A 9 MB EPUB
    // needs ~36 KB beyond its size (9M/256 = 36864) — well above the fixed 8 KB
    // slack. With only fixed slack free, it would pass here and fail on
    // write(); now it is correctly NoSpace.
    const size_t NINE_MB = 9u * 1024u * 1024u;
    assert(checkUpload(string("a.epub"), NINE_MB, NINE_MB + KOMABON_UPLOAD_SLACK) == UploadVerdict::NoSpace);
    assert(checkUpload(string("a.epub"), NINE_MB, NINE_MB + NINE_MB / 256 + KOMABON_UPLOAD_SLACK) == UploadVerdict::Ok);

    // Unknown contentLength (0) is not a reason to reject: the handler
    // validates later, byte by byte, via the return value of write().
    assert(checkUpload(string("a.epub"), 0, BIG) == UploadVerdict::Ok);
    // ...but with a full partition it remains NoSpace.
    assert(checkUpload(string("a.epub"), 0, 0) == UploadVerdict::NoSpace);

    printf("All 18 tests passed.\n");
    return 0;
}