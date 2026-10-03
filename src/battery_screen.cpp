// battery_screen.cpp - see battery_screen.h.
//
// The three battery behaviours used to sit as unexplained rows in the long
// Settings list ("Battery saver", "Battery longevity"), and it was not clear how
// they differed. They live here now, each with a short explanation:
//
//   Auto turn off display  - daily power saving, opt-in.
//   Battery longevity      - long-term cell health, opt-in.
//   Low-battery saver      - automatic safety net below 20%, not a setting.
//
// Text is plain ASCII on purpose: the UI fonts are subsets and any glyph they
// lack renders as a tofu box.
#include "battery_screen.h"
#include "settings_screen.h"
#include "power_mgmt.h"
#include "theme.h"

#include <LilyGoLib.h>
#include <lvgl.h>
#include <stdio.h>

// Defined in main.cpp.
void screen_return_to(lv_obj_t *scr);

static const lv_color_t NW = lv_color_hex(0xFFFFFF);   // primary text
static const lv_color_t NG = lv_color_hex(0x9A9A9A);   // secondary text

static lv_obj_t *s_screen      = nullptr;
static lv_obj_t *s_return      = nullptr;
static lv_obj_t *s_status_lbl  = nullptr;   // "Battery 82%  -  Charging"
static lv_obj_t *s_autooff_sw  = nullptr;
static lv_obj_t *s_longv_sw    = nullptr;

// ---- handlers ---------------------------------------------------------------

static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_RIGHT) screen_return_to(s_return);
}

static void on_autooff_changed(lv_event_t *)
{
    settings_set_auto_off_display(lv_obj_has_state(s_autooff_sw, LV_STATE_CHECKED));
}

static void on_longv_changed(lv_event_t *)
{
    power_set_longevity(lv_obj_has_state(s_longv_sw, LV_STATE_CHECKED));   // persisted there
}

// ---- building blocks --------------------------------------------------------

// A titled option: a header row (title left, switch OR a dim tag right) followed
// by a wrapped explanation. Everything sizes to its content so the explanations
// can be any length.
static lv_obj_t *make_option(lv_obj_t *parent, const char *title, const char *desc,
                             lv_obj_t **sw_out, lv_event_cb_t cb, const char *tag)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    lv_obj_set_layout(box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 6, LV_PART_MAIN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *head = lv_obj_create(box);
    lv_obj_remove_style_all(head);
    lv_obj_set_width(head, LV_PCT(100));
    lv_obj_set_height(head, 38);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(head);
    lv_obj_set_style_text_font(t, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(t, NW, LV_PART_MAIN);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);

    if (sw_out) {
        lv_obj_t *sw = lv_switch_create(head);
        lv_obj_set_size(sw, 70, 34);
        lv_obj_set_style_bg_color(sw, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(sw, ARGUS_ACCENT, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        *sw_out = sw;
    } else if (tag) {
        lv_obj_t *g = lv_label_create(head);
        lv_obj_set_style_text_font(g, theme_text_font(16), LV_PART_MAIN);
        lv_obj_set_style_text_color(g, NG, LV_PART_MAIN);
        lv_label_set_text(g, tag);
        lv_obj_align(g, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    lv_obj_t *d = lv_label_create(box);
    lv_obj_set_width(d, LV_PCT(100));
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(d, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(d, NG, LV_PART_MAIN);
    lv_label_set_text(d, desc);
    return box;
}

static void build()
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_screen, 34, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_screen, 58, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_screen, 90, LV_PART_MAIN);   // keep the last text off the bezel
    lv_obj_set_style_pad_row(s_screen, 22, LV_PART_MAIN);
    lv_obj_set_layout(s_screen, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_screen, LV_DIR_VER);

    lv_obj_t *title = lv_label_create(s_screen);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(title, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, NG, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(title, 3, LV_PART_MAIN);
    lv_label_set_text(title, "BATTERY");

    s_status_lbl = lv_label_create(s_screen);
    lv_obj_set_width(s_status_lbl, LV_PCT(100));
    lv_obj_set_style_text_align(s_status_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status_lbl, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_lbl, NW, LV_PART_MAIN);
    lv_label_set_text(s_status_lbl, "");

    make_option(s_screen, "Auto turn off display",
        "Your choice, saves power every day. About 1 minute after the screen dims, "
        "and only if you have not touched the watch, it turns completely black. "
        "A touch, a button press or a new notification wakes it.",
        &s_autooff_sw, on_autooff_changed, nullptr);

    make_option(s_screen, "Battery longevity",
        "Your choice, protects the battery over the years. Charges to 4.1 V instead "
        "of 4.2 V: roughly double the battery lifespan, for about 10% less runtime "
        "on each charge.",
        &s_longv_sw, on_longv_changed, nullptr);

    make_option(s_screen, "Low-battery saver",
        "Automatic safety net, not a setting. Below 20% and not charging, the screen "
        "also turns off 10 seconds after it dims. It stops above 25% or as soon as "
        "you charge.",
        nullptr, nullptr, "Automatic");

    lv_obj_add_event_cb(s_screen, on_gesture, LV_EVENT_GESTURE, NULL);
}

// ---- public -----------------------------------------------------------------

void battery_screen_show()
{
    if (!s_screen) build();

    lv_obj_t *from = lv_screen_active();
    if (from != s_screen) s_return = from;

    // Reflect the live state on open.
    if (settings_get_auto_off_display()) lv_obj_add_state(s_autooff_sw, LV_STATE_CHECKED);
    else                                 lv_obj_clear_state(s_autooff_sw, LV_STATE_CHECKED);
    if (power_get_longevity()) lv_obj_add_state(s_longv_sw, LV_STATE_CHECKED);
    else                       lv_obj_clear_state(s_longv_sw, LV_STATE_CHECKED);

    int  pct      = instance.pmu.getBatteryPercent();
    bool charging = instance.pmu.isVbusIn();
    char buf[48];
    if (pct >= 0) snprintf(buf, sizeof buf, "%d%%%s", pct, charging ? "  -  charging" : "");
    else          snprintf(buf, sizeof buf, "--");
    lv_label_set_text(s_status_lbl, buf);

    lv_obj_scroll_to_y(s_screen, 0, LV_ANIM_OFF);
    lv_scr_load(s_screen);
}

bool battery_screen_is_active() { return lv_screen_active() == s_screen; }
