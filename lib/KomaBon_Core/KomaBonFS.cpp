#include "KomaBonFS.h"
#include <Arduino.h>
#include "SDMgr.h"

// The physical instance of the internal memory (Fallback)
fs::LittleFSFS InternalEbookFS;

// The global pointer that directs traffic.
fs::FS* EbookFSPtr = &InternalEbookFS;

namespace KomaBonStorage {

bool ensureReady() {
    if (EbookFSPtr == &SD) {
        return SDMgr::getInstance().ensureReady();
    }
    return true;
}

bool mountEbooks() {
    if (EbookFSPtr == &SD) {
        Serial.println("EbookFS: Using external MicroSD storage.");
        return true;
    }

    Serial.println("EbookFS: MicroSD absent. Starting internal partition...");
    bool ok = InternalEbookFS.begin(false, "/ebooks", 10, "ebooks");
    if (!ok) {
        Serial.println("EbookFS: Formatting internal partition...");
        ok = InternalEbookFS.begin(true, "/ebooks", 10, "ebooks");
    }
    return ok;
}

size_t getUsedBytes() {
    if (EbookFSPtr == &SD) return SD.usedBytes();
    return InternalEbookFS.usedBytes();
}

size_t getTotalBytes() {
    if (EbookFSPtr == &SD) return SD.totalBytes();
    return InternalEbookFS.totalBytes();
}

} // namespace KomaBonStorage