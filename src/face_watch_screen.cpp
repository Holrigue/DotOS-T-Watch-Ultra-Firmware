// face_watch_screen.cpp - see face_watch_screen.h.
//
// Customization screen for the Dot watchface, opened from Tools > Face. All
// controls drive live setters (face_watch.* for fonts/accent/order, the shared
// clock_screen_* setters for 12/24h and wallpaper) and repaint the face at once
// via clock_screen_apply_face_custom(). Styled in the Nothing palette to match
// the face: white / grey on black, with a red default accent.
#include "face_watch_screen.h"
#include "face_watch.h"
#include "theme.h"

#include <lvgl.h>

// Defined in main.cpp.
void screen_return_to(lv_obj_t *scr);
void clock_screen_set_12h(bool use_12h);
bool clock_screen_get_12h();
void clock_screen_set_wallpaper(bool enabled);
bool clock_screen_get_wallpaper();
void clock_screen_apply_face_custom();

static const lv_color_t NW = lv_color_hex(0xFFFFFF);   // primary text
static const lv_color_t NG = lv_color_hex(0x9A9A9A);   // secondary text

static lv_obj_t *screen;
static lv_obj_t *s_return = nullptr;
static lv_obj_t *hour_dd;
static lv_obj_t *date_dd;
static lv_obj_t *time_dd;
static lv_obj_t *order_dd;
static lv_obj_t *wall_sw;
static lv_obj_t *accent_dots[FACE_ACC__COUNT];

// Swatch colours, index == FaceAccent value.
static uint32_t accent_swatch_rgb(int i)
{
    switch (i) {
        case FACE_ACC_WHITE: return 0xFFFFFF;
        case FACE_ACC_GREY:  return 0x9A9A9A;
        case FACE_ACC_AMBER: return 0xF0A020;
        case FACE_ACC_BLUE:  return 0x9BBCD6;
        case FACE_ACC_RED:
        default:             return 0xE02020;
    }
}

// ---- back gesture ----------------------------------------------------------
static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_TOP || dir == LV_DIR_RIGHT) screen_return_to(s_return);
}

// ---- handlers --------------------------------------------------------------
static void on_hour_changed(lv_event_t *)
{
    face_set_hour_font((FaceHourFont)lv_dropdown_get_selected(hour_dd));
    clock_screen_apply_face_custom();
}

static void on_date_changed(lv_event_t *)
{
    face_set_date_font((FaceDateFont)lv_dropdown_get_selected(date_dd));
    clock_screen_apply_face_custom();
}

static void on_time_changed(lv_event_t *)
{
    clock_screen_set_12h(lv_dropdown_get_selected(time_dd) == 1);   // 0 = 24h, 1 = 12h
    clock_screen_apply_face_custom();
}

static void on_order_changed(lv_event_t *)
{
    face_set_date_order((FaceDateOrder)lv_dropdown_get_selected(order_dd));
    clock_screen_apply_face_custom();
}

static void on_wall_changed(lv_event_t *)
{
    clock_screen_set_wallpaper(lv_obj_has_state(wall_sw, LV_STATE_CHECKED));
}

static void refresh_accent_rings()
{
    int sel = (int)face_accent();
    for (int i = 0; i < FACE_ACC__COUNT; i++) {
        bool on = (i == sel);
        lv_obj_set_style_border_color(accent_dots[i], NW, LV_PART_MAIN);
        lv_obj_set_style_border_width(accent_dots[i], on ? 3 : 0, LV_PART_MAIN);
    }
}

static void on_accent_clicked(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= FACE_ACC__COUNT) return;
    face_set_accent((FaceAccent)i);
    refresh_accent_rings();
    clock_screen_apply_face_custom();
}

// ---- row helpers -----------------------------------------------------------
static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &font_argus_label_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, NW, LV_PART_MAIN);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 24, y + 8);
    return l;
}

static lv_obj_t *make_dropdown(lv_obj_t *parent, const char *opts, int y, lv_event_cb_t cb)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, opts);
    lv_obj_set_size(dd, 190, 40);
    lv_obj_align(dd, LV_ALIGN_TOP_RIGHT, -24, y);
    lv_obj_set_style_bg_color(dd, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_text_color(dd, NW, LV_PART_MAIN);
    lv_obj_set_style_border_color(dd, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_border_width(dd, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(dd, 8, LV_PART_MAIN);
    lv_obj_clear_flag(dd, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return dd;
}

// ---- build -----------------------------------------------------------------
void face_watch_screen_create()
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_font(title, &font_argus_label_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, NG, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(title, 3, LV_PART_MAIN);
    lv_label_set_text(title, "FACE");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    make_label(screen, "Hour font", 96);
    hour_dd = make_dropdown(screen, "Dots\nMontserrat", 96, on_hour_changed);
    lv_dropdown_set_selected(hour_dd, (uint32_t)face_hour_font());

    make_label(screen, "Date font", 148);
    date_dd = make_dropdown(screen, "Orbitron\nMono", 148, on_date_changed);
    lv_dropdown_set_selected(date_dd, (uint32_t)face_date_font());

    // Accent swatches row.
    make_label(screen, "Accent", 200);
    for (int i = 0; i < FACE_ACC__COUNT; i++) {
        lv_obj_t *d = lv_obj_create(screen);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 32, 32);
        lv_obj_align(d, LV_ALIGN_TOP_RIGHT, -24 - (FACE_ACC__COUNT - 1 - i) * 40, 202);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(d, lv_color_hex(accent_swatch_rgb(i)), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(d, on_accent_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        accent_dots[i] = d;
    }
    refresh_accent_rings();

    make_label(screen, "Wallpaper", 252);
    wall_sw = lv_switch_create(screen);
    lv_obj_set_size(wall_sw, 70, 34);
    lv_obj_align(wall_sw, LV_ALIGN_TOP_RIGHT, -24, 255);
    lv_obj_set_style_bg_color(wall_sw, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(wall_sw, NW, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_add_event_cb(wall_sw, on_wall_changed, LV_EVENT_VALUE_CHANGED, NULL);

    make_label(screen, "Time", 304);
    time_dd = make_dropdown(screen, "24-hour\n12-hour", 304, on_time_changed);

    make_label(screen, "Date order", 356);
    order_dd = make_dropdown(screen, "DD/MM\nMM/DD\nYYYY-MM-DD\nDD Mon", 356, on_order_changed);
    lv_dropdown_set_selected(order_dd, (uint32_t)face_date_order());

    lv_obj_add_event_cb(screen, on_gesture, LV_EVENT_GESTURE, NULL);
}

void face_watch_screen_show()
{
    if (!screen) face_watch_screen_create();

    lv_obj_t *from = lv_screen_active();
    if (from != screen) s_return = from;

    // Reflect the live state of the shared toggles on open.
    lv_dropdown_set_selected(time_dd, clock_screen_get_12h() ? 1 : 0);
    if (clock_screen_get_wallpaper()) lv_obj_add_state(wall_sw, LV_STATE_CHECKED);
    else                              lv_obj_clear_state(wall_sw, LV_STATE_CHECKED);
    refresh_accent_rings();

    lv_scr_load(screen);
}

bool face_watch_screen_is_active() { return lv_screen_active() == screen; }
