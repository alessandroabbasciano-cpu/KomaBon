#ifndef COVER_EXTRACTOR_H
#define COVER_EXTRACTOR_H

#include <Arduino.h>
#include <vector>

// Forward declaration of BookEntry to avoid circular dependencies
struct BookEntry;

class CoverExtractor {
  public:
    // Scans the book list and processes the first book missing a cached thumbnail.
    // Returns true if a thumbnail was generated, signaling a display refresh is required.
    static bool processNextCover(std::vector<BookEntry>& books);
};

#endif // COVER_EXTRACTOR_H