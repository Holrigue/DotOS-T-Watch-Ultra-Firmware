// device_mode.cpp - device-side actuation for the mode arbiter. See device_mode.h.
#include "device_mode.h"
#include "ancs.h"
#include "ans.h"
#include "ble_scan_manager.h"

#include <WiFi.h>
#include <Preferences.h>
#include <lvgl.h>

static DeviceMode     s_mode     = DeviceMode::FieldTool;
static NotifyPlatform s_platform = NotifyPlatform::iOS;

// Boot-restore retry (see device_mode_restore_boot). A switch the USER makes
// cancels it, so a retry can never undo a choice made during the retry window.
static lv_timer_t *s_restore_timer = nullptr;
static bool        s_restoring     = false;   // true while the retry itself calls set()

static bool wifi_active() { return WiFi.getMode() != WIFI_MODE_NULL; }

// Persist the current enabled-state + platform to NVS. Called only on genuine,
// successful transitions so a WiFi-blocked attempt can never clobber the saved
// preference.
static void persist()
{
    Preferences p;
    if (!p.begin("argusnotify", false)) return;
    p.putBool("en", s_mode == DeviceMode::DailyWear);
    p.putUChar("plat", (uint8_t)s_platform);
    p.end();
}

DeviceMode device_mode_get() { return s_mode; }

bool device_mode_is_daily_wear() { return s_mode == DeviceMode::DailyWear; }

NotifyPlatform device_mode_platform() { return s_platform; }
void device_mode_set_platform(NotifyPlatform p) { s_platform = p; persist(); }

// Bring up the notification source for the selected platform. iOS -> ANCS
// (watch is GATT client), Android -> ANS/Gadgetbridge (watch is GATT server).
// Exactly one is ever up, so they never contend for the radio.
static bool start_notifications()
{
    return (s_platform == NotifyPlatform::iOS) ? ancs::start() : ans::start();
}

static void stop_notifications()
{
    ancs::stop();
    ans::stop();
}

ModeAction device_mode_set(DeviceMode requested)
{
    if (!s_restoring && s_restore_timer) {
        lv_timer_delete(s_restore_timer);
        s_restore_timer = nullptr;
    }
    ModeAction action = device_mode_plan(s_mode, requested, wifi_active());
    switch (action) {
    case ModeAction::StartNotifications:
        // start_*() re-check WiFi and return false if they somehow raced on;
        // treat that as still blocked (and do NOT persist, so the preference
        // survives a boot where WiFi happened to be up).
        if (!start_notifications()) return ModeAction::BlockedWifiActive;
        s_mode = DeviceMode::DailyWear;
        persist();
        break;
    case ModeAction::StopNotifications:
        stop_notifications();
        s_mode = DeviceMode::FieldTool;
        persist();
        break;
    case ModeAction::BlockedWifiActive:
    case ModeAction::NoChange:
        break;   // mode unchanged, nothing to persist
    }
    return action;
}

// Boot restore of the saved notification state.
//
// The platform (iOS / Android) is always restored, so the Notify screen shows
// the user's choice even when notifications were left off. If they were left
// ON, bring them back up. At boot the radio is often briefly busy (a boot
// radio, WiFi still tearing down, a BLE scan holding the GAP slot), which used
// to make the restore give up silently and leave Notify off until the user
// re-enabled it by hand. Instead, retry every few seconds for a while. A retry
// never rewrites the saved preference, so a boot where it cannot come up (WiFi
// kept on at boot, for example) still restores on the next one.
static constexpr uint32_t RESTORE_RETRY_MS    = 5000;
static constexpr uint32_t RESTORE_RETRY_COUNT = 24;    // ~2 minutes

static bool try_restore_notifications()
{
    if (s_mode == DeviceMode::DailyWear) return true;   // already up (user enabled it)
    if (ble_scan_active()) return false;                // don't fight a BLE scanner
    s_restoring = true;
    device_mode_set(DeviceMode::DailyWear);
    s_restoring = false;
    return s_mode == DeviceMode::DailyWear;
}

static uint32_t s_restore_runs = 0;

static void restore_retry_cb(lv_timer_t *t)
{
    bool done = try_restore_notifications();
    // Stop on success or after the last attempt. We delete the timer ourselves
    // (no repeat count) so s_restore_timer never points at a freed timer.
    if (done || ++s_restore_runs >= RESTORE_RETRY_COUNT) {
        lv_timer_delete(t);
        s_restore_timer = nullptr;
    }
}

void device_mode_restore_boot()
{
    Preferences p;
    if (!p.begin("argusnotify", true)) return;
    // Default-ON: a fresh watch (or one whose NVS was wiped by a full-erase
    // flash) boots into DailyWear with notifications up, instead of staying in
    // FieldTool until the user enables Notify by hand. A user who deliberately
    // turned Notify off has en=false persisted (the key is present), so their
    // choice is still respected - only the absent-key case defaults to on.
    bool    en   = p.getBool("en", true);
    uint8_t plat = p.getUChar("plat", (uint8_t)NotifyPlatform::iOS);
    p.end();

    s_platform = (plat == (uint8_t)NotifyPlatform::Android) ? NotifyPlatform::Android
                                                            : NotifyPlatform::iOS;
    if (!en) return;                        // was off; the platform is all we restore

    if (try_restore_notifications()) return;
    s_restore_runs  = 0;
    s_restore_timer = lv_timer_create(restore_retry_cb, RESTORE_RETRY_MS, NULL);
}
