// tools_screen.cpp — retired RECON/TOOLS grid, now a thin forwarder.
//
// The 27-tile grid this file used to build was merged into the unified Apps
// launcher (apps_screen.cpp): categories replaced the flat mode-gated grid, so
// the grid was built but never shown. It is gone now — ~2.3k lines of tile
// construction, the HD icon sprites, and the flipper logo image with it — to
// reclaim flash and drop the dead dependency web (every detector/launcher header
// this file used to pull in).
//
// What stays are the small entry points other screens still call:
//   - tools_screen_show() / tools_screen_is_active(): every sub-screen's "back"
//     gesture forwards to the launcher through these.
//   - tools_attach_jump_gesture(): the swipe-down shortcut on radio/tool
//     sub-screens, now opening the launcher (kept identical, Daily still inert).
//   - tools_screen_create(): a no-op; the launcher is built by
//     apps_screen_create() in main.cpp's boot path.
#include "tools_screen.h"
#include "apps_screen.h"
#include "argus_mode.h"

// The old RECON grid is merged into the unified Apps launcher, so this entry
// (and every sub-screen's "back" gesture that calls it) forwards there.
void tools_screen_show() { apps_screen_show(); }
bool tools_screen_is_active() { return apps_screen_is_active(); }

// The launcher is built by apps_screen_create(); nothing to build here anymore.
// Kept so main.cpp's boot sequence (which calls both) needs no edit.
void tools_screen_create() {}

// Swipe-DOWN -> Apps launcher, on tool/radio sub-screens that attach it. Mirrors
// the old swipe-down->Tools shortcut and stays gated to Defense/Offense (Daily
// left it inert). A pull from the very top edge belongs to the notification
// shade, so that case is handed back. Screens with vertically-scrolling content
// still scroll; the gesture only fires when the scroll doesn't consume the swipe.
bool touch_started_at_top_edge();   // main.cpp: a pull from the top edge opens the shade

static void tools_jump_gesture_cb(lv_event_t *e)
{
    if (touch_started_at_top_edge()) return;   // that pull belongs to the notification shade
    lv_indev_t *indev = lv_event_get_indev(e);
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM &&
        argus_mode_current() != ArgusMode::Daily)
        tools_screen_show();
}

void tools_attach_jump_gesture(lv_obj_t *screen)
{
    if (screen) lv_obj_add_event_cb(screen, tools_jump_gesture_cb, LV_EVENT_GESTURE, NULL);
}
