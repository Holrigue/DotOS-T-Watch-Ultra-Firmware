#pragma once
#include <lvgl.h>

// "Sante" screen: the wearer's health metrics mirrored from Gadgetbridge
// (sleep score, steps + goal, stress, heart rate). Opened by the "Health" tile
// (heart icon) in the Tools grid; swipe down returns home. Reads health_state.
void health_screen_create();
void health_screen_show();
bool health_screen_is_active();
void health_screen_update();   // refresh values; call on show and on the 1 Hz tick
