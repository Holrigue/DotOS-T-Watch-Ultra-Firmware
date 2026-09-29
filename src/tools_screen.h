#pragma once
#include <lvgl.h>

// The RECON/TOOLS grid was merged into the unified Apps launcher
// (apps_screen.cpp). These are the surviving entry points:
//   - _create() is now a no-op (the launcher is built by apps_screen_create());
//   - _show()/_is_active() forward to the launcher.
void tools_screen_create();
void tools_screen_show();
bool tools_screen_is_active();

// Attach a swipe-down->Apps-launcher shortcut to a sub-screen (radio/menu
// pages). Gated to Defense/Offense. Call once in the screen's _create().
// Defined in tools_screen.cpp.
void tools_attach_jump_gesture(lv_obj_t *screen);
