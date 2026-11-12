#pragma once
// drain_stats.h - how fast the battery is actually draining.
//
// The PMU on this watch (AXP2101) reports a charge PERCENTAGE but has no current
// sensor, so "how many mA does the watch draw" cannot be read directly. What can
// be measured is the slope: percent lost per hour over the current discharge run.
// That is the number that decides whether a power optimisation was worth it, so
// it is shown in Settings > System Info.
//
// A "run" starts at the first sample taken on battery and ends when a charger is
// seen; the next sample on battery starts a new one. The run also records how
// much of it was spent with the panel off, because the two states drain very
// differently and a rate means little without that split.
//
// Header-only and free of hardware so it is covered by the host tests
// (test/test_drain_stats.cpp). The caller supplies time, percent and state.
#include <cstdint>

namespace drain {

class Tracker {
public:
    // Call every few tens of seconds. now_s: seconds since boot. pct: 0..100.
    // on_battery: no charger attached. screen_off: panel is off right now.
    void sample(uint32_t now_s, int pct, bool on_battery, bool screen_off)
    {
        if (!on_battery || pct < 0 || pct > 100) { active_ = false; return; }
        if (!active_) {
            active_ = true;
            start_s_ = last_s_ = now_s;
            start_pct_ = pct_ = pct;
            off_s_ = 0;
            return;
        }
        uint32_t dt = now_s - last_s_;
        // A long gap is not evidence about the screen state in between (the loop
        // was stalled or the sampler was not called): count only short steps.
        if (screen_off && dt <= kMaxStepS) off_s_ += dt;
        last_s_ = now_s;
        pct_ = pct;
    }

    bool     active()    const { return active_; }
    uint32_t elapsed_s() const { return active_ ? last_s_ - start_s_ : 0; }

    // Percent per hour, times 10 (42 = 4.2 %/h). -1 until the run is long enough
    // to mean something: at least kMinRunS on battery AND a whole percent lost
    // (the gauge only resolves 1%, so anything less is noise).
    int rate_x10() const
    {
        if (!active_) return -1;
        uint32_t el = elapsed_s();
        int drop = start_pct_ - pct_;
        if (el < kMinRunS || drop < 1) return -1;
        return (int)(((uint64_t)drop * 36000u) / el);
    }

    // Whole hours left at the measured rate, -1 while the rate is unknown.
    int hours_left() const
    {
        int r = rate_x10();
        if (r <= 0) return -1;
        return (pct_ * 10) / r;
    }

    // Share of the run spent with the panel off, 0..100. -1 before any time passed.
    int screen_off_pct() const
    {
        uint32_t el = elapsed_s();
        if (!active_ || el == 0) return -1;
        return (int)(((uint64_t)off_s_ * 100u) / el);
    }

    static constexpr uint32_t kMinRunS  = 600;   // 10 min before a rate is shown
    static constexpr uint32_t kMaxStepS = 120;   // longest step counted as "off"

private:
    bool     active_ = false;
    uint32_t start_s_ = 0, last_s_ = 0, off_s_ = 0;
    int      start_pct_ = 0, pct_ = 0;
};

// The one tracker the firmware feeds (main loop) and reads (Settings).
inline Tracker &tracker() { static Tracker t; return t; }

}  // namespace drain
