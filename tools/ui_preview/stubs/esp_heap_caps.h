#pragma once
#include <stdlib.h>
#define MALLOC_CAP_DMA 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
static inline void* heap_caps_malloc(size_t n, unsigned) { return malloc(n); }
static inline void heap_caps_free(void* p) { free(p); }
static inline size_t heap_caps_get_free_size(unsigned) { return 180000; }
static inline size_t heap_caps_get_minimum_free_size(unsigned) { return 160000; }
static inline size_t heap_caps_get_largest_free_block(unsigned) { return 100000; }
