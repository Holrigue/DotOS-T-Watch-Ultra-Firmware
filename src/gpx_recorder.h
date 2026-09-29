#pragma once
#include <stdint.h>

// gpx_recorder — log the live GPS fix to a .gpx file on the SD card while
// hiking. Runs on its own 1 Hz LVGL timer once started, so it keeps recording
// in the background regardless of which screen is showing (or if the display is
// dimmed). Points are throttled by time + distance so files stay small.
namespace gpxrec {

bool active();

// Open /gpx/rec_<YYYYMMDD-HHMMSS>.gpx and begin recording. Creates /gpx if
// needed. Returns false if the SD card or file could not be opened.
bool start();

// Finish the current file (close the GPX tags) and stop the timer.
void stop();

// Sample the GPS once and append a <trkpt> if it moved enough / enough time
// passed. Driven by the internal timer; safe to call directly too.
void tick();

int         points();   // trackpoints written so far
const char *path();     // current/last file path ("" when never started)

}  // namespace gpxrec
