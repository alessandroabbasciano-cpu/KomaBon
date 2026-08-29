#include "KomaBonFS.h"

// The physical instance of the internal memory (Fallback)
fs::LittleFSFS InternalEbookFS;

// The global pointer that directs traffic.
// Defaults to internal memory. SDMgr will redirect it to SD 
// if the hardware mount succeeds at boot.
fs::FS* EbookFSPtr = &InternalEbookFS;

bool EbookFS_begin() {
    // If EbookFSPtr points to SD, SDMgr has already mounted it at boot.
    // We don't need to do anything, the "/ebooks" mount point is already acquired.
    if (EbookFSPtr == &SD) {
        Serial.println("EbookFS: Using external MicroSD storage.");
        return true;
    }

    // Otherwise, initialize the internal partition (Fallback)
    Serial.println("EbookFS: MicroSD absent. Starting internal partition...");
    bool ok = InternalEbookFS.begin(false, "/ebooks", 10, "ebooks");
    if (!ok) {
        Serial.println("EbookFS: Formatting internal partition...");
        ok = InternalEbookFS.begin(true, "/ebooks", 10, "ebooks");
    }
    return ok;
}

size_t EbookFS_usedBytes() {
    if (EbookFSPtr == &SD) return SD.usedBytes();
    return InternalEbookFS.usedBytes();
}

size_t EbookFS_totalBytes() {
    if (EbookFSPtr == &SD) return SD.totalBytes();
    return InternalEbookFS.totalBytes();
}