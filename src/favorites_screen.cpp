// favorites_screen.cpp - see favorites_screen.h.
//
// Two screens: the grid of chosen shortcuts (opened by the watch face's
// swipe-right), and an Edit list of every favoritable app with a switch each.
// The chosen set is a list of app NAMES, persisted to /Settings/favorites.txt
// (one per line) so it survives a re-flash, and resolved back to a launch
// through apps_fav_launch_by_name().
#include "favorites_screen.h"
#include "apps_screen.h"
#include "theme.h"
#include "usb_sd.h"            // usb_sd_is_running() - host owns the card while mounted
#include <LilyGoLib.h>
#include <SD.h>
#include <cstring>

void clock_screen_show();      // main.cpp: the watch face (back target)

namespace {

constexpr int  MAX_FAV   = 16;
constexpr int  NAME_LEN  = 40;
const char    *FAV_PATH  = "/Settings/favorites.txt";

char s_fav[MAX_FAV][NAME_LEN];
int  s_fav_n = 0;

lv_obj_t *s_scr  = nullptr;    // the grid
lv_obj_t *s_edit = nullptr;    // the edit list

// ---- persistence -----------------------------------------------------------
void fav_load()
{
    s_fav_n = 0;
    if (!instance.isCardReady()) return;
    File f = SD.open(FAV_PATH, FILE_READ);
    if (!f) return;
    while (f.available() && s_fav_n < MAX_FAV) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        strncpy(s_fav[s_fav_n], line.c_str(), NAME_LEN - 1);
        s_fav[s_fav_n][NAME_LEN - 1] = '\0';
        s_fav_n++;
    }
    f.close();
}

void fav_save()
{
    if (usb_sd_is_running() || !instance.isCardReady()) return;
    SD.mkdir("/Settings");
    File f = SD.open(FAV_PATH, FILE_WRITE);
    if (!f) return;
    for (int i = 0; i < s_fav_n; i++) f.printf("%s\n", s_fav[i]);
    f.close();
}

int fav_index(const char *name)
{
    for (int i = 0; i < s_fav_n; i++)
        if (strcmp(s_fav[i], name) == 0) return i;
    return -1;
}

void fav_toggle(const char *name)
{
    int idx = fav_index(name);
    if (idx >= 0) {
        for (int i = idx; i < s_fav_n - 1; i++) strncpy(s_fav[i], s_fav[i + 1], NAME_LEN);
        s_fav_n--;
    } else if (s_fav_n < MAX_FAV) {
        strncpy(s_fav[s_fav_n], name, NAME_LEN - 1);
        s_fav[s_fav_n][NAME_LEN - 1] = '\0';
        s_fav_n++;
    }
    fav_save();
}

// ---- small UI helpers ------------------------------------------------------
lv_obj_t *make_screen(const char *title)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ARGUS_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr = lv_label_create(scr);
    lv_obj_set_style_text_font(hdr, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(hdr, ARGUS_QUIET, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(hdr, 3, LV_PART_MAIN);
    lv_label_set_text(hdr, title);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 26);
    return scr;
}

void on_grid_gesture(lv_event_t *e)
{
    // Reached by swiping right on the face, so swipe right goes back to it.
    // Other directions fall through to the global nav handler.
    if (lv_indev_get_gesture_dir(lv_event_get_indev(e)) == LV_DIR_RIGHT)
        clock_screen_show();
}

void on_edit_gesture(lv_event_t *e)
{
    if (lv_indev_get_gesture_dir(lv_event_get_indev(e)) == LV_DIR_RIGHT)
        favorites_screen_show();   // back to the (rebuilt) grid
}

void on_fav_tile(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);
    if (name) apps_fav_launch_by_name(name);
}

void show_edit();   // fwd

void on_edit_button(lv_event_t *)
{
    show_edit();
}

void on_edit_row(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    const char *name = apps_fav_name(i);
    if (name) fav_toggle(name);
}

// ---- the grid --------------------------------------------------------------
lv_obj_t *make_tile(lv_obj_t *grid, const char *text, bool accent)
{
    lv_obj_t *t = lv_obj_create(grid);
    lv_obj_remove_style_all(t);
    lv_obj_set_size(t, 182, 74);
    lv_obj_set_style_bg_color(t, ARGUS_TILE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(t, ARGUS_RAISED, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(t, 18, LV_PART_MAIN);
    if (accent) {
        lv_obj_set_style_border_color(t, ARGUS_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_border_width(t, 2, LV_PART_MAIN);
    }
    lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(t, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = lv_label_create(t);
    lv_obj_set_style_text_font(l, theme_text_font(18), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, accent ? ARGUS_ACCENT : ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(l, 162);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return t;
}

void build_grid()
{
    s_scr = make_screen("FAVORITES");

    lv_obj_t *grid = lv_obj_create(s_scr);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 400, 430);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_pad_all(grid, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_column(grid, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(grid, 40, LV_PART_MAIN);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_GESTURE_BUBBLE);

    for (int i = 0; i < s_fav_n; i++) {
        lv_obj_t *t = make_tile(grid, s_fav[i], false);
        lv_obj_add_event_cb(t, on_fav_tile, LV_EVENT_CLICKED, (void *)s_fav[i]);
    }
    if (s_fav_n == 0) {
        lv_obj_t *hint = lv_label_create(grid);
        lv_obj_set_style_text_font(hint, theme_text_font(16), LV_PART_MAIN);
        lv_obj_set_style_text_color(hint, ARGUS_TEXT_DIM, LV_PART_MAIN);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_width(hint, 360);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_label_set_text(hint, "No shortcuts yet.\nTap Edit to pick your favourite apps.");
        lv_obj_set_style_pad_bottom(hint, 10, LV_PART_MAIN);
    }
    lv_obj_t *edit = make_tile(grid, LV_SYMBOL_SETTINGS "  Edit", true);
    lv_obj_add_event_cb(edit, on_edit_button, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(s_scr, on_grid_gesture, LV_EVENT_GESTURE, NULL);
}

// ---- the edit list ---------------------------------------------------------
void show_edit()
{
    if (s_edit) { lv_obj_del(s_edit); s_edit = nullptr; }
    s_edit = make_screen("EDIT FAVORITES");

    lv_obj_t *list = lv_obj_create(s_edit);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 400, 432);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_pad_all(list, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(list, 40, LV_PART_MAIN);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(list, LV_OBJ_FLAG_GESTURE_BUBBLE);

    int n = apps_fav_count();
    for (int i = 0; i < n; i++) {
        const char *name = apps_fav_name(i);
        if (!name) continue;
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 384, 48);
        lv_obj_set_style_bg_color(row, ARGUS_TILE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 10, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);

        lv_obj_t *l = lv_label_create(row);
        lv_obj_set_style_text_font(l, theme_text_font(18), LV_PART_MAIN);
        lv_obj_set_style_text_color(l, ARGUS_TEXT, LV_PART_MAIN);
        lv_label_set_text(l, name);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 14, 0);

        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_set_size(sw, 60, 30);
        lv_obj_set_style_bg_color(sw, lv_color_make(0x44, 0x44, 0x44), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(sw, ARGUS_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (fav_index(name) >= 0) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, on_edit_row, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -14, 0);
    }

    lv_obj_add_event_cb(s_edit, on_edit_gesture, LV_EVENT_GESTURE, NULL);
    lv_scr_load(s_edit);
}

}  // namespace

void favorites_init()
{
    fav_load();
}

void favorites_screen_show()
{
    if (s_scr) { lv_obj_del(s_scr); s_scr = nullptr; }   // rebuild from the current list
    build_grid();
    lv_scr_load(s_scr);
}

bool favorites_screen_is_active()
{
    return s_scr && lv_screen_active() == s_scr;
}
