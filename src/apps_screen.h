#pragma once
#include <lvgl.h>

// The unified "Apps" launcher, replacing the old two-grid split (the "RECON"
// tools grid in tools_screen.cpp and the "TOOLS" clock-utility grid in
// time_screen.cpp). The home screen lists Notifications (pinned at the top)
// then four category rows - Tracking / Defense / Offense / Apps - plus a
// pinned Settings gear in the corner. Each category opens a short, readable
// list where detectors show inline as ON/OFF toggle rows and everything else
// opens its screen.
//
// Everything is shown regardless of ArgusMode (no gating) - the offensive
// screens keep their own transmit-time mode guards. tools_screen_show() and
// time_screen_show() forward here, so every sub-screen's "back" gesture lands
// on this one menu.
void apps_screen_create();
void apps_screen_show();
bool apps_screen_is_active();
