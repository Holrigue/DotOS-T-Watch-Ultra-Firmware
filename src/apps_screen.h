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
void apps_screen_show();                 // the "Menu" home (category list)
void apps_screen_show_apps_category();   // shortcut straight into the "Apps" sub-menu
bool apps_screen_is_active();

// ---- Favorites page support ------------------------------------------------
// The set of apps/screens a user can pin to the Favorites page: every launcher
// entry plus Music and Health. Addressed by name so the saved list survives a
// firmware update that reorders the table.
int         apps_fav_count();
const char *apps_fav_name(int i);            // nullptr past the end
void        apps_fav_launch_by_name(const char *name);   // no-op if the name is gone
