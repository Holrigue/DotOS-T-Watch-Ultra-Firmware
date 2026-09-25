// find_screen.cpp - see find_screen.h.
//
// "Find my device" for the DotOS companion app, styled in the Nothing palette to
// match the face. One big red button rings the phone (a BLE notify the app
// listens for); the reverse direction (phone ringing the watch) is driven from
// the app and handled in ans/main. Only works while the watch is connected to
// the companion app over the Android notification link.
#include "find_screen.h"
#include "theme.h"

#include <lvgl.h>

// Defined in main.cpp.
void screen_return_to(lv_obj_t *scr);
bool find_ring_phone();        // notify the phone to ring; false if not connected
bool find_phone_connected();   // is a companion phone connected right now

static const lv_color_t NW = lv_color_hex(0xFFFFFF);   // primary text
static const lv_color_t NG = lv_color_hex(0x9A9A9A);   // secondary text
static const lv_color_t NR = lv_color_hex(0xE02020);   // accent red

static lv_obj_t *screen;
static lv_obj_t *s_return = nullptr;
static lv_obj_t *s_status;

static void refresh_status()
{
    if (!s_status) return;
    bool up = find_phone_connected();
    lv_label_set_text(s_status, up ? "Phone connected" : "Phone not connected");
    lv_obj_set_style_text_color(s_status, up ? NW : NG, LV_PART_MAIN);
}

static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_TOP || dir == LV_DIR_RIGHT) screen_return_to(s_return);
}

static void on_ring_phone(lv_event_t *)
{
    bool ok = find_ring_phone();
    if (s_status) {
        lv_label_set_text(s_status, ok ? "Ringing your phone..."
                                       : "Phone not connected - open the app first");
        lv_obj_set_style_text_color(s_status, ok ? NR : NG, LV_PART_MAIN);
    }
}

static void build()
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_font(title, &font_argus_label_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, NG, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(title, 3, LV_PART_MAIN);
    lv_label_set_text(title, "FIND");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    // Ring-the-phone button: big, red, centred.
    lv_obj_t *btn = lv_button_create(screen);
    lv_obj_set_size(btn, 240, 96);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(btn, NR, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 20, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, on_ring_phone, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(bl, NW, LV_PART_MAIN);
    lv_label_set_text(bl, "Ring my phone");
    lv_obj_center(bl);

    s_status = lv_label_create(screen);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status, NG, LV_PART_MAIN);
    lv_label_set_text(s_status, "");
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 60);

    lv_obj_t *hint = lv_label_create(screen);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, NG, LV_PART_MAIN);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, 320);
    lv_label_set_text(hint, "To ring the watch, tap Ring watch in the phone app.");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -40);

    lv_obj_add_event_cb(screen, on_gesture, LV_EVENT_GESTURE, NULL);
}

void find_screen_show()
{
    if (!screen) build();

    lv_obj_t *from = lv_screen_active();
    if (from != screen) s_return = from;

    refresh_status();
    lv_scr_load(screen);
}

bool find_screen_is_active() { return lv_screen_active() == screen; }
