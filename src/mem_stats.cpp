// mem_stats.cpp - see mem_stats.h.
#include "mem_stats.h"

#include <Arduino.h>
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_bt.h"

MemStats mem_stats_read()
{
    MemStats m{};
    m.free_internal     = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    m.min_free_internal = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    m.largest_internal  = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    m.largest_dma       = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    m.free_psram        = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    wifi_mode_t wm = WIFI_MODE_NULL;
    m.wifi_on = (esp_wifi_get_mode(&wm) == ESP_OK) && wm != WIFI_MODE_NULL;
    m.bt_on   = esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED;
    return m;
}

void mem_stats_log(const char *tag)
{
    const MemStats m = mem_stats_read();
    Serial.printf("[mem] %s t=%lu free=%u min=%u block=%u dma=%u psram=%u wifi=%d bt=%d\n",
                  tag, (unsigned long)millis(),
                  (unsigned)m.free_internal, (unsigned)m.min_free_internal,
                  (unsigned)m.largest_internal, (unsigned)m.largest_dma,
                  (unsigned)m.free_psram, m.wifi_on ? 1 : 0, m.bt_on ? 1 : 0);
}

void mem_stats_tick()
{
    static uint32_t next_ms  = 5000;
    static int      last_wifi = -1, last_bt = -1;
    const uint32_t now = millis();

    // The radio state is cheap to read; a change is when the cost is worth printing.
    wifi_mode_t wm = WIFI_MODE_NULL;
    const int wifi = (esp_wifi_get_mode(&wm) == ESP_OK && wm != WIFI_MODE_NULL) ? 1 : 0;
    const int bt   = esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED ? 1 : 0;
    if (last_wifi != -1 && (wifi != last_wifi || bt != last_bt)) {
        // Radios take a moment to finish allocating; log now and again in 3 s.
        mem_stats_log(wifi != last_wifi ? (wifi ? "wifi-up" : "wifi-down")
                                        : (bt   ? "bt-up"   : "bt-down"));
        next_ms = now + 3000;
    }
    last_wifi = wifi;
    last_bt   = bt;

    if ((int32_t)(now - next_ms) < 0) return;
    mem_stats_log("periodic");
    next_ms = now + 30000;
}

void mem_stats_format_low(char *out, size_t n)
{
    const MemStats m = mem_stats_read();
    snprintf(out, n, "%u KB min, %u KB block",
             (unsigned)(m.min_free_internal / 1024), (unsigned)(m.largest_internal / 1024));
}
