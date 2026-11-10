// test_drain_stats.cpp - host tests for the battery drain tracker (src/drain_stats.h).
#include "wl_test.h"
#include "drain_stats.h"

WL_TEST(drain_no_rate_before_ten_minutes_or_a_whole_percent) {
  drain::Tracker t;
  WL_CHECK_EQ(t.rate_x10(), -1);            // nothing sampled
  t.sample(0, 80, true, false);
  t.sample(300, 79, true, false);           // 1% but only 5 min
  WL_CHECK_EQ(t.rate_x10(), -1);
  t.sample(900, 79, true, false);
  WL_CHECK_EQ(t.rate_x10(), 40);            // 1% in 15 min = 4.0 %/h
  drain::Tracker flat;
  flat.sample(0, 80, true, false);
  flat.sample(3600, 80, true, false);       // an hour, no drop: still unknown
  WL_CHECK_EQ(flat.rate_x10(), -1);
}

WL_TEST(drain_rate_and_hours_left) {
  drain::Tracker t;
  t.sample(0, 90, true, true);
  t.sample(7200, 80, true, true);           // 10% in 2 h
  WL_CHECK_EQ(t.rate_x10(), 50);            // 5.0 %/h
  WL_CHECK_EQ(t.hours_left(), 16);          // 80% / 5 %/h
}

WL_TEST(drain_charger_ends_the_run_and_battery_starts_a_new_one) {
  drain::Tracker t;
  t.sample(0, 50, true, false);
  t.sample(3600, 40, true, false);
  WL_CHECK(t.rate_x10() > 0);
  t.sample(3660, 41, false, false);         // plugged in
  WL_CHECK(!t.active());
  WL_CHECK_EQ(t.rate_x10(), -1);
  WL_CHECK_EQ(t.hours_left(), -1);
  t.sample(7200, 90, true, false);          // unplugged: fresh run from here
  WL_CHECK(t.active());
  WL_CHECK_EQ(t.elapsed_s(), 0u);
  t.sample(7200 + 3600, 88, true, false);
  WL_CHECK_EQ(t.rate_x10(), 20);
}

WL_TEST(drain_screen_off_share_ignores_long_gaps) {
  drain::Tracker t;
  WL_CHECK_EQ(t.screen_off_pct(), -1);
  t.sample(0, 70, true, false);
  for (uint32_t s = 30; s <= 600; s += 30) t.sample(s, 70, true, true);    // 10 min off
  for (uint32_t s = 630; s <= 1200; s += 30) t.sample(s, 70, true, false); // 10 min on
  WL_CHECK_EQ(t.screen_off_pct(), 50);
  t.sample(1200 + 5000, 69, true, true);    // one huge step: not counted as off
  WL_CHECK(t.screen_off_pct() < 50);
}

WL_TEST(drain_bad_percent_is_ignored_as_a_run) {
  drain::Tracker t;
  t.sample(0, -1, true, false);             // gauge not ready
  WL_CHECK(!t.active());
  t.sample(10, 101, true, false);
  WL_CHECK(!t.active());
}
