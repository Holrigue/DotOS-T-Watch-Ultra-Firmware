#pragma once
//
// health_data.h - pure model for the wearer's health metrics mirrored from an
// external tracker (the user's Amazfit Helio, synced by Gadgetbridge on the
// phone). This is the DATA layer only: no Arduino, no BLE, no NVS, so it builds
// and is unit-tested on the host exactly like device_mode_plan / threat_state.
//
// WHY A PURE CORE. Phase 2.2 has one genuinely open design point - HOW the
// Amazfit's numbers reach the watch (Gadgetbridge does not forward one device's
// health data to another, so a phone-side relay has to push it; see
// docs/health/README.md). That transport is undecided, but the RECEIVING model
// is the same whichever wins: hold the four metrics, compile a heart-rate
// average on a fixed cadence, know when a value has gone stale. So the core is
// written and proven first, and the transport + the NVS cache + the UI just
// feed and read it later.
//
// Time is injected as a millisecond clock on every call (never read here) so the
// 2-minute heart-rate window and the staleness checks are deterministic under
// test. millis() overflow is a non-issue: all comparisons use unsigned
// subtraction (now - then), which wraps correctly.
#include <cstdint>

namespace health {

// Cadence for compiling raw heart-rate samples into the displayed average. The
// spec is "average beats-per-minute compiled every 2 minutes": samples land
// continuously, and every 2 minutes their mean becomes the shown BPM and the
// window resets.
static constexpr uint32_t HR_COMPILE_MS = 120000;   // 2 min

// A metric is "stale" (UI should gray it) once it is older than its cadence by a
// comfortable margin - long enough not to flicker between normal updates, short
// enough to notice the phone relay has stopped. Sleep is a once-a-day score, so
// its window spans a full day plus slack.
static constexpr uint32_t HR_STALE_MS     = 360000;    // 6 min  (>2 windows)
static constexpr uint32_t STRESS_STALE_MS = 900000;    // 15 min
static constexpr uint32_t STEPS_STALE_MS  = 1800000;   // 30 min
static constexpr uint32_t SLEEP_STALE_MS  = 93600000;  // 26 h

class HealthData {
public:
    // ---- Ingest: called by whatever transport feeds the watch ---------------
    // Scores are 0..100; out-of-range values are clamped so a malformed relay
    // packet can never push the UI past its bar.
    void set_sleep_score(uint8_t score, uint32_t now_ms);
    void set_steps(uint32_t steps, uint32_t now_ms);
    void set_stress(uint8_t stress, uint32_t now_ms);

    // The daily step goal. Configuration, not a measurement, so it carries no
    // timestamp and never goes stale; 0 means "no goal set" (progress reads 0).
    void set_step_goal(uint32_t goal);

    // Heart rate. Two feeds are supported so the relay can use whichever it has:
    //  - add_hr_sample(): raw instantaneous readings; the mean is published on
    //    the next tick() at or past the 2-minute boundary.
    //  - set_hr_avg(): a value the phone side already averaged; published at once.
    void add_hr_sample(uint16_t bpm, uint32_t now_ms);
    void set_hr_avg(uint16_t bpm, uint32_t now_ms);

    // Drive the 2-minute HR compile. Call at ~1 Hz. Cheap and idempotent between
    // boundaries; publishes the window mean and resets the accumulator when the
    // window closes with at least one sample.
    void tick(uint32_t now_ms);

    // ---- Read: UI ------------------------------------------------------------
    bool     has_sleep_score() const { return sleep_valid_; }
    uint8_t  sleep_score()     const { return sleep_score_; }

    bool     has_steps()       const { return steps_valid_; }
    uint32_t steps()           const { return steps_; }
    uint32_t step_goal()       const { return step_goal_; }
    // 0..100, clamped, for a progress bar. 0 when no goal is set.
    uint8_t  step_progress_pct() const;
    bool     step_goal_reached() const;

    bool     has_stress()      const { return stress_valid_; }
    uint8_t  stress()          const { return stress_; }

    bool     has_hr()          const { return hr_valid_; }
    uint16_t hr()              const { return hr_bpm_; }

    // Staleness: true when the value exists but is older than its window.
    bool sleep_stale(uint32_t now_ms)  const;
    bool steps_stale(uint32_t now_ms)  const;
    bool stress_stale(uint32_t now_ms) const;
    bool hr_stale(uint32_t now_ms)     const;

    // ---- Persistence snapshot ------------------------------------------------
    // The device side caches this in NVS so a reboot shows the last known values
    // (grayed as stale until the relay refreshes them) instead of blanks. Only
    // published/settled values are carried - never the in-flight HR accumulator.
    // Ages are "milliseconds before the snapshot instant", so restore() can
    // rebuild each last-update time against the new boot clock.
    struct Snapshot {
        bool     sleep_valid;
        uint8_t  sleep_score;
        uint32_t sleep_age_ms;
        bool     steps_valid;
        uint32_t steps;
        uint32_t steps_age_ms;
        uint32_t step_goal;
        bool     stress_valid;
        uint8_t  stress;
        uint32_t stress_age_ms;
        bool     hr_valid;
        uint16_t hr_bpm;
        uint32_t hr_age_ms;
    };
    Snapshot snapshot(uint32_t now_ms) const;
    void restore(const Snapshot &s, uint32_t now_ms);

private:
    // Settled values + when each was last updated (for staleness).
    bool     sleep_valid_ = false;
    uint8_t  sleep_score_ = 0;
    uint32_t sleep_ms_    = 0;

    bool     steps_valid_ = false;
    uint32_t steps_       = 0;
    uint32_t steps_ms_    = 0;
    uint32_t step_goal_   = 0;

    bool     stress_valid_ = false;
    uint8_t  stress_       = 0;
    uint32_t stress_ms_    = 0;

    bool     hr_valid_ = false;
    uint16_t hr_bpm_   = 0;
    uint32_t hr_ms_    = 0;

    // Heart-rate compile window (raw-sample feed only).
    uint32_t hr_sum_          = 0;
    uint32_t hr_count_        = 0;
    uint32_t hr_window_start_ = 0;
    bool     hr_window_open_  = false;
};

}  // namespace health
