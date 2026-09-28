// apps_screen.cpp - see apps_screen.h.
//
// A single scrollable list of app titles (the launcher) plus a second list of
// detector toggles (the activations page). Both are plain LVGL: a flex-column
// container of full-width rows, each a clickable lv_obj with a title label and
// a right-side chevron or ON/OFF pill. No icons, no drag-reorder, no mode
// gating - deliberately simple, per the redesign.
#include "apps_screen.h"
#include "theme.h"

#include <LilyGoLib.h>
#include <stdint.h>

// Detector toggle APIs (uniform start/stop/is_running across all of them).
#include "airtag.h"
#include "flipper.h"
#include "skimmer.h"
#include "flock.h"
#include "evil_twin.h"
#include "handshake.h"
#include "tracker_sweep.h"
#include "device_mode.h"

// Defined in main.cpp.
void clock_screen_show();
void main_loop_request_lvgl_priority(int cycles);

// Launcher targets - forward-declared rather than pulling ~30 screen headers.
// All are void()-returning show entry points (names verified against the headers).
void notifications_screen_show();
void music_screen_show();
void find_screen_show();
void compass_screen_show();
void presence_screen_show();
void threat_radar_screen_show();
void tracker_timeline_screen_show();
void spycam_screen_show();
void nfc_field_screen_show();
void pet_screen_show();
void wifi_screen_show();
void analyze_screen_show();
void deauth_screen_show();
void loot_screen_show();
void beacon_spam_screen_show();
void deauth_attack_screen_show();
void rogue_ap_screen_show();
void probe_sniffer_screen_show();
void mouse_screen_show();
void tesla_cp_screen_show();
void tpms_screen_show();
void pager_screen_show();
void aprs_screen_show();
void meshtastic_screen_show();
void usb_sd_screen_show();
void face_watch_screen_show();
void alarm_screen_show();
void stopwatch_screen_show();
void timer_screen_show();
void calendar_screen_show();
void world_clock_screen_show();
void sun_moon_screen_show();
void health_screen_show();
void flashlight_screen_show();
void settings_screen_show();

static const lv_color_t AW    = lv_color_hex(0xFFFFFF);           // title text
static const lv_color_t AG    = lv_color_hex(0x9A9A9A);           // secondary
static const lv_color_t AROW  = lv_color_hex(0x141414);          // row fill
static const lv_color_t AON   = lv_color_make(0x00, 0x66, 0x2A); // toggle ON pill
static const lv_color_t AOFF  = lv_color_make(0x2A, 0x2A, 0x2A); // toggle OFF pill

namespace {

struct App    { const char *title; void (*fn)(); };
struct Toggle { const char *title; bool (*start)(); void (*stop)(); bool (*running)(); };

// Notify (phone mirror) is a DeviceMode switch, not a detector, so it gets thin
// wrappers to match the uniform toggle shape.
bool notify_running() { return device_mode_is_daily_wear(); }
bool notify_start()   { device_mode_set(DeviceMode::DailyWear); return device_mode_is_daily_wear(); }
void notify_stop()    { device_mode_set(DeviceMode::FieldTool); }

const App APPS[] = {
    { "Notifications",   notifications_screen_show },
    { "Music",           music_screen_show },
    { "Find",            find_screen_show },
    { "Compass",         compass_screen_show },
    { "Presence Radar",  presence_screen_show },
    { "Threat Radar",    threat_radar_screen_show },
    { "Tracker Timeline",tracker_timeline_screen_show },
    { "Spycam Detector", spycam_screen_show },
    { "NFC Field",       nfc_field_screen_show },
    { "HexHound",        pet_screen_show },
    { "WiFi Survey",     wifi_screen_show },
    { "WiFi Analyze",    analyze_screen_show },
    { "Deauth Detector", deauth_screen_show },
    { "Loot",            loot_screen_show },
    { "Beacon Spam",     beacon_spam_screen_show },
    { "Deauther",        deauth_attack_screen_show },
    { "Rogue AP",        rogue_ap_screen_show },
    { "Probe Sniffer",   probe_sniffer_screen_show },
    { "BT Mouse",        mouse_screen_show },
    { "Tesla Charge",    tesla_cp_screen_show },
    { "TPMS",            tpms_screen_show },
    { "Pager 13:37",     pager_screen_show },
    { "LoRa APRS",       aprs_screen_show },
    { "Meshtastic",      meshtastic_screen_show },
    { "USB SD",          usb_sd_screen_show },
    { "Watch Face",      face_watch_screen_show },
    { "Alarm",           alarm_screen_show },
    { "Stopwatch",       stopwatch_screen_show },
    { "Timer",           timer_screen_show },
    { "Calendar",        calendar_screen_show },
    { "World Clock",     world_clock_screen_show },
    { "Sun / Moon",      sun_moon_screen_show },
    { "Health",          health_screen_show },
    { "Flashlight",      flashlight_screen_show },
    { "Settings",        settings_screen_show },
};
constexpr int APP_COUNT = (int)(sizeof(APPS) / sizeof(APPS[0]));

const Toggle TOGGLES[] = {
    { "AirTag (Find My)",   airtag_start,        airtag_stop,        airtag_is_running },
    { "Trackers",           tracker_sweep_start, tracker_sweep_stop, tracker_sweep_is_running },
    { "Flipper Zero",       flipper_start,       flipper_stop,       flipper_is_running },
    { "Card Skimmers",      skimmer_start,       skimmer_stop,       skimmer_is_running },
    { "Flock / Surveil.",   flock_start,         flock_stop,         flock_is_running },
    { "Evil Twin",          evil_twin_start,     evil_twin_stop,     evil_twin_is_running },
    { "Pwn (handshake)",    handshake_start,     handshake_stop,     handshake_is_running },
    { "Notify (phone)",     notify_start,        notify_stop,        notify_running },
};
constexpr int TOGGLE_COUNT = (int)(sizeof(TOGGLES) / sizeof(TOGGLES[0]));

}  // namespace

static lv_obj_t *s_apps = nullptr;   // launcher list
static lv_obj_t *s_acts = nullptr;   // activations (toggles) list
static lv_obj_t *s_toggle_pill[TOGGLE_COUNT];

// ---- row + list helpers -----------------------------------------------------

static lv_obj_t *make_list(lv_obj_t *screen, const char *title)
{
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr = lv_label_create(screen);
    lv_obj_set_style_text_font(hdr, &font_argus_label_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(hdr, AG, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(hdr, 3, LV_PART_MAIN);
    lv_label_set_text(hdr, title);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 26);

    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 440, 400);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 8, LV_PART_MAIN);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    return list;
}

// A full-width row: title on the left, a right-side accessory label (chevron or
// pill) returned so toggle rows can update it. cb gets `ud` as user data.
static lv_obj_t *make_row(lv_obj_t *list, const char *title, const char *right,
                          lv_event_cb_t cb, void *ud)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 62);
    lv_obj_set_style_bg_color(row, AROW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, 18, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, ud);

    lv_obj_t *t = lv_label_create(row);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(t, AW, LV_PART_MAIN);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *r = lv_label_create(row);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(r, AG, LV_PART_MAIN);
    lv_label_set_text(r, right ? right : LV_SYMBOL_RIGHT);
    lv_obj_align(r, LV_ALIGN_RIGHT_MID, 0, 0);
    return r;
}

static void set_pill(lv_obj_t *pill, bool on)
{
    lv_label_set_text(pill, on ? "ON" : "OFF");
    lv_obj_set_style_text_color(pill, on ? AW : AG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pill, on ? AON : AOFF, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(pill, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(pill, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(pill, 3, LV_PART_MAIN);
}

// ---- callbacks --------------------------------------------------------------

static void acts_refresh()
{
    for (int i = 0; i < TOGGLE_COUNT; i++)
        if (s_toggle_pill[i]) set_pill(s_toggle_pill[i], TOGGLES[i].running());
}

static void on_app_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < APP_COUNT && APPS[idx].fn) APPS[idx].fn();
}

static void on_toggle_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= TOGGLE_COUNT) return;
    if (TOGGLES[idx].running()) TOGGLES[idx].stop();
    else                        TOGGLES[idx].start();   // may no-op on radio conflict
    if (s_toggle_pill[idx]) set_pill(s_toggle_pill[idx], TOGGLES[idx].running());
}

static void open_activations(lv_event_t *)
{
    acts_refresh();
    lv_scr_load(s_acts);
}

static void on_apps_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_event_get_indev(e));
    if (dir == LV_DIR_RIGHT || dir == LV_DIR_TOP) clock_screen_show();
}

static void on_acts_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_event_get_indev(e));
    if (dir == LV_DIR_RIGHT || dir == LV_DIR_TOP) lv_scr_load(s_apps);
}

// ---- build ------------------------------------------------------------------

static void build()
{
    // Launcher list.
    s_apps = lv_obj_create(NULL);
    lv_obj_t *list = make_list(s_apps, "APPS");
    // Activations link first, then every app.
    lv_obj_t *acc = make_row(list, "Activations", LV_SYMBOL_RIGHT, open_activations, NULL);
    lv_obj_set_style_text_color(acc, lv_color_hex(0xE02020), LV_PART_MAIN);   // accent the entry
    for (int i = 0; i < APP_COUNT; i++)
        make_row(list, APPS[i].title, LV_SYMBOL_RIGHT, on_app_clicked, (void *)(intptr_t)i);
    lv_obj_add_event_cb(s_apps, on_apps_gesture, LV_EVENT_GESTURE, NULL);

    // Activations (toggles) list.
    s_acts = lv_obj_create(NULL);
    lv_obj_t *tlist = make_list(s_acts, "ACTIVATIONS");
    for (int i = 0; i < TOGGLE_COUNT; i++) {
        s_toggle_pill[i] = make_row(tlist, TOGGLES[i].title, "OFF",
                                    on_toggle_clicked, (void *)(intptr_t)i);
        set_pill(s_toggle_pill[i], TOGGLES[i].running());
    }
    lv_obj_add_event_cb(s_acts, on_acts_gesture, LV_EVENT_GESTURE, NULL);
}

// ---- public API -------------------------------------------------------------

void apps_screen_create() { if (!s_apps) build(); }

void apps_screen_show()
{
    if (!s_apps) build();
    main_loop_request_lvgl_priority(12);   // keep the first flick smooth
    lv_scr_load(s_apps);
}

bool apps_screen_is_active()
{
    return lv_screen_active() == s_apps || lv_screen_active() == s_acts;
}
