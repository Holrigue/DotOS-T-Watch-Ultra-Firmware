#pragma once
#include <stdint.h>
#include <stdbool.h>

// Nearby-human presence detector: passive BLE scan for phone/wearable-style
// advertisements (Apple Continuity, Google Fast Pair, Microsoft Swift Pair),
// filtered to a strong RSSI so ambient/far traffic doesn't count. Answers "is
// someone with a phone near me right now" - not "is a specific device
// following me" (that's what AirTag/Flipper/Flock/Threat Radar already do),
// so this deliberately does NOT feed threatradar_observe().
//
// Same _start/_stop/_is_running/_check/_bg_tick shape as airtag.h, except
// _get_count's meaning: here it's a LIVE count of devices inside the rolling
// "nearby" window (30s), so it reads as "how many are near me right now",
// not a running tally since the detector was turned on.

bool human_detector_start();
void human_detector_stop();
bool human_detector_is_running();
int  human_detector_get_count();     // devices currently "nearby" (rolling window)
void human_detector_reset_count();

// One nearby device, for the radar screen: its last RSSI and a stable per-MAC
// angle (0..359, hashed from the MAC) so its blip keeps the same bearing frame
// to frame instead of jumping around.
struct HumanBlip {
    int8_t   rssi;
    uint16_t angle_deg;
};

// Snapshot the devices currently inside the rolling window into out[0..max),
// newest RSSI each. Returns how many were written (<= max, <= nearby count).
// Safe to call from the UI thread; it just reads the detector's table.
int  human_detector_snapshot(HumanBlip *out, int max);

// Inspect one BLE advertisement for a phone/wearable-style beacon: dedups,
// updates the rolling nearby table, and queues an SD log entry on a fresh
// sighting. Returns true on a match. Lets the wardriver feed detections in
// without the standalone scanner running (same convention as airtag_check).
bool human_detector_check(const uint8_t *mac6, int8_t rssi, uint8_t addr_type,
                           const uint8_t *adv, int adv_len);

// Prunes stale entries from the rolling nearby table and drains queued
// detections to the SD card. Call from loop().
void human_detector_bg_tick();
