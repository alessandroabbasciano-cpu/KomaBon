#pragma once
#include <LittleFS.h>
#include <SD.h>

// System Filesystem - uses the default LittleFS singleton
#define SystemFS LittleFS

// Ebook Filesystem - Dynamic abstraction pointer
extern fs::FS* EbookFSPtr;
#define EbookFS (*EbookFSPtr)
namespace KomaBonStorage {
bool mountEbooks();
size_t getUsedBytes();
size_t getTotalBytes();
} // namespace KomaBonStorage