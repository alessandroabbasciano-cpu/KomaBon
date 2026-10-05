#pragma once
// KomaBon — admission control for book/font uploads.
//
// Rationale: /api/books/upload responded 200 even when the file wasn't opened
// or the partition was full, so the UI couldn't distinguish success from
// failure. The decision to accept an upload is pure logic and lives here,
// separated from the asynchronous handler, to be testable without hardware.
//
// Pure template — works with Arduino String and std::string.
// Host-testable: tools/tests/test_upload_guard.cpp.

#include <cstddef>
#include "FileExt.h"
#include "SafeName.h"

// Fixed slack for LittleFS metadata (directory entries, partially used blocks).
// An EPUB that fits "tightly" would still fail to write without this margin.
// In addition to this, there is a component that scales with size — see checkUpload.
#ifndef KOMABON_UPLOAD_SLACK
#define KOMABON_UPLOAD_SLACK 8192
#endif

enum class UploadVerdict { Ok, BadExtension, UnsafeName, NoSpace };

// `filename` must already be a basename (no directory components) — the
// handler extracts it before calling. `contentLength` is the size of the
// multipart body, always larger than the file, hence a conservative estimate.
// `contentLength == 0` means unknown: it doesn't prevent acceptance, but the
// space check still runs, so a partition with less than _UPLOAD_SLACK
// free returns NoSpace nonetheless.
//
// The required space scales with size: littlefs keeps CTZ skip-list pointers
// inside data blocks, so a file occupies about size * 4096/4088 in blocks —
// ~18 KB of overhead on a 9 MB file, well above the fixed 8 KB slack. Without
// this, an upload would pass here and fail later on write(), returning 500
// where 507 is the correct response.
// (size/256 bounds size/511 = size*4096/4088 - size; no risk of overflow
// for sizes around 10 MB with 32-bit size_t.)
template <typename S> UploadVerdict checkUpload(const S& filename, size_t contentLength, size_t freeBytes) {
    if (!hasExtensionCI(filename, ".epub") && !hasExtensionCI(filename, ".ttf")) {
        return UploadVerdict::BadExtension;
    }
    if (!isSafeBookName(filename)) {
        return UploadVerdict::UnsafeName;
    }
    if (freeBytes < contentLength + contentLength / 256 + KOMABON_UPLOAD_SLACK) {
        return UploadVerdict::NoSpace;
    }
    return UploadVerdict::Ok;
}