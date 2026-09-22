// notifications_screen.h - LVGL screen listing mirrored phone notifications.
#pragma once
#include <lvgl.h>
#include "notify/notification.h"

// Build the screen once at boot (called from setup(), like the other screens).
void notifications_screen_create();

// Show it (from the Tools "Notify" tile).
void notifications_screen_show();

// Append one notification card (app / title / wrapped body) to `parent`. Shared
// with the pull-down notification shade so both surfaces render identically.
void notifications_add_card(lv_obj_t *parent, const notify::Notification *n);
