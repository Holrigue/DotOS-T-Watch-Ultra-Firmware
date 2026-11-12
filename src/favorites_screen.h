#pragma once
#include <lvgl.h>

// A user-customisable page of app shortcuts, opened by swiping right on the
// watch face. The chosen set is persisted to /Settings/favorites.txt (one app
// name per line) so it survives a re-flash. Names come from apps_fav_name()
// (apps_screen.cpp): every launcher app plus Music and Health.
void favorites_init();            // load the saved list from SD; call once at boot
void favorites_screen_show();     // the favorites grid (rebuilt from the current list)
bool favorites_screen_is_active();
