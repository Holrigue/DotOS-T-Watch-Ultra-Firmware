// test_health_data.cpp - host unit tests for the pure health-metrics model
// (src/health_data). No hardware, no BLE: time is a plain millisecond input on
// every call, so the 2-minute heart-rate compile window and the per-metric
// staleness are exercised deterministically. Mirrors the threat_state test style.
#include "wl_test.h"
#include "health_data.h"

#include <cstdint>

using health::HealthData;
using health::HR_COMPILE_MS;

// ---- Scores clamp and read back --------------------------------------------
WL_TEST(health_scores_set_and_clamp) {
  HealthData h;
  WL_CHECK(!h.has_sleep_score());
  WL_CHECK(!h.has_stress());

  h.set_sleep_score(83, 1000);
  h.set_stress(42, 1000);
  WL_CHECK(h.has_sleep_score());
  WL_CHECK_EQ(h.sleep_score(), (uint8_t)83);
  WL_CHECK(h.has_stress());
  WL_CHECK_EQ(h.stress(), (uint8_t)42);

  // Out-of-range is clamped to 100 so the UI bar can never overrun.
  h.set_sleep_score(200, 2000);
  h.set_stress(150, 2000);
  WL_CHECK_EQ(h.sleep_score(), (uint8_t)100);
  WL_CHECK_EQ(h.stress(), (uint8_t)100);
}

// ---- Steps + goal progress --------------------------------------------------
WL_TEST(health_step_progress) {
  HealthData h;
  // No goal set: progress is 0 and never "reached", even with steps logged.
  h.set_steps(5000, 1000);
  WL_CHECK_EQ(h.step_progress_pct(), (uint8_t)0);
  WL_CHECK(!h.step_goal_reached());

  h.set_step_goal(10000);
  WL_CHECK_EQ(h.step_progress_pct(), (uint8_t)50);
  WL_CHECK(!h.step_goal_reached());

  h.set_steps(10000, 2000);
  WL_CHECK_EQ(h.step_progress_pct(), (uint8_t)100);
  WL_CHECK(h.step_goal_reached());

  // Over goal: bar clamps to 100 but the raw count is preserved.
  h.set_steps(13500, 3000);
  WL_CHECK_EQ(h.step_progress_pct(), (uint8_t)100);
  WL_CHECK(h.step_goal_reached());
  WL_CHECK_EQ(h.steps(), (uint32_t)13500);
}

// ---- Heart rate: raw samples compile only on the 2-minute boundary ---------
WL_TEST(health_hr_raw_window) {
  HealthData h;
  WL_CHECK(!h.has_hr());

  // Three samples inside the window: mean = (60+80+70)/3 = 70.
  h.add_hr_sample(60, 0);
  h.add_hr_sample(80, 30000);
  h.add_hr_sample(70, 60000);

  // Ticks before the boundary must NOT publish anything.
  h.tick(90000);
  WL_CHECK(!h.has_hr());

  // Tick at the boundary compiles and publishes the rounded mean.
  h.tick(HR_COMPILE_MS);
  WL_CHECK(h.has_hr());
  WL_CHECK_EQ(h.hr(), (uint16_t)70);
}

// Rounded mean: (70+71) / 2 = 70.5 -> 71.
WL_TEST(health_hr_rounds_mean) {
  HealthData h;
  h.add_hr_sample(70, 0);
  h.add_hr_sample(71, 1000);
  h.tick(HR_COMPILE_MS);
  WL_CHECK_EQ(h.hr(), (uint16_t)71);
}

// A window that closes with zero samples publishes nothing and never latches a
// stale value; a zero-BPM reading is dropped, not averaged in.
WL_TEST(health_hr_ignores_zero_and_empty) {
  HealthData h;
  h.add_hr_sample(0, 0);          // dropped: does not even open a window
  h.tick(HR_COMPILE_MS);
  WL_CHECK(!h.has_hr());

  h.add_hr_sample(100, 1000);
  h.add_hr_sample(0, 2000);       // dropped from the running sum
  h.tick(1000 + HR_COMPILE_MS);
  WL_CHECK(h.has_hr());
  WL_CHECK_EQ(h.hr(), (uint16_t)100);
}

// A pre-averaged feed publishes immediately and supersedes a raw window.
WL_TEST(health_hr_preaveraged) {
  HealthData h;
  h.add_hr_sample(200, 0);        // start a raw window...
  h.set_hr_avg(66, 5000);         // ...then a relay-averaged value lands
  WL_CHECK(h.has_hr());
  WL_CHECK_EQ(h.hr(), (uint16_t)66);
  // The superseded window must not later overwrite the 66 at the old boundary.
  h.tick(HR_COMPILE_MS);
  WL_CHECK_EQ(h.hr(), (uint16_t)66);
}

// ---- Staleness --------------------------------------------------------------
WL_TEST(health_staleness) {
  HealthData h;
  uint32_t t = 1000000;
  h.set_hr_avg(72, t);
  h.set_steps(4000, t);
  h.set_stress(30, t);
  h.set_sleep_score(90, t);

  // Fresh right after setting.
  WL_CHECK(!h.hr_stale(t));
  WL_CHECK(!h.steps_stale(t));
  WL_CHECK(!h.stress_stale(t));
  WL_CHECK(!h.sleep_stale(t));

  // HR goes stale first (6 min); steps/stress still fresh at that point.
  WL_CHECK(h.hr_stale(t + health::HR_STALE_MS));
  WL_CHECK(!h.steps_stale(t + health::HR_STALE_MS));

  WL_CHECK(h.stress_stale(t + health::STRESS_STALE_MS));
  WL_CHECK(h.steps_stale(t + health::STEPS_STALE_MS));
  WL_CHECK(h.sleep_stale(t + health::SLEEP_STALE_MS));

  // A never-set metric is not "stale" (nothing to show), just absent.
  HealthData empty;
  WL_CHECK(!empty.hr_stale(t + health::HR_STALE_MS));
}

// ---- Snapshot / restore preserves values and their ages --------------------
WL_TEST(health_snapshot_roundtrip) {
  HealthData h;
  uint32_t t = 500000;
  h.set_sleep_score(77, t);
  h.set_steps(8200, t);
  h.set_step_goal(9000);
  h.set_stress(25, t);
  h.set_hr_avg(68, t);

  // Snapshot 60 s later: every metric is 60 s old.
  uint32_t snap_at = t + 60000;
  HealthData::Snapshot s = h.snapshot(snap_at);
  WL_CHECK_EQ(s.hr_age_ms, (uint32_t)60000);
  WL_CHECK_EQ(s.step_goal, (uint32_t)9000);

  // Restore onto a fresh object against a brand-new boot clock.
  HealthData r;
  uint32_t boot = 10000;
  r.restore(s, boot);
  WL_CHECK_EQ(r.sleep_score(), (uint8_t)77);
  WL_CHECK_EQ(r.steps(), (uint32_t)8200);
  WL_CHECK_EQ(r.step_goal(), (uint32_t)9000);
  WL_CHECK_EQ(r.stress(), (uint8_t)25);
  WL_CHECK_EQ(r.hr(), (uint16_t)68);
  WL_CHECK(r.step_goal_reached() == false);   // 8200 < 9000

  // Ages carry over: 60 s old at boot, so not yet HR-stale (6 min window).
  WL_CHECK(!r.hr_stale(boot));
  WL_CHECK(r.hr_stale(boot + health::HR_STALE_MS));
}
