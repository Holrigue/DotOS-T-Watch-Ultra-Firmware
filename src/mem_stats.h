#pragma once
// mem_stats.h - internal-RAM measurement for the "save RAM" work.
//
// The watch's scarce resource is internal SRAM (the 8 MB PSRAM holds the LVGL
// objects and rasters). This module reads the numbers that matter and prints them
// on the serial console, so a flasher log shows what a feature really costs:
//   free   - internal bytes free right now
//   min    - the lowest free ever reached since boot (the peak usage; it only goes down)
//   block  - the largest single free internal block (a big allocation needs this, not "free")
//   dma    - the largest free DMA-capable block (the display driver needs this)
//   psram  - PSRAM bytes free
//   wifi / bt - whether each radio is up, to read the numbers against
#include <stddef.h>
#include <stdint.h>

struct MemStats {
    uint32_t free_internal;
    uint32_t min_free_internal;
    uint32_t largest_internal;
    uint32_t largest_dma;
    uint32_t free_psram;
    bool     wifi_on;
    bool     bt_on;
};

// Read the live numbers.
MemStats mem_stats_read();

// Print one `[mem]` line with `tag` on the serial console.
void mem_stats_log(const char *tag);

// Call every loop(): prints a `[mem]` line ~5 s after boot, then every 30 s, and
// right away whenever a radio switches on or off (that is when the cost shows).
void mem_stats_tick();

// Format the Settings "Low" readout: lowest free ever + largest free block, in KB.
void mem_stats_format_low(char *out, size_t n);
