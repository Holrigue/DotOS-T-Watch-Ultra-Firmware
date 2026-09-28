#pragma once
#include <stdint.h>
#include <stdbool.h>

// detector_toggle.h - one control point for the passive detectors that
// the Tools tiles and the Dot face's detection badges both drive, so the two
// surfaces always agree and the user's on/off choice survives a reboot.
//
// Running state still comes straight from each detector (*_is_running()); this
// module only adds (a) a uniform start/stop by id and (b) NVS persistence of
// the user's choice, in the same Preferences style as device_mode / argus_mode
// (namespace "argusdet").
//
// Boot restore is deliberately cautious:
//   * deferred ~10 s after boot, after the boot radios and phone notifications
//     have claimed what they need (a detector that can't get its radio simply
//     stays off for this boot, and its saved choice is kept for next time);
//   * crash-guarded: a "restore pending" flag is set before starting and cleared
//     30 s later. If the watch resets inside that window, the next boot skips
//     the restore once instead of looping. Unlike the SD settings, NVS cannot be
//     fixed with a card reader, so this guard is the recovery path.

enum class Detector : uint8_t {
    Flock = 0,
    EvilTwin,
    AirTag,
    Flipper,
    Skimmer,
    HumanDetector,
    Count
};

bool detector_is_running(Detector d);
int  detector_count(Detector d);

// Persist the user's choice for one detector (NVS). Call after any start/stop
// the user asked for, from whichever surface they used.
void detector_remember(Detector d, bool on);

// Flip a detector and persist the resulting state. Returns the running state
// after the call: false after a start means the radio was not available.
bool detector_toggle(Detector d);

// Re-start the detectors the user left on. Call once at the end of setup().
void detector_restore_on_boot();
