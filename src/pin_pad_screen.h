#pragma once
#include <lvgl.h>

// PIN pad for the Offense unlock. Neutral steel-blue - it leaks nothing about
// what it guards. First run (no PINs set) walks through setting the unlock PIN
// then the longer shred PIN. Normal run: enter a PIN -> unlock reveals Offense;
// the shred PIN runs the duress self-destruct behind a fake "Unlocking..." decoy.
// Reached by the side-button knock (Phase B); a temporary Settings entry opens it
// for now. Swipe right to cancel.

void pin_pad_screen_create();
void pin_pad_screen_show();
// Same as pin_pad_screen_show(), but on a successful unlock it runs on_unlock()
// instead of the default landing (the Tools grid). Used by the Apps-menu Offense
// entry so that, after consent + PIN, the watch returns to the Offense category.
// The callback is cleared after it fires (and reset by pin_pad_screen_show()).
void pin_pad_screen_show_then(void (*on_unlock)());
bool pin_pad_screen_is_active();
