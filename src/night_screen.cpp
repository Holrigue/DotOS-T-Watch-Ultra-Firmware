// night_screen.cpp - see night_screen.h.
//
// A switch to turn Night time on, then the quiet window as two times set with
// - / + buttons in 30 minute steps (the window may cross midnight). The times
// are shown in the watch's 12 or 24 hour format. Plain ASCII text on purpose: the
// UI fonts are subsets and a missing glyph renders as a tofu box.
#include "night_screen.h"
#include "night_mode.h"
#include "night_window.h"
#include "theme.h"

#include <lvgl.h>
#include <stdint.h>
#include <stdio.h>

// Defined in main.cpp.
void screen_return_to(lv_obj_t *scr);
bool clock_screen_get_12h();

static const lv_color_t NW = lv_color_hex(0xFFFFFF);   // primary text
static const lv_color_t NG = lv_color_hex(0x9A9A9A);   // secondary text

static constexpr int STEP_MIN = 30;

static lv_obj_t *s_screen    = nullptr;
static lv_obj_t *s_return    = nullptr;
static lv_obj_t *s_on_sw     = nullptr;
static lv_obj_t *s_window    = nullptr;   // the two time rows; dimmed while Night time is off
static lv_obj_t *s_start_lbl = nullptr;
static lv_obj_t *s_end_lbl   = nullptr;

// ---- formatting / refresh -------------------------------------------------------

static void format_time(int minute_of_day, char *buf, size_t n)
{
    int h = minute_of_day / 60, m = minute_of_day % 60;
    if (clock_screen_get_12h()) {
        int h12 = h % 12;
        if (h12 == 0) h12 = 12;
        snprintf(buf, n, "%d:%02d %s", h12, m, h < 12 ? "AM" : "PM");
    } else {
        snprintf(buf, n, "%02d:%02d", h, m);
    }
}

static void refresh()
{
    char buf[16];
    format_time(night_mode_start_min(), buf, sizeof buf);
    lv_label_set_text(s_start_lbl, buf);
    format_time(night_mode_end_min(), buf, sizeof buf);
    lv_label_set_text(s_end_lbl, buf);
    lv_obj_set_style_opa(s_window, night_mode_enabled() ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN);
}

// ---- handlers ---------------------------------------------------------------------

static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) screen_return_to(s_return);
}

static void on_switch(lv_event_t *)
{
    night_mode_set_enabled(lv_obj_has_state(s_on_sw, LV_STATE_CHECKED));
    refresh();
}

// user data: bit 1 = which time (0 start, 1 end), bit 0 = direction (1 plus, 0 minus).
static void on_step(lv_event_t *e)
{
    int code  = (int)(intptr_t)lv_event_get_user_data(e);
    int which = code >> 1;
    int delta = (code & 1) ? STEP_MIN : -STEP_MIN;
    int start = night_mode_start_min();
    int end   = night_mode_end_min();
    if (which == 0) start = night_step_minutes(start, delta);
    else            end   = night_step_minutes(end, delta);
    night_mode_set_window(start, end);
    refresh();
}

// ---- building blocks ------------------------------------------------------------------

static lv_obj_t *make_step_button(lv_obj_t *parent, const char *glyph, int code)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 50, 42);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_border_color(b, NG, LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(b, on_step, LV_EVENT_CLICKED, (void *)(intptr_t)code);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, NW, LV_PART_MAIN);
    lv_label_set_text(l, glyph);
    lv_obj_center(l);
    return b;
}

// "From   [-]  22:00  [+]" - a label, then the stepper.
static lv_obj_t *make_time_row(lv_obj_t *parent, const char *title, int which, lv_obj_t **time_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 46);
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(row);
    lv_obj_set_style_text_font(t, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(t, NW, LV_PART_MAIN);
    lv_label_set_text(t, title);
    lv_obj_set_flex_grow(t, 1);

    make_step_button(row, "-", which * 2 + 0);

    lv_obj_t *tl = lv_label_create(row);
    lv_obj_set_width(tl, 92);
    lv_obj_set_style_text_align(tl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(tl, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(tl, NW, LV_PART_MAIN);
    lv_label_set_text(tl, "");
    *time_out = tl;

    make_step_button(row, "+", which * 2 + 1);
    return row;
}

static void build()
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_screen, 34, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_screen, 58, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_screen, 90, LV_PART_MAIN);   // keep the last text off the bezel
    lv_obj_set_style_pad_row(s_screen, 20, LV_PART_MAIN);
    lv_obj_set_layout(s_screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_screen, LV_DIR_VER);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(title, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, NG, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(title, 3, LV_PART_MAIN);
    lv_label_set_text(title, "NIGHT TIME");

    // Header row: title + switch.
    lv_obj_t *head = lv_obj_create(s_screen);
    lv_obj_remove_style_all(head);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, 38);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ht = lv_label_create(head);
    lv_obj_set_style_text_font(ht, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(ht, NW, LV_PART_MAIN);
    lv_label_set_text(ht, "Quiet hours");
    lv_obj_align(ht, LV_ALIGN_LEFT_MID, 0, 0);

    s_on_sw = lv_switch_create(head);
    lv_obj_set_size(s_on_sw, 70, 34);
    lv_obj_set_style_bg_color(s_on_sw, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_on_sw, ARGUS_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_add_event_cb(s_on_sw, on_switch, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(s_on_sw, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t *desc = lv_label_create(s_screen);
    lv_obj_set_width(desc, LV_PCT(100));
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(desc, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(desc, NG, LV_PART_MAIN);
    lv_label_set_text(desc,
        "During these hours the watch stays quiet: it does not vibrate, and a new "
        "notification does not light up the screen. Notifications still wait in the "
        "list. Your alarms still ring and vibrate.");

    // The window: two stepper rows, dimmed while Night time is off.
    s_window = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_window);
    lv_obj_set_width(s_window, LV_PCT(100));
    lv_obj_set_height(s_window, LV_SIZE_CONTENT);
    lv_obj_set_layout(s_window, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_window, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_window, 10, LV_PART_MAIN);
    lv_obj_clear_flag(s_window, LV_OBJ_FLAG_SCROLLABLE);

    make_time_row(s_window, "From", 0, &s_start_lbl);
    make_time_row(s_window, "Until", 1, &s_end_lbl);

    lv_obj_add_event_cb(s_screen, on_gesture, LV_EVENT_GESTURE, NULL);
}

// ---- public -----------------------------------------------------------------------------

void night_screen_show()
{
    if (!s_screen) build();

    lv_obj_t *from = lv_screen_active();
    if (from != s_screen) s_return = from;

    if (night_mode_enabled()) lv_obj_add_state(s_on_sw, LV_STATE_CHECKED);
    else                      lv_obj_clear_state(s_on_sw, LV_STATE_CHECKED);
    refresh();

    lv_obj_scroll_to_y(s_screen, 0, LV_ANIM_OFF);
    lv_scr_load(s_screen);
}

bool night_screen_is_active() { return lv_screen_active() == s_screen; }
