// human_detector.cpp - see human_detector.h.
//
// Same shape as airtag.cpp (passive BLE scan through the shared
// ble_scan_manager, AD-record walk, dedup table, queue -> SD log via
// _bg_tick), but a different question: not "is this specific device
// following me across distance and time" (AirTag/Flipper/Flock/Threat
// Radar), but "is someone with a phone near me right now". So instead of
// matching one vendor's specific beacon type, this matches the three
// manufacturer/service signatures that phones and wearables broadcast
// constantly (Apple Continuity, Google Fast Pair, Microsoft Swift Pair),
// gated to a strong RSSI so far-away/ambient traffic doesn't count as
// "nearby". It's a proximity heuristic, not device fingerprinting - a v1
// rough edge worth knowing: the RSSI threshold below is a rough guess at
// "within a couple of metres" for typical BLE TX power, not calibrated
// against this exact hardware's antenna.
#include "human_detector.h"
#include "ble_scan_manager.h"
#include "detect_log_sd.h"
#include "gps_screen.h"
#include "geo_cell.h"   // geo::kGpsLogDecimals - SD log GPS precision

#include <LilyGoLib.h>
#include <SD.h>
#include <string.h>
#include "esp_gap_ble_api.h"
#include "freertos/queue.h"

void clock_screen_get_local_time(struct tm *out);   // defined in main.cpp

namespace {

// Rough "within a couple of metres" cutoff for typical phone/wearable BLE TX
// power. Not calibrated against this watch's antenna - a real-hardware pass
// may need to tune this.
constexpr int8_t kRssiNearThreshold = -70;

struct HumanHit {
    uint8_t mac[6];
    int8_t  rssi;
    uint8_t addr_type;
};

#define HUMAN_SEEN_SIZE 24
struct SeenEntry {
    uint8_t  mac[6];
    uint32_t last_seen_ms;     // freshens the rolling "nearby now" window
    uint32_t last_logged_ms;   // suppresses re-logging the same MAC too often
};

volatile bool s_running = false;
QueueHandle_t s_queue   = nullptr;

SeenEntry s_seen[HUMAN_SEEN_SIZE];
int       s_seen_count = 0;

constexpr uint32_t kNearbyWindowMs = 30000;    // "currently nearby" horizon
constexpr uint32_t kRelogMs        = 300000;   // re-log the same MAC at most every 5 min

}  // namespace

bool human_detector_check(const uint8_t *mac6, int8_t rssi, uint8_t addr_type,
                           const uint8_t *adv, int adv_len)
{
    if (rssi < kRssiNearThreshold) return false;

    bool phone_like = false;
    for (int pos = 0; pos < adv_len; ) {
        uint8_t seg_len = adv[pos];
        if (seg_len == 0) break;
        if (pos + 1 + (int)seg_len > adv_len) break;
        uint8_t        ad_type     = adv[pos + 1];
        const uint8_t *ad_data     = adv + pos + 2;
        int            ad_data_len = (int)seg_len - 1;

        if (ad_type == 0xFF && ad_data_len >= 2) {
            uint16_t company = (uint16_t)ad_data[0] | ((uint16_t)ad_data[1] << 8);
            // Apple Continuity (Nearby/Handoff/AirPods/...) - anything except
            // sub-type 0x12 (Find My / offline finding), which airtag.cpp
            // already owns; counting it here too would double-detect AirTags
            // as "a human", which isn't what this screen is for.
            if (company == 0x004C && !(ad_data_len >= 3 && ad_data[2] == 0x12))
                phone_like = true;
            // Microsoft Swift Pair.
            if (company == 0x0006)
                phone_like = true;
        }
        if ((ad_type == 0x02 || ad_type == 0x03) && ad_data_len >= 2) {
            // Google Fast Pair 16-bit service UUID.
            uint16_t uuid16 = (uint16_t)ad_data[0] | ((uint16_t)ad_data[1] << 8);
            if (uuid16 == 0xFE2C) phone_like = true;
        }
        pos += 1 + (int)seg_len;
    }
    if (!phone_like) return false;

    uint32_t now = millis();
    int idx = -1;
    for (int i = 0; i < s_seen_count; i++) {
        if (memcmp(s_seen[i].mac, mac6, 6) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        if (s_seen_count < HUMAN_SEEN_SIZE) {
            idx = s_seen_count++;
        } else {
            idx = 0;
            for (int i = 1; i < s_seen_count; i++)
                if (s_seen[i].last_seen_ms < s_seen[idx].last_seen_ms) idx = i;
        }
        memcpy(s_seen[idx].mac, mac6, 6);
        s_seen[idx].last_logged_ms = 0;
    }
    s_seen[idx].last_seen_ms = now;

    if (now - s_seen[idx].last_logged_ms >= kRelogMs) {
        s_seen[idx].last_logged_ms = now;
        if (!s_queue) s_queue = xQueueCreate(8, sizeof(HumanHit));
        HumanHit hit = {};
        memcpy(hit.mac, mac6, 6);
        hit.rssi = rssi; hit.addr_type = addr_type;
        if (s_queue) xQueueSend(s_queue, &hit, 0);
    }
    return true;
}

namespace {

void on_scan_result(esp_ble_gap_cb_param_t *param)
{
    if (!s_running) return;
    auto &res = param->scan_rst;
    int total = (int)res.adv_data_len + (int)res.scan_rsp_len;
    human_detector_check(res.bda, (int8_t)res.rssi, res.ble_addr_type, res.ble_adv, total);
}

}  // namespace

bool human_detector_start()
{
    if (s_running) return true;
    if (!s_queue) {
        s_queue = xQueueCreate(8, sizeof(HumanHit));
        if (!s_queue) return false;
    }
    if (!ble_scan_add(on_scan_result)) return false;
    s_running    = true;
    s_seen_count = 0;
    return true;
}

void human_detector_stop()
{
    if (!s_running) return;
    s_running = false;
    ble_scan_remove(on_scan_result);
}

bool human_detector_is_running() { return s_running; }

int human_detector_get_count()
{
    // Live count within the rolling window - prune first so a phone that
    // walked away doesn't linger in the count until the next bg_tick().
    uint32_t now = millis();
    int count = 0;
    for (int i = 0; i < s_seen_count; i++)
        if (now - s_seen[i].last_seen_ms < kNearbyWindowMs) count++;
    return count;
}

void human_detector_reset_count() { s_seen_count = 0; }

void human_detector_bg_tick()
{
    uint32_t now = millis();
    int w = 0;
    for (int i = 0; i < s_seen_count; i++) {
        if (now - s_seen[i].last_seen_ms < kNearbyWindowMs) {
            if (w != i) s_seen[w] = s_seen[i];
            w++;
        }
    }
    s_seen_count = w;

    if (!s_queue) return;
    HumanHit hit;
    if (xQueueReceive(s_queue, &hit, 0) != pdTRUE) return;
    if (!instance.isCardReady()) return;
    if (!SD.exists("/HumanDetector")) SD.mkdir("/HumanDetector");
    File f = SD.open("/HumanDetector/discovered.txt", FILE_APPEND);
    if (!f) return;
    struct tm t;
    clock_screen_get_local_time(&t);
    f.printf("%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    f.printf("\tMAC %02X:%02X:%02X:%02X:%02X:%02X (%s)",
        hit.mac[0], hit.mac[1], hit.mac[2], hit.mac[3], hit.mac[4], hit.mac[5],
        hit.addr_type == BLE_ADDR_TYPE_RANDOM ? "RAND" : "PUB");
    f.printf("\tRSSI %d", hit.rssi);
    if (gps_screen_has_lock() && instance.gps.location.isValid()) {
        f.printf("\tGPS %.*f,%.*f", geo::kGpsLogDecimals, instance.gps.location.lat(), geo::kGpsLogDecimals, instance.gps.location.lng());
        if (instance.gps.altitude.isValid()) f.printf("\tAlt %.1fm", instance.gps.altitude.meters());
    }
    f.print("\n");
    f.close();
    detect_log_enforce("/HumanDetector/discovered.txt");
}
