// level_screen.h - a digital bubble level (spirit level) using the accelerometer.
#pragma once
#include <lvgl.h>

// Built lazily on first show, like the other app screens.
void level_screen_show();
bool level_screen_is_active();
