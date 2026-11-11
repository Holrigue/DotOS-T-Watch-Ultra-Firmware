#include "settings_screen.h"
#include "theme.h"
#include "usb_sd.h"
#include "timezone.h"      // last-sync stamp for the clock-trust row
#include "usb_sd_screen.h"   // card reader, reachable in EVERY mode (see below)
#include "boot_prefs.h"
#include "argus_mode.h"
#include "pin_pad_screen.h"
#include "charge_state.h"
#include "detect_log_sd.h"
#include "detect/log_retention.h"   // kMaxAgeDays, for the readout
#include "health_state.h"           // daily step goal (Dot progress bar + Health screen)
#include "haptic.h"                  // global vibration intensity
#include "notify/notify_center.h"    // notification-vibration toggle (separate from touch feedback)
#include "battery_screen.h"          // Settings > Battery sub-menu
#include "night_screen.h"            // Settings > Night time sub-menu
#include "night_mode.h"              // On/Off readout on the Night time row
#include "dim_steps.h"                // evenly spaced stops of the Dim Timer slider
#include "drain_stats.h"              // measured battery drain (%/h)
#include "mem_stats.h"                // lowest-ever free RAM + largest free block
#include <LilyGoLib.h>
#include <SD.h>
#include <time.h>
#include <Arduino.h>          // millis()
#include <esp_heap_caps.h>    // heap_caps_get_free_size / _total_size

// Defined in main.cpp
void main_loop_request_lvgl_priority(int cycles);
void low_mem_show_dialog(const char *msg);   // modal dialog defined in main.cpp
void clock_screen_show();                    // go home to the watch face
void face_watch_screen_show();               // Dot watchface customization (Settings entry)
void clock_screen_set_face(int mode);        // DotOS only; arg ignored (always Dot)
void clock_screen_set_12h(bool use_12h);
bool clock_screen_get_12h();
void clock_screen_set_dim_timeout(uint32_t ms);
void clock_screen_set_dim_brightness(uint8_t level);
void clock_screen_set_show_day(bool show);
void clock_screen_set_show_date(bool show);
bool clock_screen_get_show_day();
bool clock_screen_get_show_date();
void clock_screen_set_show_ampm(bool show);
void clock_screen_set_show_secs(bool show);
void clock_screen_set_vibrate(bool enabled);
void clock_screen_set_motion_wake(bool enabled);
void clock_screen_set_motion_sensitivity(int level);   // 1 (big movement) .. 5 (small)
void clock_screen_set_auto_brightness(bool on);        // follow the sun
void clock_screen_set_battery_saver(bool on);          // "Auto turn off display": screen fully off 1 min after the dim
bool clock_screen_has_sun_location();                  // a GPS fix has been saved
void clock_screen_restore_brightness();                // apply the active brightness now
void clock_screen_set_manual_override(bool on);
void clock_screen_apply_manual_time(int year, int mon, int day, int hour, int min);
void clock_screen_get_local_time(struct tm *out);

// Manual-time roller range: years selectable in the date picker.
#define MANUAL_YEAR_BASE  2020
#define MANUAL_YEAR_COUNT 41   // 2020 … 2060

static lv_obj_t *settings_screen;
static lv_obj_t *s_ofs_btn  = nullptr;  // one Offense button; label+action follow the mode
static lv_obj_t *s_sync_lbl = nullptr;  // "clock last synced" trust row

// Render the clock-trust row from the persisted stamp. Refreshed on every
// settings_screen_show(), because the answer ages by itself: a row built once
// would still read "synced today" a month later, which is the exact false
// confidence this exists to remove.
static void settings_update_clock_sync()
{
    if (!s_sync_lbl) return;

    const clocksync::Stamp st = timezone_last_sync();

    struct tm now;
    instance.rtc.getDateTime(&now);             // UTC, same basis as the stamp
    clocktime::DateTime nu = { now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
                               now.tm_hour, now.tm_min, now.tm_sec };

    const bool stale = clocksync::is_stale(st, nu);
    char buf[96];
    if (!st.valid) {
        snprintf(buf, sizeof(buf),
                 "Clock NEVER SYNCED - time and log stamps are unverified");
    } else {
        const int age = clocksync::age_days(st, nu);
        if (age < 0) {
            // The stamp post-dates the RTC, so one of the two is wrong and we
            // cannot say which. Refusing to pick is the honest answer.
            snprintf(buf, sizeof(buf),
                     "Clock sync stamp is in the future (%04d-%02d-%02d) - not trusted",
                     st.utc.year, st.utc.mon, st.utc.day);
        } else {
            snprintf(buf, sizeof(buf), "Clock synced %04d-%02d-%02d (%s), %d day%s ago%s",
                     st.utc.year, st.utc.mon, st.utc.day,
                     clocksync::source_name(st.src), age, age == 1 ? "" : "s",
                     stale ? " - STALE" : "");
        }
    }
    lv_label_set_text(s_sync_lbl, buf);
    // Emphasis by BRIGHTNESS, not hue. Red on this watch means a live threat
    // (see the Threat Radar accent rules) and gray means a hardware fault; an
    // unvouched-for clock is neither, so it gets full-brightness body text
    // against the dim it normally sits in rather than a colour it has not
    // earned.
    lv_obj_set_style_text_color(s_sync_lbl,
                                stale ? ARGUS_TEXT : ARGUS_TEXT_DIM, LV_PART_MAIN);
}
static lv_obj_t *s_ofs_lbl = nullptr;
static lv_obj_t *brightness_slider;
static lv_obj_t *brightness_val_label;
static lv_obj_t *sysinfo_label = nullptr;   // System Info readout (bottom of screen)
static lv_obj_t *s_settings_title = nullptr;   // "SETTINGS" heading; repainted per mode on show

// Fill the System Info label with live heap / PSRAM / battery / uptime. Cheap;
// called on settings_screen_show() so it snapshots each time the screen opens.
static void settings_update_sysinfo()
{
    if (!sysinfo_label) return;
    unsigned heap_kb  = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    unsigned ps_free  = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    unsigned ps_total = (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024);
    int  batt = instance.pmu.getBatteryPercent();
    // Same three-state split the clock-face bolt uses, so the two readouts
    // never disagree. Snapshot (no debounce): this label is rebuilt on show,
    // not ticked, so there is nothing here to strobe.
    ChargeState cs = charge_state_raw(instance.pmu.isVbusIn(),
                                      instance.pmu.isCharging());
    const char *chg = (cs == ChargeState::Charging) ? " chg"
                    : (cs == ChargeState::Topped)   ? " full"
                                                    : "";
    uint32_t up = millis() / 1000;
    // Detection-record count. This walks the logs on SD, so it is deliberately
    // sampled here (settings_screen_show) rather than on a tick.
    int det = detect_log_total_records();
    char low[40];
    mem_stats_format_low(low, sizeof low);
    // Measured drain over the current run on battery. The PMU has no current
    // sensor, so this slope is the only real consumption figure there is.
    char drn[48];
    {
        const drain::Tracker &t = drain::tracker();
        int r = t.rate_x10();
        if (!t.active())      snprintf(drn, sizeof drn, "on charger");
        else if (r < 0)       snprintf(drn, sizeof drn, "measuring (%lum)",
                                       (unsigned long)(t.elapsed_s() / 60));
        else                  snprintf(drn, sizeof drn, "%d.%d%%/h ~%dh left",
                                       r / 10, r % 10, t.hours_left());
    }
    int off_pct = drain::tracker().screen_off_pct();
    if (off_pct < 0) off_pct = 0;
    lv_label_set_text_fmt(sysinfo_label,
        "Heap : %u KB free\n"
        "Low  : %s\n"
        "PSRAM: %u / %u KB\n"
        "Batt : %d%%%s\n"
        "Drain: %s\n"
        "Off  : %d%% of run\n"
        "Up   : %luh %02lum %02lus\n"
        "Det  : %d record(s), %dd max",
        heap_kb, low, ps_free, ps_total,
        batt, chg,
        drn, off_pct,
        (unsigned long)(up / 3600), (unsigned long)((up % 3600) / 60),
        (unsigned long)(up % 60),
        det, detlog::kMaxAgeDays);
}
static int32_t   s_brightness = DEVICE_MAX_BRIGHTNESS_LEVEL;
static lv_obj_t *dim_timeout_slider;
static lv_obj_t *dim_timeout_val_label;
static lv_obj_t *dim_brightness_slider;
static lv_obj_t *dim_brightness_val_label;
static int32_t   s_dim_brightness = DEVICE_MAX_BRIGHTNESS_LEVEL / 4;
// The Digital face's "AM/PM" and "seconds" options have no control any more
// (the Dot face is the only face), but a saved settings.txt may still carry
// them, so the values are kept and written back unchanged.
static bool s_show_ampm = true;
static bool s_show_secs = true;
// Show Day / Show Date now live on the Facewatch Customize screen (face_watch_screen.cpp).
static lv_obj_t *vibrate_switch;   // "Vibration": ordinary notifications buzz
static lv_obj_t *touch_switch;     // "Touch feedback": UI gestures / button buzz
static lv_obj_t *motion_wake_switch;
static lv_obj_t *motion_wake_val_label;
static lv_obj_t *motion_sens_slider;
static lv_obj_t *motion_sens_val_label;
static lv_obj_t *step_goal_slider;
static lv_obj_t *step_goal_val_label;
static lv_obj_t *vib_intensity_slider;
static lv_obj_t *vib_intensity_val_label;
static lv_obj_t *auto_bright_switch;
static lv_obj_t *auto_bright_val_label;
// "Auto turn off display" (Settings > Battery). The switch itself lives on the
// Battery screen; the value is kept here so it persists with the other settings.
static bool s_auto_off_display = false;
static lv_obj_t *s_night_hint = nullptr;   // "On" / "Off" readout on the Night time row
static lv_obj_t *manual_time_switch;
static lv_obj_t *manual_time_val_label;
// Screenshot long-press toggle — bottom of the settings list. Disabled
// (greyed + forced off) whenever SD isn't mounted, since there's nowhere
// to write the capture without a card.
static lv_obj_t *screenshot_row;
static lv_obj_t *screenshot_switch;
static lv_obj_t *screenshot_val_label;
static lv_obj_t *screenshot_hint;
// "Enable at boot" per-radio opt-in toggles. Each writes its flag to
// /Settings/boot_radios.txt via boot_prefs (NOT to settings.txt). No Bluetooth
// toggle: the BLE controller must never be brought up at boot (it boot-loops
// the watch), so BT is manual-only and has no boot pref.
static lv_obj_t *boot_wifi_switch;
static lv_obj_t *boot_wifi_val_label;
static lv_obj_t *boot_ble_switch;
static lv_obj_t *boot_ble_val_label;
static lv_obj_t *boot_lora_switch;
static lv_obj_t *boot_lora_val_label;
static lv_obj_t *boot_gps_switch;
static lv_obj_t *boot_gps_val_label;
static lv_obj_t *year_roller;
static lv_obj_t *month_roller;
static lv_obj_t *day_roller;
static lv_obj_t *hour_roller;
static lv_obj_t *minute_roller;

// Suppresses settings_save_to_sd() during settings_screen_load() so applying
// loaded values via lv_obj_add_state / lv_dropdown_set_selected — which fire
// LV_EVENT_VALUE_CHANGED — doesn't redundantly rewrite the file we just read.
static bool g_loading = false;

// Step goal and vibration intensity live in NVS (health / haptic modules), which
// a re-flash ERASES, so they used to reset on every flash. We now also persist
// them to the SD settings file (which survives a flash) and re-apply them LATE at
// boot - after health_boot_restore() / haptic_boot_restore() have run - so the SD
// value wins over the wiped-NVS default. -1 = no value in the SD file yet.
static int s_sd_step_goal = -1;
static int s_sd_vib       = -1;

// ARGUS Daily/Defense mode controls (appended to the bottom of the list).
static lv_obj_t *defense_switch;
static lv_obj_t *defense_val_label;
static lv_obj_t *defpersist_switch;
static lv_obj_t *defpersist_val_label;

// ---- Layout ----------------------------------------------------------------
//
// The list is a FLEX COLUMN: every row is a direct child of the screen, created
// in the order it is shown, and LVGL stacks them. A hidden row takes no space,
// so the rows below close up by themselves.
//
// It used to be laid out by hand: each row carried an absolute y, plus three
// stacked corrections (the hidden face rows, the collapsed Manual Time editor,
// the six power rows inserted later). Adding or removing a row meant renumbering
// everything under it, and a missed number showed up on the watch as a dead gap
// or two rows drawn on top of each other. None of that bookkeeping exists now:
// to add a row, create it where it should appear.
#define ROW_GAP 8   // vertical gap between rows (the old 48 px pitch of 40 px rows)

// ---- Manual-time-only registry --------------------------------------------
//
// The manual-time hint line, the roller block and the SET TIME button only make
// sense when the manual-time switch is on. apply_layout() shows or hides them
// as a group; the flex column does the rest.
#define MAX_MANUAL_TIME_OBJS 16
static lv_obj_t *s_manual_objs[MAX_MANUAL_TIME_OBJS];
static int       s_manual_count = 0;

static void register_manual_obj(lv_obj_t *obj)
{
    if (s_manual_count < MAX_MANUAL_TIME_OBJS)
        s_manual_objs[s_manual_count++] = obj;
}

// DotOS is the only watch face now, so this is fixed at 2 (FACE_DOT). Kept as a
// function because apply_layout() and the SD save still call it; the old
// Digital/Analog/Dot dropdown it used to read is gone.
static inline int settings_face_mode()
{
    return 2;   // FACE_DOT
}

// ---- Sections ---------------------------------------------------------------
//
// The list is grouped into collapsible sections so Settings opens as six short
// headings instead of five screenfuls of rows. One section is open at a time.
//
// Rows are still built in one pass by settings_screen_create(). While building,
// sec(X) says "the rows created from here on belong to section X"; once every
// row exists, finish_sections() reads those marks, adds the six headers and
// moves the rows into section order (the flex column lays them out by index).
enum : uint8_t { SEC_DISPLAY, SEC_SOUND, SEC_TIME, SEC_CONN, SEC_SYSTEM, SEC_SECURITY,
                 SEC_COUNT, SEC_ALWAYS = 0xFE, SEC_NEVER = 0xFF };
static const char *const SEC_NAME[SEC_COUNT] = {
    "Display", "Sound & vibration", "Time", "Connections", "System", "Security" };

struct SecMark { uint32_t first_child; uint8_t sec; };
struct SecRow  { lv_obj_t *obj; uint8_t sec; };
static SecMark   s_marks[40];
static int       s_mark_count = 0;
static SecRow    s_rows[80];
static int       s_row_count = 0;
static bool      s_sec_open[SEC_COUNT] = { true, false, false, false, false, false };
static lv_obj_t *s_sec_chev[SEC_COUNT] = { nullptr };

static void sec(uint8_t section)
{
    if (s_mark_count < (int)(sizeof(s_marks) / sizeof(s_marks[0])))
        s_marks[s_mark_count++] = { lv_obj_get_child_count(settings_screen), section };
}

static void apply_layout();

static void on_section_header(lv_event_t *e)
{
    int which = (int)(intptr_t)lv_event_get_user_data(e);
    bool open = !s_sec_open[which];
    for (int i = 0; i < SEC_COUNT; i++) s_sec_open[i] = false;   // one at a time
    s_sec_open[which] = open;
    apply_layout();
    lv_obj_update_layout(settings_screen);
    lv_obj_scroll_to_view((lv_obj_t *)lv_event_get_current_target(e), LV_ANIM_ON);
}

static void finish_sections()
{
    // 1. Which section does each existing row belong to?
    uint32_t n = lv_obj_get_child_count(settings_screen);
    s_row_count = 0;
    int mark = -1;
    for (uint32_t i = 0; i < n && s_row_count < (int)(sizeof(s_rows) / sizeof(s_rows[0])); i++) {
        while (mark + 1 < s_mark_count && s_marks[mark + 1].first_child <= i) mark++;
        s_rows[s_row_count++] = { lv_obj_get_child(settings_screen, i),
                                  mark >= 0 ? s_marks[mark].sec : (uint8_t)SEC_ALWAYS };
    }

    // 2. One tappable header per section.
    lv_obj_t *hdr[SEC_COUNT];
    for (int k = 0; k < SEC_COUNT; k++) {
        lv_obj_t *h = lv_obj_create(settings_screen);
        lv_obj_remove_style_all(h);
        lv_obj_set_size(h, 380, 46);
        lv_obj_set_style_bg_color(h, ARGUS_TILE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(h, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(h, ARGUS_RAISED, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_radius(h, 12, LV_PART_MAIN);
        lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(h, on_section_header, LV_EVENT_CLICKED, (void *)(intptr_t)k);

        lv_obj_t *l = lv_label_create(h);
        lv_obj_set_style_text_font(l, theme_text_font(20), LV_PART_MAIN);
        lv_obj_set_style_text_color(l, ARGUS_TEXT, LV_PART_MAIN);
        lv_label_set_text(l, SEC_NAME[k]);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 14, 0);

        s_sec_chev[k] = lv_label_create(h);
        lv_obj_set_style_text_font(s_sec_chev[k], &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_sec_chev[k], ARGUS_ACCENT, LV_PART_MAIN);
        lv_obj_align(s_sec_chev[k], LV_ALIGN_RIGHT_MID, -14, 0);
        hdr[k] = h;
    }

    // 3. Section order: [always rows] then, per section, its header and its rows
    //    in the order they were built. Rows marked SEC_NEVER (the old separators
    //    and sub-headings the section headers replace) stay hidden at the end.
    int32_t idx = 0;
    for (int i = 0; i < s_row_count; i++)
        if (s_rows[i].sec == SEC_ALWAYS) lv_obj_move_to_index(s_rows[i].obj, idx++);
    for (int k = 0; k < SEC_COUNT; k++) {
        lv_obj_move_to_index(hdr[k], idx++);
        for (int i = 0; i < s_row_count; i++)
            if (s_rows[i].sec == k) lv_obj_move_to_index(s_rows[i].obj, idx++);
    }
}

// Show/hide rows: a row shows when its section is open AND its own condition
// (face rows, Manual Time editor) allows it. Positions are the flex column's job.
static void apply_layout()
{
    // The 12h / AM-PM / Show-seconds rows only apply to the Digital face; both
    // Analog and Dot render their own time, so those rows hide for either.
    bool manual_on = lv_obj_has_state(manual_time_switch, LV_STATE_CHECKED);

    for (int i = 0; i < s_row_count; i++) {
        lv_obj_t *o = s_rows[i].obj;
        uint8_t   sc = s_rows[i].sec;
        bool show = (sc == SEC_ALWAYS) || (sc < SEC_COUNT && s_sec_open[sc]);
        for (int m = 0; m < s_manual_count; m++)
            if (s_manual_objs[m] == o) show = show && manual_on;
        if (show) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag  (o, LV_OBJ_FLAG_HIDDEN);
    }
    for (int k = 0; k < SEC_COUNT; k++)
        if (s_sec_chev[k])
            lv_label_set_text(s_sec_chev[k], s_sec_open[k] ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
}

// Open or close every section at once (the simulator gallery uses this to
// capture the whole list; also handy for a future "expand all").
void settings_screen_expand_all(bool open)
{
    for (int k = 0; k < SEC_COUNT; k++) s_sec_open[k] = open;
    if (settings_screen) apply_layout();
}

static const char *SETTINGS_PATH = "/Settings/settings.txt";

// Forward declaration so the change callbacks can call it
static void settings_save_to_sd();

// Format the dim-timeout value label (0 = OFF, else s / m / m ss).
static void dim_timeout_show(uint32_t sec)
{
    char buf[16];
    if (sec == 0)           snprintf(buf, sizeof buf, "OFF");
    else if (sec < 60)      snprintf(buf, sizeof buf, "%us", (unsigned)sec);
    else if (sec % 60 == 0) snprintf(buf, sizeof buf, "%um", (unsigned)(sec / 60));
    else                    snprintf(buf, sizeof buf, "%um%02us", (unsigned)(sec / 60), (unsigned)(sec % 60));
    if (dim_timeout_val_label) lv_label_set_text(dim_timeout_val_label, buf);
}

static void on_dim_timeout_changed(lv_event_t *e)
{
    (void)e;
    uint32_t sec = dim_step_seconds((int)lv_slider_get_value(dim_timeout_slider));
    clock_screen_set_dim_timeout(sec * 1000);
    dim_timeout_show(sec);   // live; saved on release (on_slider_released)
}

static void on_dim_brightness_changed(lv_event_t *e)
{
    s_dim_brightness = lv_slider_get_value(dim_brightness_slider);
    clock_screen_set_dim_brightness((uint8_t)s_dim_brightness);
    int pct = (int)s_dim_brightness * 100 / (int)DEVICE_MAX_BRIGHTNESS_LEVEL;
    lv_label_set_text_fmt(dim_brightness_val_label, "%d%%", pct);
    // No save here — a drag fires VALUE_CHANGED on every pixel.
    // The save runs once on LV_EVENT_RELEASED via on_slider_released.
}

// Slider drags fire VALUE_CHANGED many times per drag. Hooking the save into
// LV_EVENT_RELEASED instead means we write the SD once when the user lifts
// their finger. Taps on the track also fire RELEASED, so single-tap changes
// still persist.
static void on_slider_released(lv_event_t *e)
{
    settings_save_to_sd();
}

static void on_vibrate_changed(lv_event_t *e)
{
    // "Vibration" now governs notification buzzes (touch feedback is separate).
    notify::set_notif_vibrate(lv_obj_has_state(vibrate_switch, LV_STATE_CHECKED));
    settings_save_to_sd();
}

static void on_touch_feedback_changed(lv_event_t *e)
{
    // UI gestures / the hardware button buzz, independent of notifications.
    clock_screen_set_vibrate(lv_obj_has_state(touch_switch, LV_STATE_CHECKED));
    settings_save_to_sd();
}

static void on_motion_wake_changed(lv_event_t *e)
{
    bool on = lv_obj_has_state(motion_wake_switch, LV_STATE_CHECKED);
    lv_label_set_text(motion_wake_val_label, on ? "On" : "Off");
    clock_screen_set_motion_wake(on);
    settings_save_to_sd();
}

static void on_motion_sens_changed(lv_event_t *e)
{
    int level = lv_slider_get_value(motion_sens_slider);
    lv_label_set_text_fmt(motion_sens_val_label, "%d", level);
    clock_screen_set_motion_sensitivity(level);
    // Save deferred to LV_EVENT_RELEASED (on_slider_released).
}

// Daily step goal for the Dot face's progress bar and the Health screen. Rounded
// to the nearest 500 for a tidy value; persisted immediately to health NVS.
static void on_step_goal_changed(lv_event_t *e)
{
    (void)e;
    int v = lv_slider_get_value(step_goal_slider);
    v = ((v + 250) / 500) * 500;          // snap to 500
    if (v < 1000) v = 1000;
    lv_label_set_text_fmt(step_goal_val_label, "%d", v);
    health_set_step_goal((uint32_t)v);
}

// Global vibration intensity (0..100%), snapped to 5%. Scales every buzz
// (notifications, calls, alarms, timers) through the haptic module, which
// persists it. 0 = silent.
static void on_vib_intensity_changed(lv_event_t *e)
{
    (void)e;
    int v = lv_slider_get_value(vib_intensity_slider);
    v = ((v + 2) / 5) * 5;                // snap to 5
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    lv_label_set_text_fmt(vib_intensity_val_label, "%d%%", v);
    haptic_set_intensity((uint8_t)v);
}

static void show_auto_bright_state(bool on)
{
    // "No GPS" until the watch has saved one fix: the sun needs a position.
    lv_label_set_text(auto_bright_val_label,
                      !on ? "Off" : (clock_screen_has_sun_location() ? "On" : "No GPS"));
}

static void on_auto_bright_changed(lv_event_t *e)
{
    bool on = lv_obj_has_state(auto_bright_switch, LV_STATE_CHECKED);
    clock_screen_set_auto_brightness(on);
    show_auto_bright_state(on);
    settings_save_to_sd();
}

bool settings_get_auto_off_display() { return s_auto_off_display; }

void settings_set_auto_off_display(bool on)
{
    s_auto_off_display = on;
    clock_screen_set_battery_saver(on);
    settings_save_to_sd();
}

static void on_screenshot_changed(lv_event_t *)
{
    bool on = lv_obj_has_state(screenshot_switch, LV_STATE_CHECKED);
    lv_label_set_text(screenshot_val_label, on ? "On" : "Off");
    settings_save_to_sd();
}

// Show Day / Show Date controls moved to the Facewatch Customize screen; their
// state is still persisted to settings.txt here (settings_save_to_sd reads it
// straight from clock_screen_get_show_day/date), and the Facewatch handlers call
// settings_persist_now() after toggling so the change is written.

int settings_get_brightness()
{
    return (int)s_brightness;
}

// Public persistence hook: the Facewatch Customize screen owns the Show Day /
// Show Date toggles now, but their state is still saved in settings.txt, so it
// calls this after toggling. Safe because settings_screen_create() runs at boot,
// so every switch settings_save_to_sd() reads already exists.
void settings_persist_now()
{
    settings_save_to_sd();
}

void settings_set_brightness(int level, bool save)
{
    if (level < 1) level = 1;
    if (level > DEVICE_MAX_BRIGHTNESS_LEVEL) level = DEVICE_MAX_BRIGHTNESS_LEVEL;
    s_brightness = level;
    clock_screen_restore_brightness();   // sun-scaled when Auto brightness is on
    if (brightness_slider) lv_slider_set_value(brightness_slider, s_brightness, LV_ANIM_OFF);
    if (brightness_val_label) {
        int pct = (int)s_brightness * 100 / (int)DEVICE_MAX_BRIGHTNESS_LEVEL;
        lv_label_set_text_fmt(brightness_val_label, "%d%%", pct);
    }
    if (save) settings_save_to_sd();
}

static void on_brightness_changed(lv_event_t *e)
{
    s_brightness = lv_slider_get_value(brightness_slider);
    clock_screen_restore_brightness();   // sun-scaled when Auto brightness is on
    int pct = (int)s_brightness * 100 / (int)DEVICE_MAX_BRIGHTNESS_LEVEL;
    lv_label_set_text_fmt(brightness_val_label, "%d%%", pct);
    // Save deferred to LV_EVENT_RELEASED — see on_slider_released.
}

// Builds a newline-joined option string for a numeric roller, e.g. lo=0 hi=23
// width=2 → "00\n01\n…\n23". The roller copies the string, so a temp buffer is
// fine.
static void build_num_opts(char *buf, size_t bufsz, int lo, int hi, int width)
{
    size_t pos = 0;
    buf[0] = '\0';
    for (int v = lo; v <= hi && pos < bufsz; v++) {
        int n = snprintf(buf + pos, bufsz - pos, "%0*d", width, v);
        if (n < 0) break;
        pos += (size_t)n;
        if (v < hi && pos < bufsz - 1) buf[pos++] = '\n';
    }
    buf[pos < bufsz ? pos : bufsz - 1] = '\0';
}

// Number of days in a given month, accounting for leap years.
static int days_in_month(int month, int year)
{
    static const int dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) return 31;
    if (month == 2) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return dim[month - 1];
}

// Creates a header label + roller pair as children of settings_screen, both
// horizontally centred at x_off. Returns the roller. Both children are
// registered as shiftable so the analog-face reflow moves them as a unit.
// `parent` is the roller block (one flex row of the list); x_off places this
// column inside it, header on top and the roller 22 px below.
static lv_obj_t *make_time_roller(lv_obj_t *parent, const char *header, const char *opts,
                                  lv_coord_t x_off, lv_coord_t w)
{
    lv_obj_t *hl = lv_label_create(parent);
    lv_obj_set_style_text_color(hl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(hl, theme_text_font(16), LV_PART_MAIN);
    lv_label_set_text(hl, header);
    lv_obj_align(hl, LV_ALIGN_TOP_MID, x_off, 0);

    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(r, 3);
    lv_obj_set_width(r, w);
    lv_obj_set_style_bg_color(r, lv_color_make(0x22, 0x22, 0x22), LV_PART_MAIN);
    lv_obj_set_style_text_color(r, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(r, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_border_color(r, lv_color_make(0x55, 0x55, 0x55), LV_PART_MAIN);
    lv_obj_set_style_border_width(r, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(r, lv_color_make(0x00, 0x55, 0x33), LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, lv_color_white(), LV_PART_SELECTED);
    lv_obj_align(r, LV_ALIGN_TOP_MID, x_off, 22);
    return r;
}

static void on_manual_time_changed(lv_event_t *e)
{
    bool on = lv_obj_has_state(manual_time_switch, LV_STATE_CHECKED);
    lv_label_set_text(manual_time_val_label, on ? "On" : "Off");
    clock_screen_set_manual_override(on);
    // Hide the editor controls + collapse the gap so the screenshot
    // section below sits flush against the manual-time switch.
    apply_layout();
    settings_save_to_sd();
}

// SET TIME: write the rollers to the RTC and switch the watch into manual mode.
static void on_set_time_clicked(lv_event_t *e)
{
    int year   = MANUAL_YEAR_BASE + (int)lv_roller_get_selected(year_roller);
    int month  = 1 + (int)lv_roller_get_selected(month_roller);
    int day    = 1 + (int)lv_roller_get_selected(day_roller);
    int hour   =     (int)lv_roller_get_selected(hour_roller);
    int minute =     (int)lv_roller_get_selected(minute_roller);

    // Clamp the day to the selected month so e.g. "Feb 31" can't be written.
    int dim = days_in_month(month, year);
    if (day > dim) day = dim;

    clock_screen_apply_manual_time(year, month, day, hour, minute);

    // Setting the time by hand switches the watch into manual mode.
    lv_obj_add_state(manual_time_switch, LV_STATE_CHECKED);
    lv_label_set_text(manual_time_val_label, "On");
    settings_save_to_sd();
}

// ---- "Enable at boot" toggle callbacks ------------------------------------
// Each toggle persists its per-radio opt-in flag to /Settings/boot_radios.txt
// via boot_prefs. These do NOT touch settings.txt, so they intentionally do not
// call settings_save_to_sd().

static void on_boot_wifi_changed(lv_event_t *e)
{
    bool on = lv_obj_has_state(boot_wifi_switch, LV_STATE_CHECKED);
    // WiFi and BLE cannot both auto-enable (they can't run together). If BLE is
    // already the chosen boot radio, refuse and tell the user rather than
    // silently overriding their earlier choice.
    if (on && boot_prefs_get(BOOT_RADIO_BLE)) {
        lv_obj_clear_state(boot_wifi_switch, LV_STATE_CHECKED);
        lv_label_set_text(boot_wifi_val_label, "Off");
        low_mem_show_dialog(
            "#ffcc33 ONE RADIO AT BOOT#\n\n"
            "WiFi and Bluetooth cannot\n"
            "both auto-enable at boot.\n\n"
            "Turn Bluetooth off here\n"
            "first, then enable WiFi.");
        return;
    }
    lv_label_set_text(boot_wifi_val_label, on ? "On" : "Off");
    boot_prefs_set(BOOT_RADIO_WIFI, on);
}

static void on_boot_ble_changed(lv_event_t *e)
{
    bool on = lv_obj_has_state(boot_ble_switch, LV_STATE_CHECKED);
    // Symmetric mutual exclusion with WiFi (see on_boot_wifi_changed).
    if (on && boot_prefs_get(BOOT_RADIO_WIFI)) {
        lv_obj_clear_state(boot_ble_switch, LV_STATE_CHECKED);
        lv_label_set_text(boot_ble_val_label, "Off");
        low_mem_show_dialog(
            "#ffcc33 ONE RADIO AT BOOT#\n\n"
            "WiFi and Bluetooth cannot\n"
            "both auto-enable at boot.\n\n"
            "Turn WiFi off here first,\n"
            "then enable Bluetooth.");
        return;
    }
    lv_label_set_text(boot_ble_val_label, on ? "On" : "Off");
    boot_prefs_set(BOOT_RADIO_BLE, on);
}

static void on_boot_lora_changed(lv_event_t *e)
{
    bool on = lv_obj_has_state(boot_lora_switch, LV_STATE_CHECKED);
    lv_label_set_text(boot_lora_val_label, on ? "On" : "Off");
    boot_prefs_set(BOOT_RADIO_LORA, on);
}

static void on_boot_gps_changed(lv_event_t *e)
{
    bool on = lv_obj_has_state(boot_gps_switch, LV_STATE_CHECKED);
    lv_label_set_text(boot_gps_val_label, on ? "On" : "Off");
    boot_prefs_set(BOOT_RADIO_GPS, on);
}

// Creates one "Enable at boot" row (left label + On/Off state label + toggle),
// styled exactly like the Matrix BG row. The checked state is seeded BEFORE the
// callback is wired so seeding never fires a spurious save. The row is
// registered shiftable so the face / manual-time reflows move it as a unit.
// Returns the switch; the state label is written to *val_out.
static lv_obj_t *make_boot_row(const char *label, bool checked,
                               lv_event_cb_t cb, lv_obj_t **val_out)
{
    lv_obj_t *row = lv_obj_create(settings_screen);
    lv_obj_set_size(row, 380, 40);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_set_style_text_color(lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(lbl, label);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *val = lv_label_create(row);
    lv_obj_set_style_text_color(val, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(val, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(val, checked ? "On" : "Off");
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, -80, 0);

    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 70, 34);
    lv_obj_set_style_bg_color(sw, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(sw, ARGUS_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    // Seed state before wiring the callback so the seed doesn't re-save the
    // value we just read out of boot_prefs.
    if (checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);

    *val_out = val;
    return sw;
}

// Daily<->Defense toggle. Offense is never reachable from here (hidden knock+PIN
// only), so this switch is a plain two-state Daily(off)/Defense(on) control.
static void on_defense_mode_changed(lv_event_t *)
{
    bool on = lv_obj_has_state(defense_switch, LV_STATE_CHECKED);
    lv_label_set_text(defense_val_label, on ? "On" : "Off");
    argus_mode_set(on ? ArgusMode::Defense : ArgusMode::Daily);
}

// "Keep on boot": persist Defense across reboots (default off = always boot Daily).
static void on_defpersist_changed(lv_event_t *)
{
    bool on = lv_obj_has_state(defpersist_switch, LV_STATE_CHECKED);
    lv_label_set_text(defpersist_val_label, on ? "On" : "Off");
    argus_mode_set_defense_persist(on);
}

void settings_screen_create()
{
    settings_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(settings_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(settings_screen, 80, LV_PART_MAIN);
    lv_obj_set_scroll_dir(settings_screen, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(settings_screen, LV_SCROLLBAR_MODE_AUTO);
    // Flex column: rows stack in creation order, centred, ROW_GAP apart.
    lv_obj_set_layout(settings_screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(settings_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(settings_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(settings_screen, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(settings_screen, ROW_GAP, LV_PART_MAIN);

    // Title
    sec(SEC_ALWAYS);
    lv_obj_t *title = lv_label_create(settings_screen);
    s_settings_title = title;   // repainted per mode in settings_screen_show()
    lv_obj_set_style_text_color(title, argus_base_accent(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, theme_title_font(), LV_PART_MAIN);
    lv_label_set_text(title, "SETTINGS");

    // Row: "Brightness" label on the left, percentage on the right
    sec(SEC_DISPLAY);
    lv_obj_t *row = lv_obj_create(settings_screen);
    lv_obj_set_size(row, 380, 30);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_set_style_text_color(lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(lbl, "Brightness");
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

    brightness_val_label = lv_label_create(row);
    lv_obj_set_style_text_color(brightness_val_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(brightness_val_label, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(brightness_val_label, "100%");
    lv_obj_align(brightness_val_label, LV_ALIGN_RIGHT_MID, 0, 0);

    // Horizontal brightness slider
    brightness_slider = lv_slider_create(settings_screen);
    lv_obj_set_size(brightness_slider, 330, 28);
    lv_obj_set_ext_click_area(brightness_slider, 14);   // finger-sized without the oversized knob
    lv_slider_set_range(brightness_slider, 1, DEVICE_MAX_BRIGHTNESS_LEVEL);
    lv_slider_set_value(brightness_slider, s_brightness, LV_ANIM_OFF);

    // Track styling
    lv_obj_set_style_bg_color(brightness_slider, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN);
    lv_obj_set_style_radius(brightness_slider, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(brightness_slider, 0, LV_PART_MAIN);

    // Filled (indicator) portion
    lv_obj_set_style_bg_color(brightness_slider, ARGUS_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(brightness_slider, 8, LV_PART_INDICATOR);

    // Knob: large enough for comfortable touch
    lv_obj_set_style_bg_color(brightness_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_radius(brightness_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(brightness_slider, 4, LV_PART_KNOB);
    lv_obj_set_style_border_width(brightness_slider, 0, LV_PART_KNOB);

    // A horizontal drag on the slider must not also count as a swipe that leaves the page.
    lv_obj_clear_flag(brightness_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(brightness_slider, on_brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(brightness_slider, on_slider_released,    LV_EVENT_RELEASED,      NULL);

    // Separator
    sec(SEC_NEVER);
    lv_obj_t *sep = lv_obj_create(settings_screen);
    lv_obj_set_size(sep, 380, 1);
    lv_obj_set_style_bg_color(sep, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep, 0, LV_PART_MAIN);

    // Facewatch row (top of Settings): opens the Dot-face customization screen.
    // DotOS is the only watch face, so this replaced the old Digital/Analog/Dot
    // selector entirely — the whole row is the tap target.
    sec(SEC_DISPLAY);
    lv_obj_t *face_row = lv_obj_create(settings_screen);
    lv_obj_set_size(face_row, 380, 40);
    lv_obj_set_style_bg_opa(face_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(face_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(face_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(face_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(face_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(face_row, [](lv_event_t *) { face_watch_screen_show(); },
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *face_lbl = lv_label_create(face_row);
    lv_obj_set_style_text_color(face_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(face_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(face_lbl, "Facewatch");
    lv_obj_align(face_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // Right-aligned affordance (plain ASCII to avoid any glyph-tofu risk).
    lv_obj_t *face_hint = lv_label_create(face_row);
    lv_obj_set_style_text_color(face_hint, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(face_hint, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(face_hint, "Customize");
    lv_obj_align(face_hint, LV_ALIGN_RIGHT_MID, 0, 0);

    // "Show Day" and "Show Date" live on the Facewatch Customize screen.

    // Vibrate row
    sec(SEC_SOUND);
    lv_obj_t *vibrate_row = lv_obj_create(settings_screen);
    lv_obj_set_size(vibrate_row, 380, 40);
    lv_obj_set_style_bg_opa(vibrate_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(vibrate_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(vibrate_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(vibrate_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *vibrate_lbl = lv_label_create(vibrate_row);
    lv_obj_set_style_text_color(vibrate_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(vibrate_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(vibrate_lbl, "Notif. vibration");
    lv_obj_align(vibrate_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    vibrate_switch = lv_switch_create(vibrate_row);
    lv_obj_set_size(vibrate_switch, 70, 34);
    lv_obj_set_style_bg_color(vibrate_switch, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(vibrate_switch, ARGUS_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_state(vibrate_switch, LV_STATE_CHECKED);   // notifications vibrate by default
    lv_obj_add_event_cb(vibrate_switch, on_vibrate_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(vibrate_switch, LV_ALIGN_RIGHT_MID, 0, 0);

    // Touch feedback row (UI gestures / button buzz) - separate from notifications.
    lv_obj_t *touch_row = lv_obj_create(settings_screen);
    lv_obj_set_size(touch_row, 380, 40);
    lv_obj_set_style_bg_opa(touch_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(touch_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(touch_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(touch_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *touch_lbl = lv_label_create(touch_row);
    lv_obj_set_style_text_color(touch_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(touch_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(touch_lbl, "Touch feedback");
    lv_obj_align(touch_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    touch_switch = lv_switch_create(touch_row);
    lv_obj_set_size(touch_switch, 70, 34);
    lv_obj_set_style_bg_color(touch_switch, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(touch_switch, ARGUS_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    // default off - UI taps stay silent unless the user turns this on
    lv_obj_add_event_cb(touch_switch, on_touch_feedback_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(touch_switch, LV_ALIGN_RIGHT_MID, 0, 0);

    // Separator before dim timer section
    sec(SEC_NEVER);
    lv_obj_t *sep2 = lv_obj_create(settings_screen);
    lv_obj_set_size(sep2, 380, 1);
    lv_obj_set_style_bg_color(sep2, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep2, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep2, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep2, 0, LV_PART_MAIN);

    // Dim Timer row: label on left, dropdown on right
    sec(SEC_DISPLAY);
    lv_obj_t *dim_row = lv_obj_create(settings_screen);
    lv_obj_set_size(dim_row, 380, 40);
    lv_obj_set_style_bg_opa(dim_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(dim_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dim_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dim_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dim_lbl = lv_label_create(dim_row);
    lv_obj_set_style_text_color(dim_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(dim_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(dim_lbl, "Dim Timer");
    lv_obj_align(dim_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // Dim-timeout SLIDER with a live value label, both fitted in the row's right
    // side so no other row's position shifts. It walks the stops in dim_steps.h
    // (5 s ... 5 min, OFF last) so every notch is the same distance apart.
    dim_timeout_val_label = lv_label_create(dim_row);
    lv_obj_set_style_text_color(dim_timeout_val_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(dim_timeout_val_label, theme_text_font(16), LV_PART_MAIN);
    lv_label_set_text(dim_timeout_val_label, "OFF");
    lv_obj_align(dim_timeout_val_label, LV_ALIGN_RIGHT_MID, 0, 0);

    dim_timeout_slider = lv_slider_create(dim_row);
    lv_obj_set_size(dim_timeout_slider, 150, 14);
    lv_obj_align(dim_timeout_slider, LV_ALIGN_RIGHT_MID, -72, 0);
    lv_slider_set_range(dim_timeout_slider, 0, kDimStepOff);
    lv_slider_set_value(dim_timeout_slider, kDimStepOff, LV_ANIM_OFF);   // OFF until settings load
    lv_obj_set_style_bg_color(dim_timeout_slider, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN);
    lv_obj_set_style_bg_color(dim_timeout_slider, ARGUS_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(dim_timeout_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(dim_timeout_slider, 7, LV_PART_KNOB);    // bigger knob to grab
    lv_obj_set_ext_click_area(dim_timeout_slider, 14);                // finger-sized hit area
    lv_obj_clear_flag(dim_timeout_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(dim_timeout_slider, on_dim_timeout_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(dim_timeout_slider, on_slider_released,      LV_EVENT_RELEASED,      NULL);

    // Dimmed Brightness row: label on left, percentage on right
    lv_obj_t *dbr_row = lv_obj_create(settings_screen);
    lv_obj_set_size(dbr_row, 380, 30);
    lv_obj_set_style_bg_opa(dbr_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(dbr_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dbr_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dbr_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dbr_lbl = lv_label_create(dbr_row);
    lv_obj_set_style_text_color(dbr_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(dbr_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(dbr_lbl, "Dimmed brightness");
    lv_obj_align(dbr_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    dim_brightness_val_label = lv_label_create(dbr_row);
    lv_obj_set_style_text_color(dim_brightness_val_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(dim_brightness_val_label, theme_text_font(20), LV_PART_MAIN);
    int init_pct = (int)s_dim_brightness * 100 / (int)DEVICE_MAX_BRIGHTNESS_LEVEL;
    lv_label_set_text_fmt(dim_brightness_val_label, "%d%%", init_pct);
    lv_obj_align(dim_brightness_val_label, LV_ALIGN_RIGHT_MID, 0, 0);

    // Dimmed Brightness slider
    dim_brightness_slider = lv_slider_create(settings_screen);
    lv_obj_set_size(dim_brightness_slider, 330, 28);
    lv_obj_set_ext_click_area(dim_brightness_slider, 14);
    lv_slider_set_range(dim_brightness_slider, 1, DEVICE_MAX_BRIGHTNESS_LEVEL);
    lv_slider_set_value(dim_brightness_slider, s_dim_brightness, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(dim_brightness_slider, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN);
    lv_obj_set_style_radius(dim_brightness_slider, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(dim_brightness_slider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dim_brightness_slider, ARGUS_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(dim_brightness_slider, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(dim_brightness_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_radius(dim_brightness_slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(dim_brightness_slider, 4, LV_PART_KNOB);
    lv_obj_set_style_border_width(dim_brightness_slider, 0, LV_PART_KNOB);

    lv_obj_clear_flag(dim_brightness_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);   // a drag is not a swipe
    lv_obj_add_event_cb(dim_brightness_slider, on_dim_brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(dim_brightness_slider, on_slider_released,         LV_EVENT_RELEASED,      NULL);

    // Motion-wake row — sits between the Dimmed slider and the Manual Time
    // section. When on, any wrist movement detected by the BHI260AP resets
    // the dim timer (same effect as a tap or button press).
    lv_obj_t *motion_row = lv_obj_create(settings_screen);
    lv_obj_set_size(motion_row, 380, 40);
    lv_obj_set_style_bg_opa(motion_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(motion_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(motion_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(motion_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *motion_lbl = lv_label_create(motion_row);
    lv_obj_set_style_text_color(motion_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(motion_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(motion_lbl, "Raise to wake");
    lv_obj_align(motion_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    motion_wake_val_label = lv_label_create(motion_row);
    lv_obj_set_style_text_color(motion_wake_val_label, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(motion_wake_val_label, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(motion_wake_val_label, "On");
    lv_obj_align(motion_wake_val_label, LV_ALIGN_RIGHT_MID, -80, 0);

    motion_wake_switch = lv_switch_create(motion_row);
    lv_obj_set_size(motion_wake_switch, 70, 34);
    lv_obj_set_style_bg_color(motion_wake_switch, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(motion_wake_switch, ARGUS_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    // Default ON so wrist-raise brightens out of the box — matches the
    // smartwatch behaviour most users expect.
    lv_obj_add_state(motion_wake_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(motion_wake_switch, on_motion_wake_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(motion_wake_switch, LV_ALIGN_RIGHT_MID, 0, 0);

    // ---- Display-power rows ----------------------------------------------------
    //
    // Motion sensitivity: how big a wrist movement "Motion brightens screen"
    // needs, 1 (big, deliberate) to 5 (small). Default 2.
    lv_obj_t *sens_row = lv_obj_create(settings_screen);
    lv_obj_set_size(sens_row, 380, 40);
    lv_obj_set_style_bg_opa(sens_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(sens_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sens_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(sens_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sens_lbl = lv_label_create(sens_row);
    lv_obj_set_style_text_color(sens_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(sens_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(sens_lbl, "Raise sensitivity");
    lv_obj_align(sens_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    motion_sens_slider = lv_slider_create(sens_row);
    lv_obj_set_size(motion_sens_slider, 120, 16);
    lv_slider_set_range(motion_sens_slider, 1, 5);
    lv_slider_set_value(motion_sens_slider, 2, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(motion_sens_slider, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN);
    lv_obj_set_style_bg_color(motion_sens_slider, ARGUS_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(motion_sens_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(motion_sens_slider, 6, LV_PART_KNOB);
    lv_obj_clear_flag(motion_sens_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(motion_sens_slider, on_motion_sens_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(motion_sens_slider, on_slider_released, LV_EVENT_RELEASED, NULL);
    lv_obj_align(motion_sens_slider, LV_ALIGN_RIGHT_MID, -12, 0);

    motion_sens_val_label = lv_label_create(sens_row);
    lv_obj_set_style_text_color(motion_sens_val_label, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(motion_sens_val_label, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(motion_sens_val_label, "2");
    lv_obj_align(motion_sens_val_label, LV_ALIGN_RIGHT_MID, -148, 0);

    // Auto brightness (follows the sun) is a plain On/Off row styled like Motion
    // above. The battery options (auto turn off display, longevity) live in the
    // Battery sub-menu, reached from the "Battery" row further down.
    struct PowerSwitch { lv_obj_t **sw; lv_obj_t **val; const char *text; lv_event_cb_t cb; };
    const PowerSwitch rows[] = {
        { &auto_bright_switch, &auto_bright_val_label, "Auto brightness", on_auto_bright_changed },
    };
    for (const PowerSwitch &r : rows) {
        lv_obj_t *row = lv_obj_create(settings_screen);
        lv_obj_set_size(row, 380, 40);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_obj_set_style_text_color(lbl, ARGUS_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, theme_text_font(20), LV_PART_MAIN);
        lv_label_set_text(lbl, r.text);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        *r.val = lv_label_create(row);
        lv_obj_set_style_text_color(*r.val, ARGUS_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(*r.val, theme_text_font(20), LV_PART_MAIN);
        lv_label_set_text(*r.val, "Off");
        lv_obj_align(*r.val, LV_ALIGN_RIGHT_MID, -80, 0);

        *r.sw = lv_switch_create(row);
        lv_obj_set_size(*r.sw, 70, 34);
        lv_obj_set_style_bg_color(*r.sw, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(*r.sw, ARGUS_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(*r.sw, r.cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_align(*r.sw, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    // Daily step goal (fixed value; feeds the Dot progress bar + Health screen).
    // Slider 1000..30000, snapped to 500 in the handler. Initialised from the
    // health NVS goal restored at boot (default 10000 when unset).
    sec(SEC_SYSTEM);
    lv_obj_t *goal_row = lv_obj_create(settings_screen);
    lv_obj_set_size(goal_row, 380, 40);
    lv_obj_set_style_bg_opa(goal_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(goal_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(goal_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(goal_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *goal_lbl = lv_label_create(goal_row);
    lv_obj_set_style_text_color(goal_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(goal_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(goal_lbl, "Step goal");
    lv_obj_align(goal_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    uint32_t goal_now = health_get_step_goal();
    if (goal_now < 1000) goal_now = 10000;   // sensible default when unset

    step_goal_slider = lv_slider_create(goal_row);
    lv_obj_set_size(step_goal_slider, 150, 16);
    lv_slider_set_range(step_goal_slider, 1000, 30000);
    lv_slider_set_value(step_goal_slider, (int)goal_now, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(step_goal_slider, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN);
    lv_obj_set_style_bg_color(step_goal_slider, ARGUS_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(step_goal_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(step_goal_slider, 6, LV_PART_KNOB);
    lv_obj_clear_flag(step_goal_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(step_goal_slider, on_step_goal_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(step_goal_slider, on_slider_released,    LV_EVENT_RELEASED,      NULL);
    lv_obj_align(step_goal_slider, LV_ALIGN_RIGHT_MID, -12, 0);

    step_goal_val_label = lv_label_create(goal_row);
    lv_obj_set_style_text_color(step_goal_val_label, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(step_goal_val_label, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text_fmt(step_goal_val_label, "%d", (int)goal_now);
    lv_obj_align(step_goal_val_label, LV_ALIGN_RIGHT_MID, -180, 0);

    // Vibration intensity (0..100%): scales all haptics; persisted by the haptic
    // module. Same row style as the sliders above.
    sec(SEC_SOUND);
    lv_obj_t *vib_row = lv_obj_create(settings_screen);
    lv_obj_set_size(vib_row, 380, 40);
    lv_obj_set_style_bg_opa(vib_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(vib_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(vib_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(vib_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *vib_lbl = lv_label_create(vib_row);
    lv_obj_set_style_text_color(vib_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(vib_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(vib_lbl, "Vib. level");
    lv_obj_align(vib_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    int vib_now = (int)haptic_get_intensity();

    vib_intensity_slider = lv_slider_create(vib_row);
    lv_obj_set_size(vib_intensity_slider, 150, 16);
    lv_slider_set_range(vib_intensity_slider, 0, 100);
    lv_slider_set_value(vib_intensity_slider, vib_now, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(vib_intensity_slider, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN);
    lv_obj_set_style_bg_color(vib_intensity_slider, ARGUS_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vib_intensity_slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(vib_intensity_slider, 6, LV_PART_KNOB);
    lv_obj_clear_flag(vib_intensity_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(vib_intensity_slider, on_vib_intensity_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(vib_intensity_slider, on_slider_released,        LV_EVENT_RELEASED,      NULL);
    lv_obj_align(vib_intensity_slider, LV_ALIGN_RIGHT_MID, -12, 0);

    vib_intensity_val_label = lv_label_create(vib_row);
    lv_obj_set_style_text_color(vib_intensity_val_label, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(vib_intensity_val_label, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text_fmt(vib_intensity_val_label, "%d%%", vib_now);
    lv_obj_align(vib_intensity_val_label, LV_ALIGN_RIGHT_MID, -180, 0);

    // Battery: one row that opens the Battery sub-menu, which gathers every
    // battery option (auto turn off display, battery longevity, the automatic
    // low-battery saver) and explains each in plain words. The whole row is the
    // tap target, like the Facewatch row at the top.
    sec(SEC_SYSTEM);
    lv_obj_t *batt_row = lv_obj_create(settings_screen);
    lv_obj_set_size(batt_row, 380, 40);
    lv_obj_set_style_bg_opa(batt_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(batt_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(batt_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(batt_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(batt_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(batt_row, [](lv_event_t *) { battery_screen_show(); },
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *batt_lbl = lv_label_create(batt_row);
    lv_obj_set_style_text_color(batt_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(batt_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(batt_lbl, "Battery");
    lv_obj_align(batt_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // Right-aligned affordance (plain ASCII to avoid any glyph-tofu risk).
    lv_obj_t *batt_hint = lv_label_create(batt_row);
    lv_obj_set_style_text_color(batt_hint, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(batt_hint, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(batt_hint, "Options");
    lv_obj_align(batt_hint, LV_ALIGN_RIGHT_MID, 0, 0);

    // Night time: opens the quiet-hours screen (no vibration, no notification wake
    // between the chosen times; alarms still ring). The right-hand readout says
    // whether it is on, and is refreshed every time Settings opens.
    sec(SEC_DISPLAY);
    lv_obj_t *night_row = lv_obj_create(settings_screen);
    lv_obj_set_size(night_row, 380, 40);
    lv_obj_set_style_bg_opa(night_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(night_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(night_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(night_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(night_row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(night_row, [](lv_event_t *) { night_screen_show(); },
                        LV_EVENT_CLICKED, NULL);

    lv_obj_t *night_lbl = lv_label_create(night_row);
    lv_obj_set_style_text_color(night_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(night_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(night_lbl, "Night time");
    lv_obj_align(night_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    s_night_hint = lv_label_create(night_row);
    lv_obj_set_style_text_color(s_night_hint, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_night_hint, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(s_night_hint, night_mode_enabled() ? "On" : "Off");
    lv_obj_align(s_night_hint, LV_ALIGN_RIGHT_MID, 0, 0);


    // ---- Manual Time section ------------------------------------------------
    // Lets the user set the clock by hand; doing so overrides the automatic
    // GPS time sync until the override switch is turned back off.

    // Separator
    sec(SEC_NEVER);
    lv_obj_t *sep3 = lv_obj_create(settings_screen);
    lv_obj_set_size(sep3, 380, 1);
    lv_obj_set_style_bg_color(sep3, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep3, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep3, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep3, 0, LV_PART_MAIN);

    // Manual Time row: label left, state label + override toggle right
    sec(SEC_TIME);
    lv_obj_t *manual_row = lv_obj_create(settings_screen);
    lv_obj_set_size(manual_row, 380, 40);
    lv_obj_set_style_bg_opa(manual_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(manual_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(manual_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(manual_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *manual_lbl = lv_label_create(manual_row);
    lv_obj_set_style_text_color(manual_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(manual_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(manual_lbl, "Manual Time");
    lv_obj_align(manual_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    manual_time_val_label = lv_label_create(manual_row);
    lv_obj_set_style_text_color(manual_time_val_label, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(manual_time_val_label, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(manual_time_val_label, "Off");
    lv_obj_align(manual_time_val_label, LV_ALIGN_RIGHT_MID, -80, 0);

    manual_time_switch = lv_switch_create(manual_row);
    lv_obj_set_size(manual_time_switch, 70, 34);
    lv_obj_set_style_bg_color(manual_time_switch, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(manual_time_switch, ARGUS_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    // default off — automatic GPS time sync stays in control until a manual set
    lv_obj_add_event_cb(manual_time_switch, on_manual_time_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(manual_time_switch, LV_ALIGN_RIGHT_MID, 0, 0);

    // Instruction line
    lv_obj_t *manual_hint = lv_label_create(settings_screen);
    lv_obj_set_style_text_color(manual_hint, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(manual_hint, theme_text_font(16), LV_PART_MAIN);
    lv_label_set_text(manual_hint, "Set the date & time below, then press SET");
    register_manual_obj(manual_hint);

    // Date / time rollers — Year, Month, Day, Hour, Minute.
    // Buffer sized for the largest list: 41 years × "YYYY\n" ≈ 205 bytes.
    // The five rollers sit side by side in one block, which is a single row of
    // the list (and a single thing to hide when Manual Time is off).
    lv_obj_t *roller_box = lv_obj_create(settings_screen);
    lv_obj_remove_style_all(roller_box);
    lv_obj_set_size(roller_box, 380, 130);
    lv_obj_clear_flag(roller_box, LV_OBJ_FLAG_SCROLLABLE);
    register_manual_obj(roller_box);

    char opts[256];
    build_num_opts(opts, sizeof(opts), MANUAL_YEAR_BASE,
                   MANUAL_YEAR_BASE + MANUAL_YEAR_COUNT - 1, 4);
    year_roller   = make_time_roller(roller_box, "Year", opts, -148, 84);
    build_num_opts(opts, sizeof(opts), 1, 12, 2);
    month_roller  = make_time_roller(roller_box, "Month", opts, -64, 64);
    build_num_opts(opts, sizeof(opts), 1, 31, 2);
    day_roller    = make_time_roller(roller_box, "Day", opts, 10, 64);
    build_num_opts(opts, sizeof(opts), 0, 23, 2);
    hour_roller   = make_time_roller(roller_box, "Hour", opts, 84, 64);
    build_num_opts(opts, sizeof(opts), 0, 59, 2);
    minute_roller = make_time_roller(roller_box, "Min", opts, 158, 64);

    // SET TIME button
    lv_obj_t *set_btn = lv_obj_create(settings_screen);
    lv_obj_set_size(set_btn, 200, 52);
    lv_obj_set_style_radius(set_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(set_btn, lv_color_make(0x00, 0xAA, 0x44), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(set_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(set_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(set_btn, 0, LV_PART_MAIN);
    lv_obj_clear_flag(set_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(set_btn, LV_OBJ_FLAG_CLICKABLE);
    register_manual_obj(set_btn);


    lv_obj_t *set_lbl = lv_label_create(set_btn);
    lv_label_set_text(set_lbl, "SET TIME");
    lv_obj_set_style_text_color(set_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(set_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_obj_center(set_lbl);
    lv_obj_add_event_cb(set_btn, on_set_time_clicked, LV_EVENT_CLICKED, NULL);

    // ---- Screenshot section -------------------------------------------------
    // Long-press anywhere captures the active screen to /Screenshots on SD.
    // The toggle is greyed and forced off whenever the card isn't mounted —
    // settings_screen_apply_sd_state() does that bookkeeping, called both
    // here at boot and from main.cpp whenever the SD state changes.

    // Separator
    sec(SEC_NEVER);
    lv_obj_t *sep_ss = lv_obj_create(settings_screen);
    lv_obj_set_size(sep_ss, 380, 1);
    lv_obj_set_style_bg_color(sep_ss, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep_ss, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep_ss, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep_ss, 0, LV_PART_MAIN);

    sec(SEC_SYSTEM);
    screenshot_row = lv_obj_create(settings_screen);
    lv_obj_set_size(screenshot_row, 380, 40);
    lv_obj_set_style_bg_opa(screenshot_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(screenshot_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screenshot_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screenshot_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ss_lbl = lv_label_create(screenshot_row);
    lv_obj_set_style_text_color(ss_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(ss_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(ss_lbl, "Screenshot long press");
    lv_obj_align(ss_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    screenshot_val_label = lv_label_create(screenshot_row);
    lv_obj_set_style_text_color(screenshot_val_label, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(screenshot_val_label, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(screenshot_val_label, "Off");
    lv_obj_align(screenshot_val_label, LV_ALIGN_RIGHT_MID, -80, 0);

    screenshot_switch = lv_switch_create(screenshot_row);
    lv_obj_set_size(screenshot_switch, 70, 34);
    lv_obj_set_style_bg_color(screenshot_switch, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(screenshot_switch, ARGUS_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(screenshot_switch, on_screenshot_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(screenshot_switch, LV_ALIGN_RIGHT_MID, 0, 0);

    // Hint — explains the 3 s threshold + where the files land.
    screenshot_hint = lv_label_create(settings_screen);
    lv_obj_set_style_text_color(screenshot_hint, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(screenshot_hint, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_align(screenshot_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(screenshot_hint, 360);
    lv_label_set_long_mode(screenshot_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(screenshot_hint,
        "Hold any point for 5 seconds to save a screenshot to "
        "/Screenshots on the SD card. Requires a mounted SD.");

    // ---- Enable at boot section ---------------------------------------------
    // Per-radio opt-in for auto-enabling a radio when the watch powers on. The
    // boot default is OFF for every radio - nothing comes up at boot unless the
    // user turns it on here. State is persisted separately in
    // /Settings/boot_radios.txt via boot_prefs (see boot_prefs.h), not in
    // settings.txt. WiFi and Bluetooth are mutually exclusive here (they cannot
    // run together), so enabling one at boot blocks the other with a warning.
    sec(SEC_NEVER);
    lv_obj_t *sep_boot = lv_obj_create(settings_screen);
    lv_obj_set_size(sep_boot, 380, 1);
    lv_obj_set_style_bg_color(sep_boot, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep_boot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep_boot, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep_boot, 0, LV_PART_MAIN);

    // Section header
    sec(SEC_CONN);   // keep the "Enable at boot" sub-heading: these toggles are about boot, not now
    lv_obj_t *boot_hdr_row = lv_obj_create(settings_screen);
    lv_obj_set_size(boot_hdr_row, 380, 30);
    lv_obj_set_style_bg_opa(boot_hdr_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(boot_hdr_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(boot_hdr_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(boot_hdr_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *boot_hdr = lv_label_create(boot_hdr_row);
    lv_obj_set_style_text_color(boot_hdr, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(boot_hdr, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(boot_hdr, "Enable at boot");
    lv_obj_align(boot_hdr, LV_ALIGN_LEFT_MID, 0, 0);

    // One toggle per boot-enableable radio. Each seeds from boot_prefs. WiFi and
    // Bluetooth are mutually exclusive (enforced in their callbacks) since the
    // two cannot run together on this board. Rows are evenly spaced 48px apart.
    // Y values follow the −130 reflow after the Wallpaper section was removed, so
    // these rows sit right under the "Enable at boot" header (1264) instead of
    // leaving a dead gap with the hint overlapping them.
    sec(SEC_CONN);
    boot_wifi_switch = make_boot_row("WiFi", boot_prefs_get(BOOT_RADIO_WIFI),
                                     on_boot_wifi_changed, &boot_wifi_val_label);
    boot_ble_switch  = make_boot_row("Bluetooth", boot_prefs_get(BOOT_RADIO_BLE),
                                     on_boot_ble_changed,  &boot_ble_val_label);
    boot_lora_switch = make_boot_row("LoRa", boot_prefs_get(BOOT_RADIO_LORA),
                                     on_boot_lora_changed, &boot_lora_val_label);
    boot_gps_switch  = make_boot_row("GPS", boot_prefs_get(BOOT_RADIO_GPS),
                                     on_boot_gps_changed,  &boot_gps_val_label);

    // Hint: clarifies the boot-time behaviour and why BT isn't listed.
    lv_obj_t *boot_hint = lv_label_create(settings_screen);
    lv_obj_set_style_text_color(boot_hint, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(boot_hint, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_align(boot_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(boot_hint, 360);
    lv_label_set_long_mode(boot_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(boot_hint,
        "Radios turned on here power up automatically at boot. Everything is "
        "off by default. WiFi and Bluetooth cannot both auto-enable.");

    // --- System Info (read-only diagnostics; refreshed on screen show) --------
    sec(SEC_NEVER);
    lv_obj_t *sep_sys = lv_obj_create(settings_screen);
    lv_obj_set_size(sep_sys, 380, 1);
    lv_obj_set_style_bg_color(sep_sys, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep_sys, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep_sys, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep_sys, 0, LV_PART_MAIN);

    sec(SEC_SYSTEM);
    lv_obj_t *sys_hdr = lv_label_create(settings_screen);
    lv_obj_set_style_text_color(sys_hdr, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(sys_hdr, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(sys_hdr, "System Info");

    sysinfo_label = lv_label_create(settings_screen);
    lv_obj_set_style_text_color(sysinfo_label, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(sysinfo_label, &font_argus_mono_16, LV_PART_MAIN);
    lv_obj_set_width(sysinfo_label, 360);
    lv_label_set_text(sysinfo_label, "");

    // --- Mode (ARGUS Daily / Defense) ----------------------------------------
    // Appended at the bottom (safe insertion into the fixed-Y layout). Offense is
    // NOT here - it is reached only via the hidden BOOT-button knock + PIN (P4/P5).
    sec(SEC_NEVER);
    lv_obj_t *sep_mode = lv_obj_create(settings_screen);
    lv_obj_set_size(sep_mode, 380, 1);
    lv_obj_set_style_bg_color(sep_mode, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep_mode, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep_mode, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep_mode, 0, LV_PART_MAIN);

    lv_obj_t *mode_hdr = lv_label_create(settings_screen);
    lv_obj_set_style_text_color(mode_hdr, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(mode_hdr, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(mode_hdr, "Mode");

    // −130 reflow (see boot rows above): sit under the "Mode" header (1784) with
    // the hint (1920) below, not overlapping it.
    sec(SEC_SECURITY);
    defense_switch = make_boot_row("Defense", argus_mode_defense_selected(),
        on_defense_mode_changed, &defense_val_label);

    defpersist_switch = make_boot_row("Keep Defense on boot", argus_mode_defense_persist(),
        on_defpersist_changed, &defpersist_val_label);

    lv_obj_t *mode_hint = lv_label_create(settings_screen);
    lv_obj_set_style_text_color(mode_hint, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(mode_hint, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_width(mode_hint, 360);
    lv_label_set_text(mode_hint, "Defense shows the anti-surveillance detectors. Daily hides them for an innocent look.");

    // ONE Offense button. Outside Offense it is the TEMPORARY test entry to the PIN
    // pad (the real entry is the power-key knock; keep this until the knock is
    // verified on-wrist, or a watch with a non-firing knock has no way in short of a
    // reflash). Inside Offense it becomes "Exit Offense". Its label + action are set
    // by mode in settings_screen_show(); a single button avoids the overlapping-hit
    // bug two stacked buttons caused (the unlock click did nothing).
    s_ofs_btn = lv_button_create(settings_screen);
    lv_obj_set_size(s_ofs_btn, 380, 64);   // taller = easier tap target (was 44)
    lv_obj_set_style_bg_color(s_ofs_btn, lv_color_make(0x2A, 0x1E, 0x10), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ofs_btn, ARGUS_OFFENSE_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ofs_btn, 1, LV_PART_MAIN);
    s_ofs_lbl = lv_label_create(s_ofs_btn);
    lv_obj_set_style_text_font(s_ofs_lbl, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ofs_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_label_set_text(s_ofs_lbl, "Unlock Offense (test)");
    lv_obj_center(s_ofs_lbl);
    // "Clear detection logs" - wipes every record ARGUS has kept about other
    // people's devices. Retention already expires these automatically (see
    // src/detect/log_retention.h), but a one-tap purge is the difference
    // between a policy the wearer has to trust and one they can enforce.
    lv_obj_t *det_btn = lv_button_create(settings_screen);
    lv_obj_set_size(det_btn, 380, 64);
    lv_obj_set_style_bg_color(det_btn, lv_color_make(0x1E, 0x1E, 0x1E), LV_PART_MAIN);
    lv_obj_set_style_border_color(det_btn, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_border_width(det_btn, 1, LV_PART_MAIN);
    lv_obj_t *det_lbl = lv_label_create(det_btn);
    lv_obj_set_style_text_font(det_lbl, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(det_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_label_set_text(det_lbl, "Clear detection logs");
    lv_obj_center(det_lbl);
    // Destructive, so it takes two taps: the first arms the button (label asks
    // for confirmation), a second within 4 s erases. Anything else disarms it.
    lv_obj_add_event_cb(det_btn, [](lv_event_t *e) {
        static uint32_t armed_at = 0;
        lv_obj_t *lbl = lv_obj_get_child((lv_obj_t *)lv_event_get_target(e), 0);
        uint32_t now = millis();
        if (armed_at == 0 || now - armed_at > 4000) {
            armed_at = now ? now : 1;
            if (lbl) { lv_label_set_text(lbl, "Tap again to erase logs"); lv_obj_center(lbl); }
            return;
        }
        armed_at = 0;
        if (lbl) { lv_label_set_text(lbl, "Clear detection logs"); lv_obj_center(lbl); }
        int n = detect_log_clear_all();
        char msg[96];
        snprintf(msg, sizeof(msg),
                 n > 0 ? "Detection logs cleared.\n%d file(s) removed."
                       : "Nothing to clear.\n(%d file(s) found)", n);
        low_mem_show_dialog(msg);
        settings_update_sysinfo();
    }, LV_EVENT_CLICKED, NULL);

    // "USB SD card reader" - a SECOND route to the same screen the Tools grid
    // reaches, and the only one that works in Daily.
    //
    // The Tools grid is hidden in Daily on purpose: Daily is the innocent-watch
    // mode and the whole point is that the grid is not there. But that also put
    // the card reader out of reach, so pulling logs off the watch required
    // switching to Defense first - which is a mode change the wearer may not
    // want to make just to copy a file, and it is not a capability worth
    // hiding: mounting a card as a USB drive is what any ordinary device does.
    // Settings is reachable in every mode (BOOT press from the clock face), so
    // the utility lives here and the disguise stays intact.
    //
    // Notify and LoRa APRS sit in the same neutral class and are still
    // launcher-only; APRS at least is an RF transmitter, so whether it belongs
    // in Daily is a real question rather than an oversight (the launcher's
    // category gating in apps_screen.cpp now owns that classification).
    // CLOCK TRUST. The watch used to present whatever the RTC held with exactly
    // as much confidence as a fresh GPS lock, which is how a Pacific-time RTC
    // rode home from DEF CON and silently misdated weeks of SD log stamps. This
    // row says when the clock was last set from a trusted source, and turns red
    // once nothing has vouched for it in kStaleDays. See clock_sync.h.
    // GEOMETRY, and it is load-bearing. This row is TWO LINES at width 360 for
    // every string it can hold (measured against Orbitron 14: the never-synced,
    // synced, stale and future-stamp variants all wrap to exactly 2 lines,
    // about 36 px). It first shipped at y=2250, which put it INSIDE the
    // "Clear detection logs" button above it - that button is at 2190 and 64 px
    // tall, so it occupies 2190..2254 - and the row rendered across the button's
    // lower edge, read as part of the chrome, and was reported as missing
    // entirely. So: leave clearance for two lines, and keep the button below far
    // enough down. 2264 + 36 = 2300, and usbsd_btn starts at 2320.
    sec(SEC_TIME);
    s_sync_lbl = lv_label_create(settings_screen);
    lv_obj_set_style_text_font(s_sync_lbl, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_width(s_sync_lbl, 360);
    // Explicit, unlike the neighbouring mode_hint: the clearance above depends
    // on this label WRAPPING to a known height rather than clipping, so do not
    // leave it to the default.
    lv_label_set_long_mode(s_sync_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_sync_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    settings_update_clock_sync();

    sec(SEC_CONN);
    lv_obj_t *usbsd_btn = lv_button_create(settings_screen);
    lv_obj_set_size(usbsd_btn, 380, 64);
    lv_obj_set_style_bg_color(usbsd_btn, lv_color_make(0x1E, 0x1E, 0x1E), LV_PART_MAIN);
    lv_obj_set_style_border_color(usbsd_btn, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_border_width(usbsd_btn, 1, LV_PART_MAIN);
    lv_obj_t *usbsd_lbl = lv_label_create(usbsd_btn);
    lv_obj_set_style_text_font(usbsd_lbl, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(usbsd_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_label_set_text(usbsd_lbl, "USB SD card reader");
    lv_obj_center(usbsd_lbl);
    lv_obj_add_event_cb(usbsd_btn, [](lv_event_t *) { usb_sd_screen_show(); },
                        LV_EVENT_CLICKED, NULL);

    // (Facewatch now lives at the TOP of Settings, repurposed from the old
    // "Watch Face" row — see face_row above. The duplicate button that used to
    // sit here was removed.)

    lv_obj_add_event_cb(s_ofs_btn, [](lv_event_t *) {
        if (argus_mode_current() == ArgusMode::Offense) { lock_offense(); clock_screen_show(); }
        else                                            { pin_pad_screen_show(); }
    }, LV_EVENT_CLICKED, NULL);

    // Every row exists: group them into sections, then show only what the open
    // section and each row's own condition allow. The flex column does the rest.
    finish_sections();
    apply_layout();

    // Reflect the current SD state in the row's interactability + checked
    // state. Boot order matters here — instance.isCardReady() may flip
    // later, but settings_screen_apply_sd_state() is also called from
    // main.cpp on every SD-state edge so the row stays in sync.
    settings_screen_apply_sd_state();
}

bool settings_screen_is_active()
{
    return lv_screen_active() == settings_screen;
}

void settings_screen_show()
{
    main_loop_request_lvgl_priority(12);
    settings_update_sysinfo();   // snapshot heap/PSRAM/battery/uptime
    settings_update_clock_sync();// the answer ages on its own; recompute per show
    if (s_night_hint) lv_label_set_text(s_night_hint, night_mode_enabled() ? "On" : "Off");

    // Heading follows the mode: red-team red in Offense, steel-blue otherwise.
    if (s_settings_title)
        lv_obj_set_style_text_color(s_settings_title, argus_base_accent(), LV_PART_MAIN);

    // Keep the Defense toggle showing the Offense EXIT target - it can be flipped
    // while inside Offense to choose whether Exit Offense lands in Defense or Daily.
    if (defense_switch) {
        bool ds = argus_mode_defense_selected();
        if (ds) lv_obj_add_state(defense_switch, LV_STATE_CHECKED);
        else    lv_obj_clear_state(defense_switch, LV_STATE_CHECKED);
        if (defense_val_label) lv_label_set_text(defense_val_label, ds ? "On" : "Off");
    }

    // Offense button label follows the mode: exit when in Offense, enter otherwise.
    if (s_ofs_lbl) {
        lv_label_set_text(s_ofs_lbl,
            argus_mode_current() == ArgusMode::Offense ? "Exit Offense" : "Unlock Offense (test)");
        lv_obj_center(s_ofs_lbl);
    }
    // Refresh percentage label to match current slider position
    int pct = (int)s_brightness * 100 / (int)DEVICE_MAX_BRIGHTNESS_LEVEL;
    lv_label_set_text_fmt(brightness_val_label, "%d%%", pct);

    // Pre-fill the manual-time rollers with the current local time so the user
    // adjusts from "now" rather than from an arbitrary default.
    struct tm now;
    clock_screen_get_local_time(&now);
    int yr_idx  = now.tm_year + 1900 - MANUAL_YEAR_BASE;
    if (yr_idx < 0)                     yr_idx = 0;
    if (yr_idx > MANUAL_YEAR_COUNT - 1) yr_idx = MANUAL_YEAR_COUNT - 1;
    int day_idx = now.tm_mday - 1;
    if (day_idx < 0)  day_idx = 0;
    if (day_idx > 30) day_idx = 30;
    lv_roller_set_selected(year_roller,   (uint32_t)yr_idx,        LV_ANIM_OFF);
    lv_roller_set_selected(month_roller,  (uint32_t)now.tm_mon,    LV_ANIM_OFF);
    lv_roller_set_selected(day_roller,    (uint32_t)day_idx,       LV_ANIM_OFF);
    lv_roller_set_selected(hour_roller,   (uint32_t)now.tm_hour,   LV_ANIM_OFF);
    lv_roller_set_selected(minute_roller, (uint32_t)now.tm_min,    LV_ANIM_OFF);

    show_auto_bright_state(lv_obj_has_state(auto_bright_switch, LV_STATE_CHECKED));
    lv_scr_load(settings_screen);
}

// Rewrite /Settings/settings.txt with the current widget states. Called from
// every on_*_changed callback. No-op if no SD is mounted or if a load is in
// progress (so applying loaded values doesn't write the file we just read).
static void settings_save_to_sd()
{
    if (g_loading) return;
    if (!instance.isCardReady()) return;
    if (usb_sd_is_running()) return;   // host owns the SD card while mounted

    if (!SD.exists("/Settings")) SD.mkdir("/Settings");

    File f = SD.open(SETTINGS_PATH, FILE_WRITE);   // FILE_WRITE = "w" (truncate)
    if (!f) return;

    f.printf("brightness=%d\n",      (int)s_brightness);
    f.printf("clock_face=%d\n",      settings_face_mode());
    // 12h/24h is chosen on the Facewatch screen; save what the clock really uses
    // (this used to save a hidden switch, so the Facewatch choice was lost on reboot).
    f.printf("format_12h=%d\n",      clock_screen_get_12h() ? 1 : 0);
    f.printf("show_ampm=%d\n",       s_show_ampm ? 1 : 0);
    f.printf("show_secs=%d\n",       s_show_secs ? 1 : 0);
    f.printf("show_day=%d\n",        clock_screen_get_show_day()  ? 1 : 0);
    f.printf("show_date=%d\n",       clock_screen_get_show_date() ? 1 : 0);
    f.printf("notif_vibrate=%d\n",   lv_obj_has_state(vibrate_switch,     LV_STATE_CHECKED) ? 1 : 0);
    f.printf("vibrate=%d\n",         lv_obj_has_state(touch_switch,       LV_STATE_CHECKED) ? 1 : 0);
    f.printf("dim_timeout_sec=%lu\n",(unsigned long)dim_step_seconds((int)lv_slider_get_value(dim_timeout_slider)));
    f.printf("dim_brightness=%d\n",  (int)s_dim_brightness);
    f.printf("motion_wake=%d\n",     lv_obj_has_state(motion_wake_switch, LV_STATE_CHECKED) ? 1 : 0);
    f.printf("motion_sens=%d\n",     (int)lv_slider_get_value(motion_sens_slider));
    f.printf("auto_bright=%d\n",     lv_obj_has_state(auto_bright_switch, LV_STATE_CHECKED) ? 1 : 0);
    f.printf("batt_saver=%d\n",      s_auto_off_display ? 1 : 0);   // key kept for saved files
    f.printf("manual_time=%d\n",     lv_obj_has_state(manual_time_switch, LV_STATE_CHECKED) ? 1 : 0);
    f.printf("screenshot=%d\n",      lv_obj_has_state(screenshot_switch,  LV_STATE_CHECKED) ? 1 : 0);
    f.printf("step_goal=%lu\n",      (unsigned long)health_get_step_goal());
    f.printf("vib_intensity=%d\n",   (int)haptic_get_intensity());
    f.close();
}

// Helpers for the load() switch/dropdown application
static void apply_switch(lv_obj_t *sw, bool checked)
{
    if (checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    else         lv_obj_clear_state(sw, LV_STATE_CHECKED);
}

void settings_screen_load()
{
    if (!instance.isCardReady()) return;
    if (!SD.exists(SETTINGS_PATH)) return;

    File f = SD.open(SETTINGS_PATH, FILE_READ);
    if (!f) return;

    g_loading = true;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        int eq = line.indexOf('=');
        if (eq <= 0) continue;
        String key = line.substring(0, eq);
        long   v   = line.substring(eq + 1).toInt();
        bool   b   = (v != 0);

        if (key == "brightness") {
            if (v < 1) v = 1;
            if (v > DEVICE_MAX_BRIGHTNESS_LEVEL) v = DEVICE_MAX_BRIGHTNESS_LEVEL;
            s_brightness = (int32_t)v;
            lv_slider_set_value(brightness_slider, v, LV_ANIM_OFF);
            instance.setBrightness((uint8_t)v);
            int pct = (int)v * 100 / (int)DEVICE_MAX_BRIGHTNESS_LEVEL;
            lv_label_set_text_fmt(brightness_val_label, "%d%%", pct);
        } else if (key == "clock_face" || key == "analog_face") {
            // Legacy face keys (the old Digital/Analog/Dot selector). DotOS is
            // the only watch face now, so the stored choice is ignored: keep the
            // Dot face and its collapsed layout regardless of what the card says.
            (void)v; (void)b;
            apply_layout();
            clock_screen_set_face(2 /* FACE_DOT */);
        } else if (key == "format_12h") {
            clock_screen_set_12h(b);
        } else if (key == "show_ampm") {
            s_show_ampm = b;
            clock_screen_set_show_ampm(b);
        } else if (key == "show_secs") {
            s_show_secs = b;
            clock_screen_set_show_secs(b);
        } else if (key == "show_day") {
            clock_screen_set_show_day(b);   // UI lives on the Facewatch screen now
        } else if (key == "show_date") {
            clock_screen_set_show_date(b);
        } else if (key == "vibrate") {           // legacy + new "Touch feedback"
            apply_switch(touch_switch, b);
            clock_screen_set_vibrate(b);
        } else if (key == "notif_vibrate") {     // "Vibration" (notifications)
            apply_switch(vibrate_switch, b);
            notify::set_notif_vibrate(b);
        } else if (key == "dim_timeout_sec") {
            if (v < 0)   v = 0;
            if (v > 300) v = 300;
            // Older files hold any 0..300 s: snap to the nearest stop of the slider.
            const int idx = dim_step_index((uint32_t)v);
            lv_slider_set_value(dim_timeout_slider, idx, LV_ANIM_OFF);
            dim_timeout_show(dim_step_seconds(idx));
            clock_screen_set_dim_timeout(dim_step_seconds(idx) * 1000);
        } else if (key == "dim_brightness") {
            if (v < 1) v = 1;
            if (v > DEVICE_MAX_BRIGHTNESS_LEVEL) v = DEVICE_MAX_BRIGHTNESS_LEVEL;
            s_dim_brightness = (int32_t)v;
            lv_slider_set_value(dim_brightness_slider, v, LV_ANIM_OFF);
            clock_screen_set_dim_brightness((uint8_t)v);
            int pct = (int)v * 100 / (int)DEVICE_MAX_BRIGHTNESS_LEVEL;
            lv_label_set_text_fmt(dim_brightness_val_label, "%d%%", pct);
        } else if (key == "motion_sens") {
            int level = (v < 1) ? 1 : (v > 5 ? 5 : (int)v);
            lv_slider_set_value(motion_sens_slider, level, LV_ANIM_OFF);
            lv_label_set_text_fmt(motion_sens_val_label, "%d", level);
            clock_screen_set_motion_sensitivity(level);
        } else if (key == "auto_bright") {
            apply_switch(auto_bright_switch, b);
            clock_screen_set_auto_brightness(b);
            show_auto_bright_state(b);
        } else if (key == "batt_saver") {
            s_auto_off_display = b;
            clock_screen_set_battery_saver(b);
        } else if (key == "motion_wake") {
            apply_switch(motion_wake_switch, b);
            lv_label_set_text(motion_wake_val_label, b ? "On" : "Off");
            clock_screen_set_motion_wake(b);
        } else if (key == "manual_time") {
            apply_switch(manual_time_switch, b);
            lv_label_set_text(manual_time_val_label, b ? "On" : "Off");
            clock_screen_set_manual_override(b);
            // Match the editor visibility + screenshot-section position
            // to the loaded switch state.
            apply_layout();
        } else if (key == "screenshot") {
            apply_switch(screenshot_switch, b);
            lv_label_set_text(screenshot_val_label, b ? "On" : "Off");
        } else if (key == "step_goal") {
            if (v < 1000)  v = 1000;
            if (v > 30000) v = 30000;
            s_sd_step_goal = (int)v;   // applied late (after health_boot_restore)
            if (step_goal_slider) {
                lv_slider_set_value(step_goal_slider, (int)v, LV_ANIM_OFF);
                lv_label_set_text_fmt(step_goal_val_label, "%ld", v);
            }
        } else if (key == "vib_intensity") {
            if (v < 0)   v = 0;
            if (v > 100) v = 100;
            s_sd_vib = (int)v;         // applied late (after haptic_boot_restore)
            if (vib_intensity_slider) {
                lv_slider_set_value(vib_intensity_slider, (int)v, LV_ANIM_OFF);
                lv_label_set_text_fmt(vib_intensity_val_label, "%ld%%", v);
            }
        }
    }
    // Re-evaluate SD-aware state once loading is done — a persisted
    // "screenshot=1" gets immediately forced back off if the card is
    // unavailable at load time, so the UI never claims it's on when the
    // capture would silently fail.
    settings_screen_apply_sd_state();

    f.close();
    g_loading = false;
}

// Re-apply the SD-persisted step goal and vibration intensity. MUST be called
// AFTER health_boot_restore() and haptic_boot_restore() so the SD value (which
// survives a flash) overrides the NVS value (which a flash erases). No-op when
// the SD file carried no such key.
void settings_apply_persisted_late()
{
    if (s_sd_step_goal >= 0) health_set_step_goal((uint32_t)s_sd_step_goal);
    if (s_sd_vib       >= 0) haptic_set_intensity((uint8_t)s_sd_vib);
}

// ---- Screenshot toggle: public API ----------------------------------------

bool settings_get_screenshot_long_press()
{
    if (!screenshot_switch) return false;
    if (!lv_obj_has_state(screenshot_switch, LV_STATE_CHECKED)) return false;
    // Belt-and-braces — apply_sd_state should already have cleared the
    // switch when the card vanished, but never claim the feature is on
    // if we can't actually write a file right now.
    if (usb_sd_is_running() || !instance.isCardReady()) return false;
    return true;
}

void settings_screen_apply_sd_state()
{
    if (!screenshot_switch) return;

    bool sd_usable = instance.isCardReady() && !usb_sd_is_running();

    if (sd_usable) {
        // Re-enable interaction; don't touch the checked state — the user
        // (or settings load) owns that.
        lv_obj_clear_state(screenshot_switch, LV_STATE_DISABLED);
        lv_obj_clear_flag(screenshot_row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(screenshot_val_label,
            ARGUS_TEXT, LV_PART_MAIN);
    } else {
        // Force the toggle off and grey it — set g_loading around the
        // visual change so the value-changed callback doesn't write the
        // forced-off state back to the SD file (and overwrite the user's
        // saved preference when the card eventually comes back).
        bool was_loading = g_loading;
        g_loading = true;
        if (lv_obj_has_state(screenshot_switch, LV_STATE_CHECKED))
            lv_obj_clear_state(screenshot_switch, LV_STATE_CHECKED);
        lv_label_set_text(screenshot_val_label, "Off");
        g_loading = was_loading;

        lv_obj_add_state(screenshot_switch, LV_STATE_DISABLED);
        lv_obj_set_style_text_color(screenshot_val_label,
            ARGUS_TEXT_DIM, LV_PART_MAIN);
    }
}
