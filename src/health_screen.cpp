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

void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    // A pull from the very top edge opens the notification shade (handled in
    // main.cpp); any other swipe down goes home.
    if (dir == LV_DIR_BOTTOM && !touch_started_at_top_edge())
        clock_screen_show();
}

// One metric row: a small gray name on the left, a big value on the right.
// Returns the value label so update() can drive it.
lv_obj_t *make_row(int y, const char *name)
{
    lv_obj_t *row = lv_obj_create(s_screen);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 360, 78);
    lv_obj_set_pos(row, 25, y);
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
    lv_obj_set_size(sep, 360, 1);
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
    lv_label_set_text(title, "SANTE");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    lv_obj_t *rule = lv_obj_create(s_screen);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, 120, 3);
    lv_obj_set_style_bg_color(rule, c_red(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, 80);

    s_sleep_val  = make_row(120, "SOMMEIL");
    s_steps_val  = make_row(210, "PAS");
    s_stress_val = make_row(300, "STRESS");
    s_hr_val     = make_row(390, "CARDIO");
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
}

void health_screen_show()
{
    if (!s_screen) health_screen_create();
    health_screen_update();
    lv_scr_load(s_screen);
}

bool health_screen_is_active()
{
    return s_screen && lv_screen_active() == s_screen;
}
