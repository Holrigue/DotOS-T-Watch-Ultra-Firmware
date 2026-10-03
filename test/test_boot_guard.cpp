// test_boot_guard.cpp - host tests for the pure boot-guard decision
// (src/boot_guard_logic.h): a clean history boots normally, an unfinished boot
// goes to safe mode, and a run of unfinished boots ends in a settings wipe.
#include "wl_test.h"
#include "boot_guard_logic.h"

WL_TEST(boot_guard_clean_history_is_normal) {
  WL_CHECK(boot_guard_level(0) == BootGuardLevel::Normal);
  WL_CHECK_EQ(boot_guard_next_count(0), 1);
}

WL_TEST(boot_guard_unfinished_boot_is_safe) {
  WL_CHECK(boot_guard_level(1) == BootGuardLevel::Safe);
  WL_CHECK(boot_guard_level(2) == BootGuardLevel::Safe);
  WL_CHECK_EQ(boot_guard_next_count(1), 2);
  WL_CHECK_EQ(boot_guard_next_count(2), 3);
}

WL_TEST(boot_guard_repeated_failure_resets) {
  WL_CHECK(boot_guard_level(kBootResetAfter) == BootGuardLevel::Reset);
  WL_CHECK(boot_guard_level(200) == BootGuardLevel::Reset);
  // A reset boot starts the count over, so the wipe is not repeated forever.
  WL_CHECK_EQ(boot_guard_next_count(kBootResetAfter), 1);
  WL_CHECK_EQ(boot_guard_next_count(255), 1);
}

// The walk a stuck watch takes: normal (hangs), safe (hangs), safe (hangs), reset.
WL_TEST(boot_guard_walk_of_a_stuck_watch) {
  uint8_t n = 0;
  const BootGuardLevel want[] = { BootGuardLevel::Normal, BootGuardLevel::Safe,
                                  BootGuardLevel::Safe, BootGuardLevel::Reset,
                                  BootGuardLevel::Safe };
  for (BootGuardLevel w : want) {
    WL_CHECK(boot_guard_level(n) == w);
    n = boot_guard_next_count(n);   // the boot never finishes, so the count stays raised
  }
}
