#pragma once
#include <pgmspace.h>
#include <stddef.h>
#include <stdint.h>

struct EmbeddedTTF {
    const char* name;
    const uint8_t* data;
    size_t size;
};

extern const EmbeddedTTF EMBEDDED_TTFS[];
extern const size_t EMBEDDED_TTFS_COUNT;
