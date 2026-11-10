#pragma once
// Host shim for the ESP-IDF heap introspection the Settings "System Info" block
// reads. SIM ONLY. Fixed, obviously-synthetic values: the sim has no ESP heap.
#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM   2
#define MALLOC_CAP_8BIT     4
#define MALLOC_CAP_DEFAULT  8
static inline size_t heap_caps_get_free_size(uint32_t)          { return 64 * 1024; }
static inline size_t heap_caps_get_total_size(uint32_t)         { return 320 * 1024; }
static inline size_t heap_caps_get_largest_free_block(uint32_t) { return 32 * 1024; }
static inline size_t heap_caps_get_minimum_free_size(uint32_t)  { return 48 * 1024; }
