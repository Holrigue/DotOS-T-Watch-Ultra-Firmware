// notify_popup.h - smartwatch-style banner that pops over the watch face when a
// notification arrives.
//
// A notification arrives on the BLE task; this shows a floating banner on the
// LVGL top layer (above the clock and every screen), driven from the UI thread
// so LVGL stays single-threaded. The banner auto-dismisses after a few seconds,
// or tap it to open the full Notify list.
#pragma once

// Start the UI-thread poll timer. Call once from setup(), after LVGL is up.
void notify_popup_init();

// True while a notification banner is on screen (its brightness boost is live).
// The dim timer checks this so it never dims the screen under a banner.
bool notify_popup_is_showing();
