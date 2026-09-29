// apps_screen.cpp - see apps_screen.h.
//
// The unified launcher, organised as category sub-menus. The home screen is a
// short list: Notifications (pinned at the top), then four category rows
// (Tracking / Defense / Offense / Apps), plus a pinned Settings gear in the
// corner. Each category opens a short, readable list; detectors appear inline
// as ON/OFF toggle rows, launchers open their screen.
//
// Everything is plain LVGL (a flex-column of full-width rows) - no icons, no
// drag-reorder, no mode gating. Kept deliberately narrow and centred so the
// text stays readable inside the round display.
#include "apps_screen.h"
#include "theme.h"

#include <LilyGoLib.h>
#include <stdint.h>

// Detector toggle APIs (uniform start/stop/is_running).
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

// Launcher targets - forward-declared (names verified against the headers).
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

static const lv_color_t AW   = lv_color_hex(0xFFFFFF);            // title text
static const lv_color_t AG   = lv_color_hex(0x9A9A9A);            // secondary
static const lv_color_t AR   = lv_color_hex(0xE02020);           // accent red
static const lv_color_t AROW = lv_color_hex(0x161616);           // row fill
static const lv_color_t AON  = lv_color_make(0x00, 0x66, 0x2A);  // toggle ON pill
static const lv_color_t AOFF = lv_color_make(0x2A, 0x2A, 0x2A);  // toggle OFF pill

namespace {

enum Cat { CAT_TRACKING = 0, CAT_DEFENSE, CAT_OFFENSE, CAT_APPS, CAT_TIMECLOCK, CAT_COUNT };
const char *CAT_NAME[CAT_COUNT] = { "TRACKING", "DEFENSE", "OFFENSE", "APPS", "TIME & CLOCK" };

struct Entry {
    const char *title;
    Cat         cat;
    bool        toggle;             // true = ON/OFF row, false = launcher
    void      (*launch)();
    bool      (*t_start)();
    void      (*t_stop)();
    bool      (*t_running)();
};

// Notify (phone mirror) is a DeviceMode switch; thin wrappers give it the
// uniform toggle shape.
bool notify_running() { return device_mode_is_daily_wear(); }
bool notify_start()   { device_mode_set(DeviceMode::DailyWear); return device_mode_is_daily_wear(); }
void notify_stop()    { device_mode_set(DeviceMode::FieldTool); }

#define L(title, cat, fn)              { title, cat, false, fn, nullptr, nullptr, nullptr }
#define T(title, cat, st, sp, run)     { title, cat, true, nullptr, st, sp, run }

const Entry ENTRIES[] = {
    // --- Tracking: who/what is tracking or near me -----------------------
    T("AirTag (Find My)", CAT_TRACKING, airtag_start,        airtag_stop,        airtag_is_running),
    T("Trackers",         CAT_TRACKING, tracker_sweep_start, tracker_sweep_stop, tracker_sweep_is_running),
    T("Flock / Surveil.", CAT_TRACKING, flock_start,         flock_stop,         flock_is_running),
    L("Threat Radar",     CAT_TRACKING, threat_radar_screen_show),
    L("Tracker Timeline", CAT_TRACKING, tracker_timeline_screen_show),
    L("Spycam Detector",  CAT_TRACKING, spycam_screen_show),

    // --- Defense: activations + defensive sensing ------------------------
    L("Presence Radar",   CAT_DEFENSE, presence_screen_show),
    T("Card Skimmers",    CAT_DEFENSE, skimmer_start, skimmer_stop, skimmer_is_running),
    T("Notify (phone)",   CAT_DEFENSE, notify_start,  notify_stop,  notify_running),
    L("NFC Field",        CAT_DEFENSE, nfc_field_screen_show),
    L("WiFi Survey",      CAT_DEFENSE, wifi_screen_show),
    L("WiFi Analyze",     CAT_DEFENSE, analyze_screen_show),
    L("Deauth Detector",  CAT_DEFENSE, deauth_screen_show),

    // --- Offense: active/attack tools ------------------------------------
    T("Evil Twin",        CAT_OFFENSE, evil_twin_start, evil_twin_stop, evil_twin_is_running),
    T("Pwn (handshake)",  CAT_OFFENSE, handshake_start, handshake_stop, handshake_is_running),
    L("Deauther",         CAT_OFFENSE, deauth_attack_screen_show),
    L("Beacon Spam",      CAT_OFFENSE, beacon_spam_screen_show),
    L("Rogue AP",         CAT_OFFENSE, rogue_ap_screen_show),
    L("Probe Sniffer",    CAT_OFFENSE, probe_sniffer_screen_show),
    L("Loot",             CAT_OFFENSE, loot_screen_show),
    L("BT Mouse",         CAT_OFFENSE, mouse_screen_show),
    L("Tesla Charge",     CAT_OFFENSE, tesla_cp_screen_show),

    // --- Apps: everyday tools --------------------------------------------
    L("Compass",          CAT_APPS, compass_screen_show),
    L("Music",            CAT_APPS, music_screen_show),
    L("Find",             CAT_APPS, find_screen_show),
    L("HexHound",         CAT_APPS, pet_screen_show),
    L("Health",           CAT_APPS, health_screen_show),
    L("Flashlight",       CAT_APPS, flashlight_screen_show),
    L("Meshtastic",       CAT_APPS, meshtastic_screen_show),
    L("Pager 13:37",      CAT_APPS, pager_screen_show),
    L("LoRa APRS",        CAT_APPS, aprs_screen_show),
    L("TPMS",             CAT_APPS, tpms_screen_show),

    // --- Time & Clock ----------------------------------------------------
    L("Alarm",            CAT_TIMECLOCK, alarm_screen_show),
    L("Stopwatch",        CAT_TIMECLOCK, stopwatch_screen_show),
    L("Timer",            CAT_TIMECLOCK, timer_screen_show),
    L("Calendar",         CAT_TIMECLOCK, calendar_screen_show),
    L("World Clock",      CAT_TIMECLOCK, world_clock_screen_show),
    L("Sun / Moon",       CAT_TIMECLOCK, sun_moon_screen_show),
    // USB SD and Facewatch now live inside Settings (pinned gear), not here.
};
#undef L
#undef T
constexpr int ENTRY_COUNT = (int)(sizeof(ENTRIES) / sizeof(ENTRIES[0]));

}  // namespace

static lv_obj_t *s_home      = nullptr;   // Notifications + category rows
static lv_obj_t *s_cat       = nullptr;   // one category's list (repopulated)
static lv_obj_t *s_cat_title = nullptr;
static lv_obj_t *s_cat_list  = nullptr;
static Cat       s_cur_cat   = CAT_TRACKING;

// Toggle pills currently on the category page, so a tap can refresh them.
static lv_obj_t *s_pill[24];
static int       s_pill_entry[24];
static int       s_pill_n = 0;

// ---- row + list helpers -----------------------------------------------------

static lv_obj_t *make_list(lv_obj_t *screen, const char *title, lv_obj_t **title_out)
{
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr = lv_label_create(screen);
    lv_obj_set_style_text_font(hdr, &font_argus_label_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(hdr, AG, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(hdr, 3, LV_PART_MAIN);
    lv_label_set_text(hdr, title);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 26);
    if (title_out) *title_out = hdr;

    // Narrow + centred so text stays clear of the round display's curve.
    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 344, 396);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 8, LV_PART_MAIN);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    return list;
}

// A full-width row: title left, right-side accessory (chevron or pill), returned
// so toggle rows can restyle it. cb gets `ud` as user data.
static lv_obj_t *make_row(lv_obj_t *list, const char *title, const char *right,
                          lv_color_t title_col, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 60);
    lv_obj_set_style_bg_color(row, AROW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, 16, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, ud);

    lv_obj_t *t = lv_label_create(row);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(t, title_col, LV_PART_MAIN);
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

// A pinned Settings gear in the top-right corner (overlay on the menu pages).
static void add_gear(lv_obj_t *screen)
{
    lv_obj_t *g = lv_obj_create(screen);
    lv_obj_remove_style_all(g);
    lv_obj_set_size(g, 46, 46);
    lv_obj_align(g, LV_ALIGN_TOP_RIGHT, -24, 30);
    lv_obj_set_style_radius(g, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g, AROW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g, [](lv_event_t *) { settings_screen_show(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ic = lv_label_create(g);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(ic, AG, LV_PART_MAIN);
    lv_label_set_text(ic, LV_SYMBOL_SETTINGS);
    lv_obj_center(ic);
}

// ---- callbacks --------------------------------------------------------------

static void on_launch(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < ENTRY_COUNT && ENTRIES[idx].launch) ENTRIES[idx].launch();
}

static void on_toggle(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= ENTRY_COUNT || !ENTRIES[idx].toggle) return;
    if (ENTRIES[idx].t_running()) ENTRIES[idx].t_stop();
    else                          ENTRIES[idx].t_start();   // may no-op on radio conflict
    for (int k = 0; k < s_pill_n; k++)
        set_pill(s_pill[k], ENTRIES[s_pill_entry[k]].t_running());
}

static void open_category(Cat c)
{
    s_cur_cat = c;
    lv_label_set_text(s_cat_title, CAT_NAME[c]);
    lv_obj_clean(s_cat_list);
    s_pill_n = 0;
    for (int i = 0; i < ENTRY_COUNT; i++) {
        if (ENTRIES[i].cat != c) continue;
        if (ENTRIES[i].toggle) {
            lv_obj_t *pill = make_row(s_cat_list, ENTRIES[i].title, "OFF", AW,
                                      on_toggle, (void *)(intptr_t)i);
            set_pill(pill, ENTRIES[i].t_running());
            if (s_pill_n < (int)(sizeof(s_pill) / sizeof(s_pill[0]))) {
                s_pill[s_pill_n]       = pill;
                s_pill_entry[s_pill_n] = i;
                s_pill_n++;
            }
        } else {
            make_row(s_cat_list, ENTRIES[i].title, LV_SYMBOL_RIGHT, AW,
                     on_launch, (void *)(intptr_t)i);
        }
    }
    lv_scr_load(s_cat);
}

static void on_cat_row(lv_event_t *e) { open_category((Cat)(intptr_t)lv_event_get_user_data(e)); }
static void on_notifications(lv_event_t *) { notifications_screen_show(); }

// Nav convention: LEFT -> home clock. On a category page, UP (or RIGHT) goes
// back up to the Apps home; on the Apps home itself, LEFT/RIGHT go home.
static void on_home_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_event_get_indev(e));
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) clock_screen_show();
}

static void on_cat_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_event_get_indev(e));
    if (dir == LV_DIR_LEFT)                        clock_screen_show();
    else if (dir == LV_DIR_TOP || dir == LV_DIR_RIGHT) lv_scr_load(s_home);
}

// ---- build ------------------------------------------------------------------

static void build()
{
    // Home: Notifications pinned at the top, then the four categories.
    s_home = lv_obj_create(NULL);
    lv_obj_t *hlist = make_list(s_home, "APPS", NULL);
    lv_obj_t *n = make_row(hlist, "Notifications", LV_SYMBOL_RIGHT, AR, on_notifications, NULL);
    (void)n;
    make_row(hlist, "Tracking", LV_SYMBOL_RIGHT, AW, on_cat_row, (void *)(intptr_t)CAT_TRACKING);
    make_row(hlist, "Defense",  LV_SYMBOL_RIGHT, AW, on_cat_row, (void *)(intptr_t)CAT_DEFENSE);
    make_row(hlist, "Offense",  LV_SYMBOL_RIGHT, AW, on_cat_row, (void *)(intptr_t)CAT_OFFENSE);
    make_row(hlist, "Apps",     LV_SYMBOL_RIGHT, AW, on_cat_row, (void *)(intptr_t)CAT_APPS);
    make_row(hlist, "Time & Clock", LV_SYMBOL_RIGHT, AW, on_cat_row, (void *)(intptr_t)CAT_TIMECLOCK);
    add_gear(s_home);
    lv_obj_add_event_cb(s_home, on_home_gesture, LV_EVENT_GESTURE, NULL);

    // Category page (repopulated by open_category()).
    s_cat = lv_obj_create(NULL);
    s_cat_list = make_list(s_cat, "TRACKING", &s_cat_title);
    add_gear(s_cat);
    lv_obj_add_event_cb(s_cat, on_cat_gesture, LV_EVENT_GESTURE, NULL);
}

// ---- public API -------------------------------------------------------------

void apps_screen_create() { if (!s_home) build(); }

void apps_screen_show()
{
    if (!s_home) build();
    main_loop_request_lvgl_priority(12);   // keep the first flick smooth
    lv_scr_load(s_home);
}

bool apps_screen_is_active()
{
    return lv_screen_active() == s_home || lv_screen_active() == s_cat;
}
