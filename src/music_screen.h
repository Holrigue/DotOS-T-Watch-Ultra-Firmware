#pragma once
#include <lvgl.h>

// Tools > Music: browse /music by artist (see music_lib.h for the folder
// convention) and play MP3/FLAC tracks. Opened from the Tools grid; swipe
// right / up on the artist list returns to whatever screen opened it. Three
// screens deep - Artists -> Tracks -> Now Playing - each with its own
// swipe-back to the level above.
void music_screen_show();
bool music_screen_is_active();
