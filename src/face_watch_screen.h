#pragma once
#include <lvgl.h>

// Tools > Face: customization for the Dot watchface - hour/date fonts, accent
// colour, wallpaper, 12/24h, and date order. Opened from the Tools grid; swipe
// right / up returns to whatever screen it was opened over.
void face_watch_screen_create();
void face_watch_screen_show();
bool face_watch_screen_is_active();
