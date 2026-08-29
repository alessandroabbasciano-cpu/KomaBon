#pragma once
#include <LittleFS.h>
#include <SD.h>

// System Filesystem - uses the default LittleFS singleton
#define SystemFS LittleFS

// Ebook Filesystem - Dynamic abstraction pointer
extern fs::FS* EbookFSPtr;
#define EbookFS (*EbookFSPtr)

// --- Helper Functions ---
bool EbookFS_begin();
size_t EbookFS_usedBytes();
size_t EbookFS_totalBytes();