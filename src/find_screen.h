#pragma once
#include <lvgl.h>

// Tools > Find: a small "find my device" panel for the DotOS companion app.
// From here the watch rings the phone; from the phone app the user rings the
// watch (handled in ans/main). Opened from the Tools grid; swipe right / up
// returns to whatever screen it was opened over.
void find_screen_show();
bool find_screen_is_active();
