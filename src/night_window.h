#pragma once
// night_window.h - the pure "is this minute inside the quiet window" rule behind
// Night time. Header-only and free of any hardware, so it is covered by the host
// test suite (test/test_night_window.cpp).
#include <cstdint>

constexpr int NIGHT_MINUTES_PER_DAY = 24 * 60;

// True when `minute_of_day` (0..1439) lies in [start_min, end_min). The window may
// cross midnight: start 22:00 and end 07:00 cover 22:00 through 06:59. An empty
// window (start == end) is never active, so a half-configured setting can not
// silence the watch around the clock by accident.
inline bool night_in_window(int minute_of_day, int start_min, int end_min)
{
    if (start_min == end_min) return false;
    if (start_min < end_min) return minute_of_day >= start_min && minute_of_day < end_min;
    return minute_of_day >= start_min || minute_of_day < end_min;   // wraps past midnight
}

// Step a minute-of-day by `delta` minutes, wrapping around the clock so 23:30 + 30
// is 00:00 and 00:00 - 30 is 23:30.
inline int night_step_minutes(int minute_of_day, int delta)
{
    int m = (minute_of_day + delta) % NIGHT_MINUTES_PER_DAY;
    return m < 0 ? m + NIGHT_MINUTES_PER_DAY : m;
}
