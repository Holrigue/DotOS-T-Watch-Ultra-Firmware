// night_mode.cpp - see night_mode.h.
#include "night_mode.h"
#include "night_window.h"
#include "haptic.h"
#include "alarm.h"          // alarm_is_ringing(): never touch the motor mid-alarm

#include <Preferences.h>
#include <time.h>

// Defined in main.cpp: the displayed local time (RTC + UTC offset).
void clock_screen_get_local_time(struct tm *out);

namespace {

const char *const NS = "argusnight";

constexpr int DEFAULT_START_MIN = 22 * 60;   // 22:00
constexpr int DEFAULT_END_MIN   = 7 * 60;    // 07:00

bool s_enabled   = false;
int  s_start_min = DEFAULT_START_MIN;
int  s_end_min   = DEFAULT_END_MIN;
bool s_applied   = false;   // what the haptic motor was last told

int clamp_minute(int m)
{
    if (m < 0) return 0;
    if (m >= NIGHT_MINUTES_PER_DAY) return NIGHT_MINUTES_PER_DAY - 1;
    return m;
}

void persist()
{
    Preferences p;
    if (p.begin(NS, false)) {
        p.putBool("on", s_enabled);
        p.putUShort("start", (uint16_t)s_start_min);
        p.putUShort("end", (uint16_t)s_end_min);
        p.end();
    }
}

}  // namespace

void night_mode_boot_restore()
{
    Preferences p;
    if (p.begin(NS, true)) {
        s_enabled   = p.getBool("on", false);
        s_start_min = clamp_minute(p.getUShort("start", DEFAULT_START_MIN));
        s_end_min   = clamp_minute(p.getUShort("end", DEFAULT_END_MIN));
        p.end();
    }
    night_mode_tick();
}

bool night_mode_enabled() { return s_enabled; }
int  night_mode_start_min() { return s_start_min; }
int  night_mode_end_min()   { return s_end_min; }

void night_mode_set_enabled(bool on)
{
    s_enabled = on;
    persist();
    night_mode_tick();
}

void night_mode_set_window(int start_min, int end_min)
{
    s_start_min = clamp_minute(start_min);
    s_end_min   = clamp_minute(end_min);
    persist();
    night_mode_tick();
}

bool night_mode_active()
{
    if (!s_enabled) return false;
    struct tm t;
    clock_screen_get_local_time(&t);
    return night_in_window(t.tm_hour * 60 + t.tm_min, s_start_min, s_end_min);
}

void night_mode_tick()
{
    bool want = night_mode_active();
    if (want == s_applied) return;
    // An alarm that is ringing has loaded its own gentle haptic effect; changing
    // the motor's effect now would silence it. Try again next second.
    if (alarm_is_ringing()) return;
    s_applied = want;
    haptic_set_night(want);
}
