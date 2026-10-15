// test_ans_packet.cpp - host tests for the ANS packet helpers (src/notify/ans_packet.h).
#include "wl_test.h"
#include "ans_packet.h"

WL_TEST(ans_call_end_is_category_3_with_zero_count) {
  const uint8_t end[] = {3, 0};
  WL_CHECK(ans_is_call_end(end, sizeof end));
  const uint8_t endpad[] = {3, 0, 0, 0};            // trailing separators are fine
  WL_CHECK(ans_is_call_end(endpad, sizeof endpad));
}

WL_TEST(ans_a_ringing_call_is_not_an_end) {
  const uint8_t ring[] = {3, 1, 'B', 'o', 'b', 0, 'T', 'e', 'a', 'm', 's'};
  WL_CHECK(!ans_is_call_end(ring, sizeof ring));
}

WL_TEST(ans_other_categories_and_short_packets_are_not_an_end) {
  const uint8_t sms[] = {5, 0};
  WL_CHECK(!ans_is_call_end(sms, sizeof sms));
  const uint8_t one[] = {3};
  WL_CHECK(!ans_is_call_end(one, sizeof one));
  WL_CHECK(!ans_is_call_end(nullptr, 2));
}
