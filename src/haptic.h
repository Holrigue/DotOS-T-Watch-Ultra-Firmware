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

// Re-apply the saved intensity's effect to the DRV2605 without persisting. Used
// to restore normal buzz after something temporarily forced a different effect
// (e.g. the Find alert maxing the motor), so the setting itself is untouched.
void haptic_reapply();

// Force the strongest buzz effect (Buzz 1 / 100%) immediately, ignoring the
// saved intensity. Pair with haptic_reapply() to restore. For urgent alerts
// (Find) that must be felt regardless of the user's comfort setting.
void haptic_force_max();

// Load a soft "wake" effect (DRV2605 Soft Bump) without persisting, so the
// repeated buzzes of a ringing alarm feel like a gentle nudge toward waking
// rather than an urgent alert. Pair with haptic_reapply() to restore the saved
// buzz when the alarm is dismissed.
void haptic_force_gentle();

// Night time: while on, the saved buzz is replaced by the silent effect, so every
// instance.vibrator() call (notifications, touch feedback, detector alerts) is
// quiet. Not persisted (night_mode owns the setting). The alarm is exempt because
// it loads its own effect with haptic_force_gentle(); Find forces the strongest.
void haptic_set_night(bool on);

// One self-contained gentle tap (Soft Bump), restoring the saved effect right
// after. For one-shot alerts like an incoming-call ring, where there is no
// natural dismiss point to restore at.
void haptic_alert_soft();

// The default when nothing is stored yet (about half the stock strength).
constexpr uint8_t HAPTIC_DEFAULT_PCT = 50;
