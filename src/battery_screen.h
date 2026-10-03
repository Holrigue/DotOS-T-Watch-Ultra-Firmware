#pragma once
// battery_screen.h - the Settings > Battery sub-menu.
//
// One place that gathers every battery-related option and says in plain words
// what each one does:
//   - Auto turn off display  (your choice: screen fully black 1 minute after it dims)
//   - Battery longevity      (your choice: charge to 4.1 V for a longer cell life)
//   - Low-battery saver      (automatic, shown for information only)
#include <lvgl.h>

void battery_screen_show();
bool battery_screen_is_active();
