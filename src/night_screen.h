#pragma once
// night_screen.h - the Settings > Night time sub-menu: an on/off switch plus the
// start and end of the quiet hours. See night_mode.h for what quiet hours do.
#include <lvgl.h>

void night_screen_show();
bool night_screen_is_active();
