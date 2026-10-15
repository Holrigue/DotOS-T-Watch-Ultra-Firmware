// test_psram_array.cpp - host tests for the lazily allocated PSRAM table (src/psram_array.h).
#include "wl_test.h"
#include "psram_array.h"

#include <cstdint>

struct Row { char name[12]; float v; uint32_t n; };

WL_TEST(psram_array_starts_unallocated_and_zeroed) {
  PsramArray<Row, 8> t;
  WL_CHECK(!t.allocated());                 // costs nothing until used
  WL_CHECK_EQ((int)t[3].n, 0);              // first access allocates, zero-filled like .bss
  WL_CHECK(t.allocated());
  WL_CHECK_EQ((int)t[7].name[0], 0);
}

WL_TEST(psram_array_holds_values_per_index) {
  PsramArray<Row, 4> t;
  for (uint32_t i = 0; i < 4; i++) t[i].n = 100 + i;
  for (uint32_t i = 0; i < 4; i++) WL_CHECK_EQ((int)t[i].n, 100 + (int)i);
  WL_CHECK_EQ((int)(PsramArray<Row, 4>::size()), 4);
}

WL_TEST(psram_array_index_past_the_end_is_clamped_not_overrun) {
  PsramArray<Row, 4> t;
  t[3].n = 7;
  t[9].n = 99;                              // clamps to the last element instead of overrunning
  WL_CHECK_EQ((int)t[3].n, 99);
}

WL_TEST(psram_array_const_access) {
  PsramArray<Row, 2> t;
  t[1].n = 5;
  const PsramArray<Row, 2> &c = t;
  WL_CHECK_EQ((int)c[1].n, 5);
}
