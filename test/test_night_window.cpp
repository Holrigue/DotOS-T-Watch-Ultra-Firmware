// test_night_window.cpp - host unit tests for the pure Night time window rule
// (src/night_window.h): a same-day window, a window that crosses midnight (the
// usual 22:00 - 07:00 case), the half-open edges, the empty window, and stepping
// a time around the clock.
#include "wl_test.h"
#include "night_window.h"

static int hm(int h, int m) { return h * 60 + m; }

// ---- A window inside one day. -------------------------------------------------
WL_TEST(night_same_day_window) {
  const int s = hm(13, 0), e = hm(15, 0);
  WL_CHECK(!night_in_window(hm(12, 59), s, e));
  WL_CHECK(night_in_window(hm(13, 0), s, e));    // start is inside
  WL_CHECK(night_in_window(hm(14, 59), s, e));
  WL_CHECK(!night_in_window(hm(15, 0), s, e));   // end is outside (half-open)
  WL_CHECK(!night_in_window(hm(3, 0), s, e));
}

// ---- The usual overnight window crosses midnight. -----------------------------
WL_TEST(night_crosses_midnight) {
  const int s = hm(22, 0), e = hm(7, 0);
  WL_CHECK(!night_in_window(hm(21, 59), s, e));
  WL_CHECK(night_in_window(hm(22, 0), s, e));
  WL_CHECK(night_in_window(hm(23, 59), s, e));
  WL_CHECK(night_in_window(hm(0, 0), s, e));     // midnight itself
  WL_CHECK(night_in_window(hm(6, 59), s, e));
  WL_CHECK(!night_in_window(hm(7, 0), s, e));    // wake-up minute is already normal
  WL_CHECK(!night_in_window(hm(12, 0), s, e));
}

// ---- An empty window is never active. -----------------------------------------
WL_TEST(night_empty_window_never_active) {
  for (int m = 0; m < NIGHT_MINUTES_PER_DAY; m += 30)
    WL_CHECK(!night_in_window(m, hm(22, 0), hm(22, 0)));
}

// ---- A window ending exactly at midnight. -------------------------------------
WL_TEST(night_window_ending_at_midnight) {
  const int s = hm(20, 0), e = hm(0, 0);          // 20:00 - 00:00 (wraps, e < s)
  WL_CHECK(night_in_window(hm(20, 0), s, e));
  WL_CHECK(night_in_window(hm(23, 59), s, e));
  WL_CHECK(!night_in_window(hm(0, 0), s, e));
  WL_CHECK(!night_in_window(hm(19, 59), s, e));
}

// ---- Stepping a time wraps around the clock. -----------------------------------
WL_TEST(night_step_wraps) {
  WL_CHECK_EQ(night_step_minutes(hm(23, 30), 30), hm(0, 0));
  WL_CHECK_EQ(night_step_minutes(hm(0, 0), -30), hm(23, 30));
  WL_CHECK_EQ(night_step_minutes(hm(7, 0), 30), hm(7, 30));
  WL_CHECK_EQ(night_step_minutes(hm(7, 0), -30), hm(6, 30));
  WL_CHECK_EQ(night_step_minutes(hm(12, 0), NIGHT_MINUTES_PER_DAY), hm(12, 0));
}
