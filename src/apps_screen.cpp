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
    // Notify (phone mirror) is not a menu entry: its on/off toggle already lives
    // in the swipe-down notifications shade, so putting it here just duplicated it.
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

static lv_obj_t *s_home = nullptr;              // Notifications + category rows
static lv_obj_t *s_cat_screen[CAT_COUNT] = {};  // one PRE-BUILT screen per category

// Toggle pills across all category screens, so opening a category can refresh
// them from the live detector state (they may have changed elsewhere).
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

// A fixed "Settings" footer pinned to the bottom of the menu page. It is a
// direct child of the screen (NOT inside the scrolling list), created last so
// it renders above the list, and opaque so rows scroll under it cleanly.
static void add_settings_footer(lv_obj_t *screen)
{
    lv_obj_t *f = lv_button_create(screen);
    lv_obj_set_size(f, 344, 52);
    lv_obj_align(f, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(f, AROW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(f, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(f, 14, LV_PART_MAIN);
    lv_obj_set_style_border_width(f, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(f, [](lv_event_t *) { settings_screen_show(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(f);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, AW, LV_PART_MAIN);
    lv_label_set_text(l, "Settings");
    lv_obj_center(l);
    lv_obj_move_foreground(f);   // stay above the scrolling list (z-order)
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

static void refresh_pills()
{
    for (int k = 0; k < s_pill_n; k++)
        set_pill(s_pill[k], ENTRIES[s_pill_entry[k]].t_running());
}

// Category screens are PRE-BUILT at boot; opening one just refreshes its toggle
// pills and loads it. Crucially we do NOT clean/rebuild objects here: this runs
// inside the row's CLICKED callback, and deleting/creating LVGL objects mid-event
// corrupts the touch input state (the earlier version did that and froze touch).
static void open_category(Cat c)
{
    if (c < 0 || c >= CAT_COUNT || !s_cat_screen[c]) return;
    refresh_pills();
    lv_scr_load(s_cat_screen[c]);
}

// Offense holds active RF/network attack tools, so opening it requires explicit
// consent: a "Red team" modal the user must Accept, or dismiss with X to back
// out. Deliberately shown every time - it is a use-authorisation gate, not a
// one-off notice.
static lv_obj_t *s_offense_modal = nullptr;
static bool      s_offense_ok    = false;   // consent given this boot (resets on reboot)

// Delete the scrim ASYNC: we are inside a CLICKED callback of a button that is a
// DESCENDANT of the scrim, so deleting it synchronously here frees the object
// still dispatching the event - which corrupts LVGL's input state and freezes
// the touchscreen. lv_obj_delete_async defers the free to after event handling,
// the same pattern the dim-gate / wake-blocker / find overlays use.
static void offense_modal_close(lv_event_t *)
{
    if (s_offense_modal) { lv_obj_delete_async(s_offense_modal); s_offense_modal = nullptr; }
}

static void offense_modal_accept(lv_event_t *)
{
    if (s_offense_modal) { lv_obj_delete_async(s_offense_modal); s_offense_modal = nullptr; }
    s_offense_ok = true;   // don't nag again until the watch reboots
    open_category(CAT_OFFENSE);
}

static void show_offense_consent()
{
    if (s_offense_modal) return;
    lv_obj_t *scrim = lv_obj_create(lv_layer_top());
    s_offense_modal = scrim;
    lv_obj_remove_style_all(scrim);
    lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scrim, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_70, LV_PART_MAIN);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);   // swallow taps on the menu behind
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(scrim);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 324, 324);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x141414), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 18, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, AR, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 18, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *x = lv_button_create(card);
    lv_obj_set_size(x, 42, 42);
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, 6, -6);
    lv_obj_set_style_radius(x, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(x, AOFF, LV_PART_MAIN);
    lv_obj_add_event_cb(x, offense_modal_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *xl = lv_label_create(x);
    lv_obj_set_style_text_color(xl, AW, LV_PART_MAIN);
    lv_label_set_text(xl, LV_SYMBOL_CLOSE);
    lv_obj_center(xl);

    lv_obj_t *title = lv_label_create(card);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, AR, LV_PART_MAIN);
    lv_label_set_text(title, "Red team");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 6);

    lv_obj_t *body = lv_label_create(card);
    lv_obj_set_style_text_font(body, &font_argus_label_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(body, AG, LV_PART_MAIN);
    lv_obj_set_width(body, 288);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body,
        "Active RF and network tools.\n\n"
        "Use them only on devices and networks you own or are explicitly "
        "authorized to test. Misuse may be illegal.");
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 52);

    lv_obj_t *acc = lv_button_create(card);
    lv_obj_set_size(acc, 288, 58);
    lv_obj_align(acc, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(acc, AR, LV_PART_MAIN);
    lv_obj_set_style_radius(acc, 12, LV_PART_MAIN);
    lv_obj_add_event_cb(acc, offense_modal_accept, LV_EVENT_CLICKED, NULL);
    lv_obj_t *al = lv_label_create(acc);
    lv_obj_set_style_text_font(al, &font_argus_label_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(al, AW, LV_PART_MAIN);
    lv_label_set_text(al, "Accept");
    lv_obj_center(al);
}

static void on_cat_row(lv_event_t *e)
{
    Cat c = (Cat)(intptr_t)lv_event_get_user_data(e);
    // Offense needs consent once per boot; after Accept, open it directly.
    if (c == CAT_OFFENSE && !s_offense_ok) show_offense_consent();
    else                                   open_category(c);
}
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

// Build ONE category's screen once, at boot: its rows are created here, never
// during a tap. Toggle rows record their pill so open_category() can refresh it.
static void build_category_screen(Cat c)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_t *list = make_list(scr, CAT_NAME[c], NULL);
    for (int i = 0; i < ENTRY_COUNT; i++) {
        if (ENTRIES[i].cat != c) continue;
        if (ENTRIES[i].toggle) {
            lv_obj_t *pill = make_row(list, ENTRIES[i].title, "OFF", AW,
                                      on_toggle, (void *)(intptr_t)i);
            set_pill(pill, ENTRIES[i].t_running());
            if (s_pill_n < (int)(sizeof(s_pill) / sizeof(s_pill[0]))) {
                s_pill[s_pill_n]       = pill;
                s_pill_entry[s_pill_n] = i;
                s_pill_n++;
            }
        } else {
            make_row(list, ENTRIES[i].title, LV_SYMBOL_RIGHT, AW,
                     on_launch, (void *)(intptr_t)i);
        }
    }
    lv_obj_add_event_cb(scr, on_cat_gesture, LV_EVENT_GESTURE, NULL);
    s_cat_screen[c] = scr;
}

static void build()
{
    // Home: Notifications pinned at the top, then the category rows.
    s_home = lv_obj_create(NULL);
    lv_obj_t *hlist = make_list(s_home, "APPS", NULL);
    make_row(hlist, "Notifications", LV_SYMBOL_RIGHT, AR, on_notifications, NULL);
    make_row(hlist, "Tracking",     LV_SYMBOL_RIGHT, AW, on_cat_row, (void *)(intptr_t)CAT_TRACKING);
    make_row(hlist, "Defense",      LV_SYMBOL_RIGHT, AW, on_cat_row, (void *)(intptr_t)CAT_DEFENSE);
    make_row(hlist, "Offense",      LV_SYMBOL_RIGHT, AW, on_cat_row, (void *)(intptr_t)CAT_OFFENSE);
    make_row(hlist, "Apps",         LV_SYMBOL_RIGHT, AW, on_cat_row, (void *)(intptr_t)CAT_APPS);
    make_row(hlist, "Time & Clock", LV_SYMBOL_RIGHT, AW, on_cat_row, (void *)(intptr_t)CAT_TIMECLOCK);
    // Shrink the list so it ends above the fixed Settings footer (no overlap).
    lv_obj_set_height(hlist, 348);
    add_settings_footer(s_home);
    lv_obj_add_event_cb(s_home, on_home_gesture, LV_EVENT_GESTURE, NULL);

    // One pre-built screen per category (no rebuilding during a tap).
    for (int c = 0; c < CAT_COUNT; c++) build_category_screen((Cat)c);
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
    lv_obj_t *act = lv_screen_active();
    if (act == s_home) return true;
    for (int c = 0; c < CAT_COUNT; c++)
        if (act == s_cat_screen[c]) return true;
    return false;
}
