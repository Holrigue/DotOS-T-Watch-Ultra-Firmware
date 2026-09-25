#pragma once
#include <lvgl.h>

// "TOOLS" grid screen reached by swiping UP from the clock face. Holds the
// everyday utilities: Alarm, Stopwatch, Timer, Calendar, World, Sun/Moon,
// Health, Flashlight, Meshtastic, Notify, Settings. (The recon/offense grid
// reached by swiping LEFT is titled "RECON".) File name kept as time_screen
// for history.

void time_screen_create();
void time_screen_show();
bool time_screen_is_active();
