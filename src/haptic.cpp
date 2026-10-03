// haptic.cpp - see haptic.h.
#include "haptic.h"

#include <Arduino.h>
#include <Preferences.h>
#include <LilyGoLib.h>   // instance, setHapticEffects()

namespace {

const char *const NS = "argushaptic";
uint8_t s_pct = HAPTIC_DEFAULT_PCT;
bool    s_night = false;   // Night time: play the silent effect instead of the saved buzz

// Map 0..100% onto the DRV2605 "Buzz" effects of library 1 (ERM), which the
// firmware selects at init. The ladder is graded 20/40/60/80/100%; 0 selects
// effect 0, which plays nothing (silent). Buzz reads as a clear, even vibration
// for a wrist alert, unlike the sharp stock effect.
uint8_t effect_for(uint8_t pct)
{
    if (pct == 0)   return 0;    // silent
    if (pct <= 25)  return 51;   // Buzz 5 - 20%
    if (pct <= 45)  return 50;   // Buzz 4 - 40%
    if (pct <= 65)  return 49;   // Buzz 3 - 60%
    if (pct <= 85)  return 48;   // Buzz 2 - 80%
    return 47;                   // Buzz 1 - 100%
}

void apply() { instance.setHapticEffects(s_night ? 0 : effect_for(s_pct)); }

}  // namespace

void haptic_boot_restore()
{
    Preferences p;
    if (p.begin(NS, true)) { s_pct = p.getUChar("pct", HAPTIC_DEFAULT_PCT); p.end(); }
    if (s_pct > 100) s_pct = 100;
    apply();
}

void haptic_set_intensity(uint8_t pct)
{
    if (pct > 100) pct = 100;
    s_pct = pct;
    apply();
    Preferences p;
    if (p.begin(NS, false)) { p.putUChar("pct", s_pct); p.end(); }
}

uint8_t haptic_get_intensity() { return s_pct; }

void haptic_reapply() { apply(); }

void haptic_force_max() { instance.setHapticEffects(47); }   // Buzz 1 / 100%

// DRV2605 ROM library 1 effect 7 = "Soft Bump 100%": a single soft nudge rather
// than the sustained Buzz, so a ringing alarm or an incoming call feels calm.
static constexpr uint8_t SOFT_ALERT_EFFECT = 7;

void haptic_force_gentle() { instance.setHapticEffects(SOFT_ALERT_EFFECT); }

void haptic_set_night(bool on)
{
    s_night = on;
    apply();
}

void haptic_alert_soft()
{
    if (s_night) return;   // Night time: an incoming call stays silent too
    instance.setHapticEffects(SOFT_ALERT_EFFECT);
    instance.vibrator();
    apply();   // restore the user's configured buzz for everything else
}
