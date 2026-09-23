// health_state.cpp - see health_state.h.
#include "health_state.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cstring>

using health::HealthData;

namespace {

HealthData s_model;

const char *const NS = "argushealth";

// ---- BLE -> loop mailbox ----------------------------------------------------
// One slot: the relay pushes health snapshots every few minutes, far slower than
// the 1 Hz drain, so a single-slot overwrite loses nothing meaningful. The
// critical section makes the copy + flag atomic against the loop's drain.
portMUX_TYPE      s_mail_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool     s_mail_ready = false;
uint8_t           s_mail_buf[health_packet::MAX_LEN];
volatile size_t   s_mail_len = 0;

// ---- NVS persistence throttle ----------------------------------------------
constexpr uint32_t SAVE_EVERY_MS = 300000;   // at most every 5 min
uint32_t s_last_save_ms = 0;
bool     s_dirty        = false;

// Bumped once per applied packet; the Health screen's refresh watches it.
uint32_t s_rx_seq = 0;

// Snapshot of what we last wrote, to skip no-op saves.
uint8_t  s_saved_sleep = 0, s_saved_stress = 0; uint16_t s_saved_hr = 0;
uint32_t s_saved_steps = 0;
bool     s_saved_valid = false;

uint16_t rd_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Apply one decoded packet to the model. Fields follow the mask in bit order.
void apply_packet(const uint8_t *d, size_t len, uint32_t now)
{
    if (len < 2 || d[0] != health_packet::VERSION) return;
    uint8_t mask = d[1];
    size_t  i    = 2;
    using namespace health_packet;

    if (mask & BIT_SLEEP)  { if (i + 1 > len) return; s_model.set_sleep_score(d[i], now); i += 1; }
    if (mask & BIT_STEPS)  { if (i + 4 > len) return; s_model.set_steps(rd_u32(d + i), now); i += 4; }
    if (mask & BIT_GOAL)   { if (i + 4 > len) return; /* goal is Settings-owned; skip */ i += 4; }
    if (mask & BIT_STRESS) { if (i + 1 > len) return; s_model.set_stress(d[i], now); i += 1; }
    if (mask & BIT_HR_AVG) { if (i + 2 > len) return; s_model.set_hr_avg(rd_u16(d + i), now); i += 2; }
    if (mask & BIT_HR_SAMPLE) { if (i + 2 > len) return; s_model.add_hr_sample(rd_u16(d + i), now); i += 2; }
}

void persist_now()
{
    Preferences p;
    if (!p.begin(NS, false)) return;
    HealthData::Snapshot s = s_model.snapshot(millis());
    p.putUInt("goal", s.step_goal);
    p.putUChar("slp_v", s.sleep_valid ? 1 : 0);
    p.putUChar("slp",   s.sleep_score);
    p.putUChar("stp_v", s.steps_valid ? 1 : 0);
    p.putUInt ("stp",   s.steps);
    p.putUChar("str_v", s.stress_valid ? 1 : 0);
    p.putUChar("str",   s.stress);
    p.putUChar("hr_v",  s.hr_valid ? 1 : 0);
    p.putUShort("hr",   s.hr_bpm);
    p.end();

    s_saved_sleep = s.sleep_score; s_saved_steps = s.steps;
    s_saved_stress = s.stress; s_saved_hr = s.hr_bpm; s_saved_valid = true;
    s_dirty = false;
}

}  // namespace

HealthData &health_model() { return s_model; }

void health_boot_restore()
{
    Preferences p;
    if (!p.begin(NS, true)) return;   // read-only; missing namespace -> defaults
    HealthData::Snapshot s{};
    s.step_goal    = p.getUInt("goal", 0);
    s.sleep_valid  = p.getUChar("slp_v", 0) != 0;
    s.sleep_score  = p.getUChar("slp", 0);
    s.steps_valid  = p.getUChar("stp_v", 0) != 0;
    s.steps        = p.getUInt("stp", 0);
    s.stress_valid = p.getUChar("str_v", 0) != 0;
    s.stress       = p.getUChar("str", 0);
    s.hr_valid     = p.getUShort("hr_v", 0) != 0;
    s.hr_bpm       = p.getUShort("hr", 0);
    p.end();

    // Restore each metric already past its stale window: the last number is
    // shown but grayed until the relay refreshes it. Steps keep their value so
    // the progress bar reflects the last known count immediately.
    s.sleep_age_ms  = health::SLEEP_STALE_MS  + 1;
    s.steps_age_ms  = health::STEPS_STALE_MS  + 1;
    s.stress_age_ms = health::STRESS_STALE_MS + 1;
    s.hr_age_ms     = health::HR_STALE_MS     + 1;

    uint32_t now = millis();
    // now may be < age just after boot; restore() uses unsigned subtraction, so
    // the reconstructed timestamp is simply "long ago", which reads as stale.
    s_model.restore(s, now);

    // Default the daily goal so the Dot progress bar is meaningful out of the
    // box; the user can change it in Settings. Persisted so it survives reboots.
    if (s_model.step_goal() == 0) health_set_step_goal(10000);
}

void health_tick_1hz()
{
    uint32_t now = millis();

    // Drain the BLE mailbox (copy out under the lock, then apply on the loop).
    if (s_mail_ready) {
        uint8_t  buf[health_packet::MAX_LEN];
        size_t   n;
        portENTER_CRITICAL(&s_mail_mux);
        n = s_mail_len;
        if (n > sizeof(buf)) n = sizeof(buf);
        memcpy(buf, s_mail_buf, n);
        s_mail_ready = false;
        portEXIT_CRITICAL(&s_mail_mux);
        apply_packet(buf, n, now);
        s_dirty = true;
        s_rx_seq++;   // a fresh push landed; the Health refresh watches this
    }

    s_model.tick(now);   // 2-minute HR compile

    // Persist at most every SAVE_EVERY_MS, and only when something changed.
    if (s_dirty && (now - s_last_save_ms >= SAVE_EVERY_MS || s_last_save_ms == 0)) {
        HealthData::Snapshot s = s_model.snapshot(now);
        bool changed = !s_saved_valid || s.sleep_score != s_saved_sleep ||
                       s.steps != s_saved_steps || s.stress != s_saved_stress ||
                       s.hr_bpm != s_saved_hr;
        if (changed) persist_now();
        else         s_dirty = false;
        s_last_save_ms = now;
    }
}

void health_set_step_goal(uint32_t goal)
{
    s_model.set_step_goal(goal);
    Preferences p;
    if (p.begin(NS, false)) { p.putUInt("goal", goal); p.end(); }
}

uint32_t health_get_step_goal() { return s_model.step_goal(); }

uint32_t health_rx_seq() { return s_rx_seq; }

bool health_data_fresh()
{
    uint32_t now = millis();
    return (s_model.has_hr()     && !s_model.hr_stale(now))
        || (s_model.has_steps()  && !s_model.steps_stale(now))
        || (s_model.has_stress() && !s_model.stress_stale(now));
}

void health_ingest_packet(const uint8_t *data, size_t len)
{
    if (!data || len == 0) return;
    if (len > health_packet::MAX_LEN) len = health_packet::MAX_LEN;
    portENTER_CRITICAL(&s_mail_mux);
    memcpy(s_mail_buf, data, len);
    s_mail_len   = len;
    s_mail_ready = true;
    portEXIT_CRITICAL(&s_mail_mux);
}
