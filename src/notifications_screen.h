// notifications_screen.h - LVGL screen listing mirrored phone notifications.
#pragma once
#include <lvgl.h>
#include <stddef.h>
#include "notify/notification.h"

// Build the screen once at boot (called from setup(), like the other screens).
void notifications_screen_create();

// Show it: from the Tools "Notify" tile, a tapped banner, or as the pull-down
// shade (swipe down on the watch face, or from the top edge of any screen).
// Swipe up / right returns to the screen it was opened over.
void notifications_screen_show();
bool notifications_screen_is_active();

// Append one notification card (app / title / wrapped body) to `parent`. Shared
// with the pull-down notification shade so both surfaces render identically.
void notifications_add_card(lv_obj_t *parent, const notify::Notification *n);

// Copy `in` into `out` (capacity outsz), dropping every codepoint the given font
// has no glyph for. Notification text can carry emoji/symbols the watch font
// lacks, which LVGL would otherwise draw as a "[]" tofu box; this strips them so
// the readable text stays clean. Whitespace is always kept. Returns `out`.
const char *notify_glyph_filter(const char *in, const lv_font_t *font,
                                char *out, size_t outsz);
