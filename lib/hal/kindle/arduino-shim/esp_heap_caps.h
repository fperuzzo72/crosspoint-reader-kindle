#pragma once

// ESP-IDF's capability-aware allocator.
//
// On the ESP32 the capability bits pick between internal SRAM, DMA-safe memory
// and PSRAM, which are genuinely different and genuinely scarce. Linux has one
// heap and 256 MB of it, so the bits are accepted and ignored.

#include <cstdlib>
#include <cstring>

#include <cstdint>

#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)

inline void* heap_caps_malloc(const size_t size, const uint32_t) { return malloc(size); }
inline void* heap_caps_calloc(const size_t n, const size_t size, const uint32_t) { return calloc(n, size); }
inline void* heap_caps_realloc(void* p, const size_t size, const uint32_t) { return realloc(p, size); }
inline void heap_caps_free(void* p) { free(p); }
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
