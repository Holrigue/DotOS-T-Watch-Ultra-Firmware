// face_watch_screen.cpp - see face_watch_screen.h.
//
// Customization screen for the Dot watchface, opened from Tools > Face. All
// controls drive live setters (face_watch.* for fonts/accent/order, the shared
// clock_screen_* setters for 12/24h and wallpaper) and repaint the face at once
// via clock_screen_apply_face_custom(). Styled in the Nothing palette to match
// the face: white / grey on black, with the DotOS orange as the default accent.
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
static lv_obj_t *text_dd;
static lv_obj_t *wall_sw;
// The accents offered in the picker, in display order. (Not simply 0..COUNT: the
// numeric values are pinned for saved settings and one of them is retired.)
static const FaceAccent kAccentChoices[] = {
    FACE_ACC_ORANGE, FACE_ACC_AMBER, FACE_ACC_RED, FACE_ACC_PURPLE,
    FACE_ACC_CYAN, FACE_ACC_GREEN, FACE_ACC_GREY,
};
static constexpr int ACCENT_N = sizeof(kAccentChoices) / sizeof(kAccentChoices[0]);

// The Accent row is a drop-down built by hand: LVGL's own list draws every item in
// one colour, and here each colour's name is written in that colour.
static lv_obj_t *accent_btn      = nullptr;   // the closed drop-down (shows the current pick)
static lv_obj_t *accent_btn_text = nullptr;
static lv_obj_t *accent_scrim    = nullptr;   // the open list's backdrop (null while closed)

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

// "Global text" family for titles + body labels + notifications (not the clock
// hour or the date line). Screens re-read the choice via theme_text_font()/
// theme_title_font() the next time they are built, so the change propagates as
// the wearer navigates; the Dot face repaints immediately.
static void on_text_changed(lv_event_t *)
{
    face_set_text_font((FaceTextFont)lv_dropdown_get_selected(text_dd));
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

static void refresh_accent_button()
{
    if (!accent_btn) return;
    FaceAccent cur = face_accent();
    lv_color_t col = lv_color_hex(face_accent_rgb_of(cur));
    lv_obj_set_style_text_color(accent_btn_text, col, LV_PART_MAIN);
    lv_label_set_text(accent_btn_text, face_accent_name(cur));
}

// Close the list. Async: this runs inside a click on a row that is a DESCENDANT of the
// scrim, and freeing it mid-dispatch would corrupt LVGL's input state.
static void accent_list_close()
{
    if (accent_scrim) { lv_obj_delete_async(accent_scrim); accent_scrim = nullptr; }
}

static void on_accent_scrim_clicked(lv_event_t *e)
{
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) accent_list_close();
}

static void on_accent_picked(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < ACCENT_N) {
        face_set_accent(kAccentChoices[i]);
        refresh_accent_button();
        clock_screen_apply_face_custom();
    }
    accent_list_close();
}

static void on_accent_open(lv_event_t *)
{
    if (accent_scrim) return;
    accent_scrim = lv_obj_create(screen);
    lv_obj_remove_style_all(accent_scrim);
    lv_obj_set_size(accent_scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(accent_scrim, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(accent_scrim, LV_OPA_80, LV_PART_MAIN);
    lv_obj_add_flag(accent_scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(accent_scrim, LV_OBJ_FLAG_GESTURE_BUBBLE);   // a swipe here must not leave the page
    lv_obj_add_event_cb(accent_scrim, on_accent_scrim_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(accent_scrim);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 310, 360);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, ARGUS_TILE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, 28, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, 4, LV_PART_MAIN);
    lv_obj_set_layout(panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_GESTURE_BUBBLE);

    for (int i = 0; i < ACCENT_N; i++) {
        FaceAccent a = kAccentChoices[i];
        lv_color_t col = lv_color_hex(face_accent_rgb_of(a));
        bool on = (a == face_accent());

        lv_obj_t *row = lv_obj_create(panel);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 44);
        lv_obj_set_style_radius(row, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, ARGUS_RAISED, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, on ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, on_accent_picked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 18, 18);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 16, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, col, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *t = lv_label_create(row);
        lv_obj_set_style_text_font(t, theme_text_font(20), LV_PART_MAIN);
        lv_obj_set_style_text_color(t, col, LV_PART_MAIN);
        lv_label_set_text(t, face_accent_name(a));
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 46, 0);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);

        if (on) {
            lv_obj_t *ck = lv_label_create(row);
            lv_obj_set_style_text_font(ck, &lv_font_montserrat_20, LV_PART_MAIN);
            lv_obj_set_style_text_color(ck, col, LV_PART_MAIN);
            lv_label_set_text(ck, LV_SYMBOL_OK);
            lv_obj_align(ck, LV_ALIGN_RIGHT_MID, -16, 0);
            lv_obj_remove_flag(ck, LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

// ---- row helpers -----------------------------------------------------------
static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, theme_text_font(20), LV_PART_MAIN);
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
    lv_obj_set_style_text_font(title, theme_text_font(20), LV_PART_MAIN);
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

    // Accent: a drop-down whose entries are written in their own colour.
    make_label(screen, "Accent", 200);
    accent_btn = lv_obj_create(screen);
    lv_obj_remove_style_all(accent_btn);
    lv_obj_set_size(accent_btn, 190, 40);
    lv_obj_align(accent_btn, LV_ALIGN_TOP_RIGHT, -24, 200);
    lv_obj_set_style_bg_color(accent_btn, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(accent_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(accent_btn, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_border_width(accent_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(accent_btn, 8, LV_PART_MAIN);
    lv_obj_clear_flag(accent_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(accent_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(accent_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(accent_btn, on_accent_open, LV_EVENT_CLICKED, NULL);

    accent_btn_text = lv_label_create(accent_btn);
    lv_obj_set_style_text_font(accent_btn_text, theme_text_font(16), LV_PART_MAIN);
    lv_obj_align(accent_btn_text, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *chev = lv_label_create(accent_btn);
    lv_obj_set_style_text_font(chev, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(chev, NG, LV_PART_MAIN);
    lv_label_set_text(chev, LV_SYMBOL_DOWN);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -10, 0);
    refresh_accent_button();

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

    make_label(screen, "Global text", 408);
    text_dd = make_dropdown(screen, "Default\nRoboto\nInter", 408, on_text_changed);
    lv_dropdown_set_selected(text_dd, (uint32_t)face_text_font());

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
    refresh_accent_button();
    accent_list_close();     // never reopen with a stale list on top

    lv_scr_load(screen);
}

bool face_watch_screen_is_active() { return lv_screen_active() == screen; }
