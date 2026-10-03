#pragma once
//
// night_mode.h - "Night time": quiet hours chosen by the wearer.
//
// While it is active (enabled AND the local time is inside the window):
//   - the watch does not vibrate - notifications, calls, touch feedback, the
//     detector alerts. A scheduled ALARM still rings and vibrates, and so does the
//     Find alert (you asked the watch to be found).
//   - an incoming notification does not light up a dimmed or switched-off screen.
//     It still lands in the notification list, and the home screen's notification
//     square shows it the next time you wake the watch yourself.
//
// The window is stored as minutes-of-day and may cross midnight (22:00 to 07:00).
// The pure rule lives in night_window.h and is host-tested.
#include <cstdint>

// Load the saved setting from NVS. Call once at boot.
void night_mode_boot_restore();

bool night_mode_enabled();
void night_mode_set_enabled(bool on);   // persists, applies at once

int  night_mode_start_min();            // 0..1439
int  night_mode_end_min();              // 0..1439
void night_mode_set_window(int start_min, int end_min);   // persists, applies at once

// Enabled AND now inside the window (local watch time).
bool night_mode_active();

// 1 Hz from the main loop: keeps the haptic motor in step with the window as the
// clock crosses its edges. Cheap when nothing changes.
void night_mode_tick();
