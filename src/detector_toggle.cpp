// detector_toggle.cpp - see detector_toggle.h.
#include "detector_toggle.h"
#include "airtag.h"
#include "flipper.h"
#include "skimmer.h"
#include "flock.h"
#include "evil_twin.h"

#include <Arduino.h>
#include <Preferences.h>
#include <lvgl.h>

static const char *const NS          = "argusdet";
static const char *const KEY_PENDING = "rspend";
static const char *const KEY[(int)Detector::Count] = {
    "flock", "evilt", "airtag", "flipper", "skim"
};

static const uint32_t kRestoreDelayMs = 10000;   // after boot radios settle
static const uint32_t kStableMs       = 30000;   // survived -> clear the guard

bool detector_is_running(Detector d)
{
    switch (d) {
        case Detector::Flock:    return flock_is_running();
        case Detector::EvilTwin: return evil_twin_is_running();
        case Detector::AirTag:   return airtag_is_running();
        case Detector::Flipper:  return flipper_is_running();
        case Detector::Skimmer:  return skimmer_is_running();
        default:                 return false;
    }
}

int detector_count(Detector d)
{
    switch (d) {
        case Detector::Flock:    return flock_get_count();
        case Detector::EvilTwin: return evil_twin_get_count();
        case Detector::AirTag:   return airtag_get_count();
        case Detector::Flipper:  return flipper_get_count();
        case Detector::Skimmer:  return skimmer_get_count();
        default:                 return 0;
    }
}

static bool detector_start(Detector d)
{
    switch (d) {
        case Detector::Flock:    return flock_start();
        case Detector::EvilTwin: return evil_twin_start();
        case Detector::AirTag:   return airtag_start();
        case Detector::Flipper:  return flipper_start();
        case Detector::Skimmer:  return skimmer_start();
        default:                 return false;
    }
}

static void detector_stop(Detector d)
{
    switch (d) {
        case Detector::Flock:    flock_stop();     break;
        case Detector::EvilTwin: evil_twin_stop(); break;
        case Detector::AirTag:   airtag_stop();    break;
        case Detector::Flipper:  flipper_stop();   break;
        case Detector::Skimmer:  skimmer_stop();   break;
        default: break;
    }
}

void detector_remember(Detector d, bool on)
{
    if ((int)d >= (int)Detector::Count) return;
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.putBool(KEY[(int)d], on);
    p.end();
}

bool detector_toggle(Detector d)
{
    bool running;
    if (detector_is_running(d)) {
        detector_stop(d);
        running = false;
    } else {
        running = detector_start(d);
    }
    // Persist what actually happened: a start refused for lack of a radio is
    // saved as off, so the boot restore never retries something that failed
    // in front of the user.
    detector_remember(d, running);
    return running;
}

static void set_pending(bool on)
{
    Preferences p;
    if (!p.begin(NS, false)) return;
    p.putBool(KEY_PENDING, on);
    p.end();
}

static void clear_guard_cb(lv_timer_t *t)
{
    (void)t;
    set_pending(false);   // survived the restore window
}

static void restore_cb(lv_timer_t *t)
{
    (void)t;
    bool want[(int)Detector::Count] = {};
    bool any = false;
    Preferences p;
    if (p.begin(NS, true)) {
        for (int i = 0; i < (int)Detector::Count; i++) {
            want[i] = p.getBool(KEY[i], false);
            any |= want[i];
        }
        p.end();
    }
    if (!any) return;

    set_pending(true);
    for (int i = 0; i < (int)Detector::Count; i++) {
        Detector d = (Detector)i;
        // Best effort: a detector whose radio is busy just stays off this boot.
        // Its saved choice is NOT overwritten, so it comes back next time.
        if (want[i] && !detector_is_running(d)) detector_start(d);
    }
    lv_timer_t *g = lv_timer_create(clear_guard_cb, kStableMs, NULL);
    lv_timer_set_repeat_count(g, 1);
}

void detector_restore_on_boot()
{
    // A pending flag left from the previous boot means the watch reset while
    // detectors were being brought up: skip the restore once and clear it.
    bool pending = false;
    Preferences p;
    if (p.begin(NS, true)) {
        pending = p.getBool(KEY_PENDING, false);
        p.end();
    }
    if (pending) {
        set_pending(false);
        return;
    }

    uint32_t now   = millis();
    uint32_t delay = now < kRestoreDelayMs ? kRestoreDelayMs - now : 1;
    lv_timer_t *t = lv_timer_create(restore_cb, delay, NULL);
    lv_timer_set_repeat_count(t, 1);
}
