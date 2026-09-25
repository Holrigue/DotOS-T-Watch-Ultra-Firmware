// health_data.cpp - see health_data.h. Pure logic, host-tested.
#include "health_data.h"

namespace health {

namespace {
inline uint8_t clamp100(uint8_t v) { return v > 100 ? 100 : v; }
// Unsigned age that is correct across a millis() wrap.
inline uint32_t age(uint32_t now, uint32_t then) { return now - then; }
}  // namespace

void HealthData::set_sleep_score(uint8_t score, uint32_t now_ms)
{
    sleep_score_ = clamp100(score);
    sleep_ms_    = now_ms;
    sleep_valid_ = true;
}

void HealthData::set_steps(uint32_t steps, uint32_t now_ms)
{
    steps_       = steps;
    steps_ms_    = now_ms;
    steps_valid_ = true;
}

void HealthData::set_stress(uint8_t stress, uint32_t now_ms)
{
    stress_       = clamp100(stress);
    stress_ms_    = now_ms;
    stress_valid_ = true;
}

void HealthData::set_step_goal(uint32_t goal)
{
    step_goal_ = goal;
}

void HealthData::add_hr_sample(uint16_t bpm, uint32_t now_ms)
{
    if (bpm == 0) return;   // dropped/again-later reading; not a real beat rate
    if (!hr_window_open_) {
        hr_window_open_  = true;
        hr_window_start_ = now_ms;
        hr_sum_          = 0;
        hr_count_        = 0;
    }
    hr_sum_   += bpm;
    hr_count_ += 1;
}

void HealthData::set_hr_avg(uint16_t bpm, uint32_t now_ms)
{
    if (bpm == 0) return;
    hr_bpm_   = bpm;
    hr_ms_    = now_ms;
    hr_valid_ = true;
    // A pre-averaged feed supersedes any raw window in progress.
    hr_window_open_ = false;
    hr_sum_         = 0;
    hr_count_       = 0;
}

void HealthData::set_hr_range(uint16_t lo, uint16_t hi, uint32_t now_ms)
{
    if (lo == 0 && hi == 0) return;
    if (lo > hi) { uint16_t t = lo; lo = hi; hi = t; }   // tolerate a swapped pair
    hr_low_          = lo;
    hr_high_         = hi;
    hr_range_ms_     = now_ms;
    hr_range_valid_  = true;
}

void HealthData::tick(uint32_t now_ms)
{
    if (hr_window_open_ && age(now_ms, hr_window_start_) >= HR_COMPILE_MS) {
        if (hr_count_ > 0) {
            hr_bpm_   = (uint16_t)((hr_sum_ + hr_count_ / 2) / hr_count_);  // rounded mean
            hr_ms_    = now_ms;
            hr_valid_ = true;
        }
        hr_window_open_ = false;
        hr_sum_         = 0;
        hr_count_       = 0;
    }
}

uint8_t HealthData::step_progress_pct() const
{
    if (step_goal_ == 0) return 0;
    uint64_t pct = (uint64_t)steps_ * 100 / step_goal_;   // 64-bit: no overflow
    return pct > 100 ? 100 : (uint8_t)pct;
}

bool HealthData::step_goal_reached() const
{
    return step_goal_ > 0 && steps_ >= step_goal_;
}

bool HealthData::sleep_stale(uint32_t now_ms) const
{
    return sleep_valid_ && age(now_ms, sleep_ms_) >= SLEEP_STALE_MS;
}
bool HealthData::steps_stale(uint32_t now_ms) const
{
    return steps_valid_ && age(now_ms, steps_ms_) >= STEPS_STALE_MS;
}
bool HealthData::stress_stale(uint32_t now_ms) const
{
    return stress_valid_ && age(now_ms, stress_ms_) >= STRESS_STALE_MS;
}
bool HealthData::hr_stale(uint32_t now_ms) const
{
    return hr_valid_ && age(now_ms, hr_ms_) >= HR_STALE_MS;
}
bool HealthData::hr_range_stale(uint32_t now_ms) const
{
    return hr_range_valid_ && age(now_ms, hr_range_ms_) >= HR_RANGE_STALE_MS;
}

HealthData::Snapshot HealthData::snapshot(uint32_t now_ms) const
{
    Snapshot s{};
    s.sleep_valid  = sleep_valid_;
    s.sleep_score  = sleep_score_;
    s.sleep_age_ms = sleep_valid_ ? age(now_ms, sleep_ms_) : 0;
    s.steps_valid  = steps_valid_;
    s.steps        = steps_;
    s.steps_age_ms = steps_valid_ ? age(now_ms, steps_ms_) : 0;
    s.step_goal    = step_goal_;
    s.stress_valid  = stress_valid_;
    s.stress        = stress_;
    s.stress_age_ms = stress_valid_ ? age(now_ms, stress_ms_) : 0;
    s.hr_valid  = hr_valid_;
    s.hr_bpm    = hr_bpm_;
    s.hr_age_ms = hr_valid_ ? age(now_ms, hr_ms_) : 0;
    return s;
}

void HealthData::restore(const Snapshot &s, uint32_t now_ms)
{
    // Rebuild each last-update instant from its stored age against the new boot
    // clock. A value whose age already exceeds its stale window is kept (so the
    // UI shows the last number, grayed) but never rings in as fresh.
    sleep_valid_ = s.sleep_valid;
    sleep_score_ = clamp100(s.sleep_score);
    sleep_ms_    = now_ms - s.sleep_age_ms;

    steps_valid_ = s.steps_valid;
    steps_       = s.steps;
    steps_ms_    = now_ms - s.steps_age_ms;
    step_goal_   = s.step_goal;

    stress_valid_ = s.stress_valid;
    stress_       = clamp100(s.stress);
    stress_ms_    = now_ms - s.stress_age_ms;

    hr_valid_ = s.hr_valid;
    hr_bpm_   = s.hr_bpm;
    hr_ms_    = now_ms - s.hr_age_ms;

    // The raw-sample window never persists.
    hr_window_open_ = false;
    hr_sum_         = 0;
    hr_count_       = 0;
}

}  // namespace health
