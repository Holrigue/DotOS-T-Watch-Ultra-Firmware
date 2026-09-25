#pragma once
//
// haptic.h - global vibration intensity for the whole watch.
//
// Every buzz (notifications, calls, alarms, timers) goes through the shared
// instance.vibrator(), which plays one DRV2605 ROM effect. We pick that effect
// from a graded "Buzz" ladder by an intensity percent, so a single setting scales
// all haptics. 0% selects the silent effect. The default is deliberately mild -
// the stock effect was uncomfortably strong.
#include <cstdint>

// Load the saved intensity from NVS and apply it. Call once at boot, after the
// hardware is up and Preferences is usable.
void haptic_boot_restore();

// Set (0..100), apply immediately, and persist. Used by the Settings slider.
void haptic_set_intensity(uint8_t pct);

// Current intensity percent.
uint8_t haptic_get_intensity();

// The default when nothing is stored yet (about half the stock strength).
constexpr uint8_t HAPTIC_DEFAULT_PCT = 50;
