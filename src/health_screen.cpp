// health_screen.cpp - see health_screen.h.
//
// A calm, glanceable readout in the Dot palette: white value when fresh, gray
// when stale or absent, a red accent rule under the title. No radios or logic
// here - it only reads health_state and paints.
#include "health_screen.h"
#include "health_state.h"
#include "theme.h"

#include <lvgl.h>
#include <Arduino.h>   // millis()
#include <cstdio>

// main.cpp helpers.
void clock_screen_show();
bool touch_started_at_top_edge();

namespace {

// Dot palette (kept local; the Dot helpers live in main.cpp).
inline lv_color_t c_white() { return lv_color_hex(0xFFFFFF); }
inline lv_color_t c_gray()  { return lv_color_hex(0x5C5C5C); }
inline lv_color_t c_dim()   { return lv_color_hex(0x9A9A9A); }
inline lv_color_t c_red()   { return lv_color_hex(0xE02020); }

lv_obj_t *s_screen     = nullptr;
lv_obj_t *s_sleep_val  = nullptr;
lv_obj_t *s_steps_val  = nullptr;
lv_obj_t *s_stress_val = nullptr;
lv_obj_t *s_hr_val     = nullptr;
lv_obj_t *s_sync_lbl   = nullptr;   // "last sync" age under the title

// Swipe-up refresh overlays + state. The watch cannot pull from the phone, so a
// "refresh" waits for the companion app to push a fresh packet: it shows a
// loading spinner and watches health_rx_seq() for an increment. If none arrives
// before the timeout, it shows a dismissible error card.
lv_obj_t   *s_load_ov       = nullptr;   // loading backdrop + spinner
lv_obj_t   *s_load_card     = nullptr;   // spinner's parent card
lv_obj_t   *s_spinner       = nullptr;   // created only during a refresh
lv_obj_t   *s_err_ov        = nullptr;   // error card (X to close)
lv_timer_t *s_refresh_timer = nullptr;
uint32_t    s_refresh_start = 0;
uint32_t    s_refresh_seq0  = 0;

constexpr uint32_t REFRESH_TIMEOUT_MS = 8000;   // wait this long for a push
constexpr uint32_t REFRESH_POLL_MS    = 150;    // spinner/seq poll cadence

void cancel_refresh()
{
    if (s_refresh_timer) { lv_timer_delete(s_refresh_timer); s_refresh_timer = nullptr; }
    // Delete the spinner so its animation stops (a persistent spinner would keep
    // ticking forever, even hidden, and keep the loop from idling).
    if (s_spinner) { lv_obj_delete(s_spinner); s_spinner = nullptr; }
    if (s_load_ov) lv_obj_add_flag(s_load_ov, LV_OBJ_FLAG_HIDDEN);
}

void refresh_timer_cb(lv_timer_t *)
{
    // A fresh packet arrived: dismiss the spinner and repaint the values.
    if (health_rx_seq() != s_refresh_seq0) {
        cancel_refresh();
        health_screen_update();
        return;
    }
    // Timed out with nothing new: show the error card.
    if (millis() - s_refresh_start >= REFRESH_TIMEOUT_MS) {
        cancel_refresh();
        if (s_err_ov) lv_obj_clear_flag(s_err_ov, LV_OBJ_FLAG_HIDDEN);
    }
}

void start_refresh()
{
    if (!s_screen || s_refresh_timer) return;   // already refreshing
    if (s_err_ov)  lv_obj_add_flag(s_err_ov, LV_OBJ_FLAG_HIDDEN);

    // Spawn the spinner fresh (deleted again in cancel_refresh).
    if (s_load_card && !s_spinner) {
        s_spinner = lv_spinner_create(s_load_card);
        lv_spinner_set_anim_params(s_spinner, 1000, 60);
        lv_obj_set_size(s_spinner, 66, 66);
        lv_obj_align(s_spinner, LV_ALIGN_TOP_MID, 0, 16);
        lv_obj_set_style_arc_color(s_spinner, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
        lv_obj_set_style_arc_color(s_spinner, c_red(), LV_PART_INDICATOR);
        lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_CLICKABLE);
    }
    if (s_load_ov) lv_obj_clear_flag(s_load_ov, LV_OBJ_FLAG_HIDDEN);
    s_refresh_seq0  = health_rx_seq();
    s_refresh_start = millis();
    s_refresh_timer = lv_timer_create(refresh_timer_cb, REFRESH_POLL_MS, nullptr);
}

void err_close_cb(lv_event_t *)
{
    if (s_err_ov) lv_obj_add_flag(s_err_ov, LV_OBJ_FLAG_HIDDEN);
}

void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    // A pull from the very top edge opens the notification shade (handled in
    // main.cpp); any other swipe down goes home.
    if (dir == LV_DIR_BOTTOM && !touch_started_at_top_edge()) {
        cancel_refresh();
        clock_screen_show();
    }
    // Swipe up: ask for the latest. The watch can only wait for the companion
    // app to push; show a spinner and resolve to the fresh values or an error.
    else if (dir == LV_DIR_TOP)
        start_refresh();
}

// One metric row: a small gray name on the left, a big value on the right.
// Returns the value label so update() can drive it.
lv_obj_t *make_row(int y, const char *name)
{
    lv_obj_t *row = lv_obj_create(s_screen);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 350, 70);
    lv_obj_set_pos(row, 30, y);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, &font_argus_label_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, c_dim(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(lbl, 2, LV_PART_MAIN);
    lv_label_set_text(lbl, name);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 6);

    lv_obj_t *val = lv_label_create(row);
    lv_obj_set_style_text_font(val, &font_argus_label_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(val, c_gray(), LV_PART_MAIN);
    lv_label_set_text(val, "--");
    lv_obj_align(val, LV_ALIGN_BOTTOM_LEFT, 0, -2);

    // Thin gray separator under the row.
    lv_obj_t *sep = lv_obj_create(row);
    lv_obj_remove_style_all(sep);
    lv_obj_set_size(sep, 350, 1);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(sep, LV_ALIGN_BOTTOM_MID, 0, 0);

    return val;
}

// Set a value label's text + freshness color: white when fresh, gray when the
// value is stale or absent.
void set_val(lv_obj_t *val, bool present, bool stale, const char *text)
{
    lv_label_set_text(val, present ? text : "--");
    lv_obj_set_style_text_color(val, (present && !stale) ? c_white() : c_gray(),
                                LV_PART_MAIN);
}

// A full-screen dim backdrop that lets gestures pass to the screen underneath
// (so swipe-down-home still works while it is up). Returns it, hidden.
lv_obj_t *make_backdrop()
{
    lv_obj_t *ov = lv_obj_create(s_screen);
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, 410, 502);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ov, LV_OPA_70, LV_PART_MAIN);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_CLICKABLE);   // touches fall through
    lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
    return ov;
}

// Builds the loading spinner and the error card, both hidden. Called once.
void build_overlays()
{
    // ---- Loading: spinner + "REFRESHING" on a small card ----
    s_load_ov = make_backdrop();
    lv_obj_t *lcard = lv_obj_create(s_load_ov);
    lv_obj_remove_style_all(lcard);
    lv_obj_set_size(lcard, 190, 160);
    lv_obj_center(lcard);
    lv_obj_set_style_bg_color(lcard, lv_color_hex(0x121212), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lcard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(lcard, 14, LV_PART_MAIN);
    lv_obj_set_style_border_color(lcard, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_border_width(lcard, 1, LV_PART_MAIN);
    lv_obj_clear_flag(lcard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(lcard, LV_OBJ_FLAG_CLICKABLE);
    s_load_card = lcard;   // start_refresh() spawns the spinner in here

    lv_obj_t *ll = lv_label_create(lcard);
    lv_obj_set_style_text_font(ll, &font_argus_label_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(ll, c_dim(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(ll, 2, LV_PART_MAIN);
    lv_label_set_text(ll, "REFRESHING");
    lv_obj_align(ll, LV_ALIGN_BOTTOM_MID, 0, -18);

    // ---- Error card: title + detail + an X button to close ----
    s_err_ov = make_backdrop();
    lv_obj_t *ecard = lv_obj_create(s_err_ov);
    lv_obj_remove_style_all(ecard);
    lv_obj_set_size(ecard, 250, 176);
    lv_obj_center(ecard);
    lv_obj_set_style_bg_color(ecard, lv_color_hex(0x160B0B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ecard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(ecard, 14, LV_PART_MAIN);
    lv_obj_set_style_border_color(ecard, c_red(), LV_PART_MAIN);
    lv_obj_set_style_border_width(ecard, 2, LV_PART_MAIN);
    lv_obj_clear_flag(ecard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(ecard, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *etitle = lv_label_create(ecard);
    lv_obj_set_style_text_font(etitle, &font_argus_label_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(etitle, c_red(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(etitle, 2, LV_PART_MAIN);
    lv_label_set_text(etitle, "REFRESH FAILED");
    lv_obj_align(etitle, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *edetail = lv_label_create(ecard);
    lv_obj_set_style_text_font(edetail, &font_argus_label_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(edetail, c_dim(), LV_PART_MAIN);
    lv_obj_set_style_text_align(edetail, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(edetail, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(edetail, 210);
    lv_label_set_text(edetail, "No new data from the phone");
    lv_obj_align(edetail, LV_ALIGN_TOP_MID, 0, 78);

    // Close (X) button, top-right corner of the card.
    lv_obj_t *xbtn = lv_obj_create(ecard);
    lv_obj_remove_style_all(xbtn);
    lv_obj_set_size(xbtn, 40, 40);
    lv_obj_align(xbtn, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_radius(xbtn, 20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(xbtn, lv_color_hex(0x2A1414), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(xbtn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(xbtn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(xbtn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(xbtn, err_close_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *xl = lv_label_create(xbtn);
    lv_obj_set_style_text_font(xl, &font_argus_label_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(xl, c_white(), LV_PART_MAIN);
    lv_label_set_text(xl, "X");
    lv_obj_center(xl);
}

}  // namespace

void health_screen_create()
{
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screen, on_gesture, LV_EVENT_GESTURE, NULL);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_style_text_font(title, &font_argus_label_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, c_white(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(title, 4, LV_PART_MAIN);
    lv_label_set_text(title, "HEALTH");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *rule = lv_obj_create(s_screen);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, 120, 3);
    lv_obj_set_style_bg_color(rule, c_red(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, 80);

    // "Last sync" age, small and dim, just under the rule.
    s_sync_lbl = lv_label_create(s_screen);
    lv_obj_set_width(s_sync_lbl, 300);   // fixed width + center so it stays centered
    lv_obj_set_style_text_align(s_sync_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_sync_lbl, &font_argus_label_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_sync_lbl, c_gray(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_sync_lbl, 1, LV_PART_MAIN);
    lv_label_set_text(s_sync_lbl, "Last sync: --");
    lv_obj_align(s_sync_lbl, LV_ALIGN_TOP_MID, 0, 88);

    // Rows compressed and raised so the last one (HEART) never lands in the
    // display's rounded bottom corner, which was clipping the value (a "68" read
    // as "58"). Block spans y 106..392, centered vertically on the round face.
    s_sleep_val  = make_row(106, "Sleep score");
    s_steps_val  = make_row(178, "Step goal");
    s_stress_val = make_row(250, "Stress level");
    s_hr_val     = make_row(322, "Avg Heartrate (2min)");

    // Overlays last so they sit on top of the rows.
    build_overlays();
}

void health_screen_update()
{
    if (!s_screen) return;
    uint32_t now = millis();
    health::HealthData &h = health_model();
    char buf[32];

    snprintf(buf, sizeof(buf), "%d / 100", h.sleep_score());
    set_val(s_sleep_val, h.has_sleep_score(), h.sleep_stale(now), buf);

    if (h.has_steps()) {
        snprintf(buf, sizeof(buf), "%lu / %lu",
                 (unsigned long)h.steps(), (unsigned long)h.step_goal());
    }
    set_val(s_steps_val, h.has_steps(), h.steps_stale(now), buf);

    snprintf(buf, sizeof(buf), "%d / 100", h.stress());
    set_val(s_stress_val, h.has_stress(), h.stress_stale(now), buf);

    snprintf(buf, sizeof(buf), "%u BPM", (unsigned)h.hr());
    set_val(s_hr_val, h.has_hr(), h.hr_stale(now), buf);

    // "Last sync" age (session only; a restored NVS snapshot reads as no sync).
    if (s_sync_lbl) {
        uint32_t last = health_last_rx_ms();
        char sbuf[32];
        if (last == 0) {
            snprintf(sbuf, sizeof(sbuf), "Last sync: --");
        } else {
            uint32_t sec = (now - last) / 1000;
            if (sec < 60)          snprintf(sbuf, sizeof(sbuf), "Last sync: %us ago", (unsigned)sec);
            else if (sec < 3600)   snprintf(sbuf, sizeof(sbuf), "Last sync: %um ago", (unsigned)(sec / 60));
            else if (sec < 86400)  snprintf(sbuf, sizeof(sbuf), "Last sync: %uh ago", (unsigned)(sec / 3600));
            else                   snprintf(sbuf, sizeof(sbuf), "Last sync: >1d ago");
        }
        lv_label_set_text(s_sync_lbl, sbuf);
    }
}

void health_screen_show()
{
    if (!s_screen) health_screen_create();
    cancel_refresh();                                  // no stale spinner
    if (s_err_ov) lv_obj_add_flag(s_err_ov, LV_OBJ_FLAG_HIDDEN);
    health_screen_update();
    lv_scr_load(s_screen);
}

bool health_screen_is_active()
{
    return s_screen && lv_screen_active() == s_screen;
}
