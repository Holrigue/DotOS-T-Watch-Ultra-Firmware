#pragma once
#include <lvgl.h>

// Tools > Compass: RELATIVE heading from the BHI260AP's GAME_ROTATION_VECTOR
// (accel+gyro). This board has no magnetometer, so there is no true north to
// show - the dial drifts and the user pins north with SET NORTH. A red
// navigation triangle points at the pinned north. The sensor is only powered
// on while this screen is showing. See compass_screen.cpp for the full story.
void compass_screen_show();
bool compass_screen_is_active();
