// test_notify_buzz_gate.cpp - host tests for the buzz rate-limit (src/notify_buzz_gate.h).
#include "wl_test.h"
#include "notify_buzz_gate.h"

WL_TEST(notify_buzz_gate_first_buzz_allowed) {
    uint32_t last = 0;
    WL_CHECK(notify_buzz_gate_allow(last, 1000, 2000));
    WL_CHECK(last == 1000);
}

WL_TEST(notify_buzz_gate_blocks_within_the_gap) {
    uint32_t last = 0;
    WL_CHECK(notify_buzz_gate_allow(last, 1000, 2000));
    WL_CHECK(!notify_buzz_gate_allow(last, 1500, 2000));   // 500ms later: blocked
    WL_CHECK(!notify_buzz_gate_allow(last, 2999, 2000));   // 1999ms later: still blocked
    WL_CHECK(last == 1000);                                 // state unchanged while blocked
}

WL_TEST(notify_buzz_gate_allows_after_the_gap) {
    uint32_t last = 0;
    notify_buzz_gate_allow(last, 1000, 2000);
    WL_CHECK(notify_buzz_gate_allow(last, 3000, 2000));    // exactly the gap later: allowed
    WL_CHECK(last == 3000);
}

WL_TEST(notify_buzz_gate_survives_millis_wrap) {
    uint32_t last = 0xFFFFF000u;                            // near the 32-bit wrap
    notify_buzz_gate_allow(last, 0xFFFFF000u, 2000);
    WL_CHECK(!notify_buzz_gate_allow(last, 0xFFFFF500u, 2000));  // +1280ms across wrap region
    WL_CHECK(notify_buzz_gate_allow(last, 0x00000200u, 2000));   // ~4600ms later (wrapped): allowed
}

WL_TEST(notify_buzz_gate_zero_now_does_not_wedge) {
    uint32_t last = 0;
    WL_CHECK(notify_buzz_gate_allow(last, 0, 2000));       // now==0 allowed
    WL_CHECK(last == 1);                                    // stored as 1, not 0 (0 = "never")
    WL_CHECK(!notify_buzz_gate_allow(last, 100, 2000));    // still within the gap
}
