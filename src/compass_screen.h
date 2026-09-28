#pragma once
#include <lvgl.h>

// Tools > Compass: true-north heading from the BHI260AP's magnetometer-fused
// ROTATION_VECTOR virtual sensor (NOT GAME_ROTATION_VECTOR, which the
// motion-wake code elsewhere in this codebase uses on purpose - that one
// skips the magnetometer and drifts, fine for a wake gesture, wrong for a
// compass). The sensor is only powered on while this screen is showing.
void compass_screen_show();
bool compass_screen_is_active();
