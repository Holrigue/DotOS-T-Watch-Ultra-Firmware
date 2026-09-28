#pragma once
#include <lvgl.h>

// The unified "Apps" launcher: one swipe-scrollable list of titles that
// replaces the old two-grid split (the "RECON" tools grid in tools_screen.cpp
// and the "TOOLS" clock-utility grid in time_screen.cpp). Tapping a title opens
// that app; a top "Activations" row opens a second list of the in-place
// detector toggles (AirTag, Flipper, Flock, ...) as labelled on/off rows.
//
// Everything is shown regardless of ArgusMode (no gating) - the offensive
// screens keep their own transmit-time mode guards. tools_screen_show() and
// time_screen_show() forward here, so every sub-screen's "back" gesture lands
// on this one menu.
void apps_screen_create();
void apps_screen_show();
bool apps_screen_is_active();
