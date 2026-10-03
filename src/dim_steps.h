#pragma once
// dim_steps.h - the stops of the Dim Timer slider (Settings). Header-only and free
// of any hardware, so it is covered by the host tests (test/test_dim_steps.cpp).
//
// The slider walks an index through this table instead of mapping pixels to raw
// seconds, so every stop is evenly spaced under the finger: fine 5-10 s steps at
// the short end where it matters, wider ones further out, and OFF at the far end.
#include <cstdint>

// Seconds per stop; 0 = never dim (OFF). OFF is last so "all the way right" = never.
constexpr uint16_t kDimStepSec[] = {
    5, 10, 15, 20, 25, 30, 40, 50, 60, 90, 120, 180, 240, 300, 0,
};
constexpr int kDimStepCount = (int)(sizeof(kDimStepSec) / sizeof(kDimStepSec[0]));
constexpr int kDimStepOff   = kDimStepCount - 1;

inline uint32_t dim_step_seconds(int idx)
{
    if (idx < 0) idx = 0;
    if (idx > kDimStepOff) idx = kDimStepOff;
    return kDimStepSec[idx];
}

// The stop closest to a saved number of seconds (0 = OFF). Used when a settings
// file written by an older build (any value 0..300) is loaded.
inline int dim_step_index(uint32_t seconds)
{
    if (seconds == 0) return kDimStepOff;
    int best = 0;
    uint32_t best_d = 0xFFFFFFFFu;
    for (int i = 0; i < kDimStepOff; i++) {
        uint32_t s = kDimStepSec[i];
        uint32_t d = s > seconds ? s - seconds : seconds - s;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}
