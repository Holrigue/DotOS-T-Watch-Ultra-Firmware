#pragma once
#include <lvgl.h>

// Tools > Presence: a radar/sonar visual for the nearby-human BLE detector
// (human_detector.*). A sweep line rotates over range rings and each nearby
// phone/wearable shows as a blip whose distance from centre tracks its RSSI.
//
// The bearing of each blip is NOT a real direction - the watch has no
// directional antenna, so the angle is a stable per-MAC placement, and the
// screen says so. Range (RSSI) is the real signal; angle is just so multiple
// devices don't stack on one spot.
//
// Opening this screen starts the detector if it wasn't already running, and
// stops it again on exit only if this screen was the one that started it (so
// it never turns off a detector the user enabled from the tile).
void presence_screen_show();
bool presence_screen_is_active();
