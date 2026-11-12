// stubs_gallery.cpp - host stand-ins for what the GALLERY screens call into.
// SIM ONLY. Never compiled into firmware.
//
// The gallery builds the REAL Settings, MENU, Facewatch and notification-shade
// sources so their layout can be looked at (and diffed) before a flash. Those
// screens reach into main.cpp, the radios, the SD log and so on; none of that
// has a host equivalent, so each entry point gets a stub with a fixed, plainly
// synthetic answer. Layout is what is under test here, not behaviour.
#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>
#include "device_mode.h"
#include "boot_prefs.h"
#include "usb_sd.h"
#include "night_mode.h"
#include "timezone.h"
#include "pin_pad_screen.h"
#include "mem_stats.h"
#include "detect_log_sd.h"
#include "music_player.h"
#include "ancs.h"
#include "ans.h"
#include "health_state.h"
#include "haptic.h"
#include "notify/notify_center.h"

// ---- main.cpp: clock screen setters/getters ---------------------------------
static bool s_12h = false, s_day = true, s_date = true;
void clock_screen_apply_face_custom()            {}
void clock_screen_set_12h(bool v)                { s_12h = v; }
bool clock_screen_get_12h()                      { return s_12h; }
void clock_screen_set_show_day(bool v)           { s_day = v; }
void clock_screen_set_show_date(bool v)          { s_date = v; }
bool clock_screen_get_show_day()                 { return s_day; }
bool clock_screen_get_show_date()                { return s_date; }
void clock_screen_set_show_secs(bool)            {}
void clock_screen_set_show_ampm(bool)            {}
void clock_screen_set_vibrate(bool)              {}
void clock_screen_set_motion_wake(bool)          {}
void clock_screen_set_motion_sensitivity(int)    {}
void clock_screen_set_manual_override(bool)      {}
void clock_screen_set_dim_timeout(uint32_t)      {}
void clock_screen_set_dim_brightness(uint8_t)    {}
void clock_screen_set_battery_saver(bool)        {}
void clock_screen_set_auto_brightness(bool)      {}
void clock_screen_restore_brightness()           {}
void clock_screen_set_face(int)                  {}
bool clock_screen_has_sun_location()             { return false; }
void clock_screen_apply_manual_time(int, int, int, int, int) {}
void clock_notifications_acknowledge()           {}
bool clock_has_unread_notifications()            { return true; }
void screen_return_to(lv_obj_t *)                {}

// ---- device mode / radios ---------------------------------------------------
bool           device_mode_is_daily_wear()                 { return true; }
NotifyPlatform device_mode_platform()                      { return NotifyPlatform::Android; }
void           device_mode_set_platform(NotifyPlatform)    {}
ModeAction     device_mode_set(DeviceMode)                 { return ModeAction{}; }
bool boot_prefs_get(BootRadio r)                           { return r == BOOT_RADIO_BLE; }
void boot_prefs_set(BootRadio, bool)                       {}
bool usb_sd_is_running()                                   { return false; }
bool usb_sd_start()                                        { return false; }
void usb_sd_stop()                                         {}
namespace ancs { bool is_connected() { return false; } void dismiss(uint32_t) {} }
namespace ans  { bool is_connected() { return true; } }

// ---- misc modules -----------------------------------------------------------
bool night_mode_enabled()                        { return false; }
clocksync::Stamp timezone_last_sync()            { return clocksync::Stamp{}; }
void pin_pad_screen_show()                       {}
void pin_pad_screen_show_then(void (*)())        {}
void mem_stats_format_low(char *out, size_t n)   { snprintf(out, n, "Low  : (sim)"); }
int  detect_log_total_records()                  { return 0; }
int  detect_log_clear_all()                      { return 0; }
static int s_vol = 60;
void music_player_set_volume(int v)              { s_vol = v; }
int  music_player_get_volume()                   { return s_vol; }
static uint32_t s_goal = 10000; static uint8_t s_vib = 50;
void     health_set_step_goal(uint32_t g)        { s_goal = g; }
uint32_t health_get_step_goal()                  { return s_goal; }
void     haptic_set_intensity(uint8_t v)         { s_vib = v; }
uint8_t  haptic_get_intensity()                  { return s_vib; }
void night_screen_show()                         {}
void battery_screen_show()                       {}
void map_screen_show()                           {}
void map_gpx_picker_show()                       {}
void level_screen_show()                         {}

// ---- notification hub: the real store, seeded by the harness ----------------
namespace notify {
NotificationStore &center() { static NotificationStore s; return s; }
void set_notif_vibrate(bool) {}
bool notif_vibrate()         { return true; }
void clear_all()             { center().clear(); }
}
