// test_dim_steps.cpp - host tests for the Dim Timer slider stops (src/dim_steps.h).
#include "wl_test.h"
#include "dim_steps.h"

WL_TEST(dim_steps_table_is_increasing_then_off) {
  for (int i = 1; i < kDimStepOff; i++)
    WL_CHECK(kDimStepSec[i] > kDimStepSec[i - 1]);
  WL_CHECK_EQ(kDimStepSec[kDimStepOff], 0);       // OFF is the last stop
  WL_CHECK(kDimStepCount > 10);                   // far finer than the old 5 choices
}

WL_TEST(dim_steps_seconds_clamps_the_index) {
  WL_CHECK_EQ(dim_step_seconds(0), 5);
  WL_CHECK_EQ(dim_step_seconds(-3), 5);
  WL_CHECK_EQ(dim_step_seconds(kDimStepOff), 0);
  WL_CHECK_EQ(dim_step_seconds(kDimStepOff + 9), 0);
}

WL_TEST(dim_steps_index_round_trips_every_stop) {
  for (int i = 0; i < kDimStepCount; i++)
    WL_CHECK_EQ(dim_step_index(dim_step_seconds(i)), i);
}

WL_TEST(dim_steps_index_snaps_old_saved_values) {
  WL_CHECK_EQ(dim_step_index(0), kDimStepOff);    // OFF stays OFF
  WL_CHECK_EQ(dim_step_seconds(dim_step_index(33)), 30);
  WL_CHECK_EQ(dim_step_seconds(dim_step_index(75)), 60);   // equidistant -> the earlier stop
  WL_CHECK_EQ(dim_step_seconds(dim_step_index(299)), 300);
  WL_CHECK_EQ(dim_step_seconds(dim_step_index(1000)), 300);
  WL_CHECK_EQ(dim_step_seconds(dim_step_index(1)), 5);
}
