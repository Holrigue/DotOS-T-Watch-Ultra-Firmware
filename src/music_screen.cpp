// music_screen.cpp - see music_screen.h.
//
// Three LVGL screens sharing this file: Artists -> Tracks -> Now Playing.
// Styled in the Nothing palette to match the rest of DotOS (white / grey on
// black, red accent). music_lib_scan() runs once, lazily, on the first time
// Tools > Music is opened - not at boot, so a watch that never opens Music
// never pays the SD directory-scan cost, and re-opening later doesn't rescan
// again (swapping SD cards mid-session needs a reboot to pick up new music;
// a manual rescan affordance can follow later).
#include "music_screen.h"
#include "music_lib.h"
#include "music_player.h"
#include "theme.h"

#include <lvgl.h>
#include <cstdio>

// Defined in main.cpp.
void screen_return_to(lv_obj_t *scr);

static const lv_color_t NW = lv_color_hex(0xFFFFFF);   // primary text
static const lv_color_t NG = lv_color_hex(0x9A9A9A);   // secondary text
static const lv_color_t NR = lv_color_hex(0xE02020);   // accent red

static lv_obj_t *s_artists_scr;
static lv_obj_t *s_tracks_scr;
static lv_obj_t *s_playing_scr;
static lv_obj_t *s_return = nullptr;   // where the Artists screen's back-gesture goes

static lv_obj_t *s_artists_list;
static lv_obj_t *s_tracks_list;
static lv_obj_t *s_tracks_title;

static lv_obj_t *s_np_title;
static lv_obj_t *s_np_artist;
static lv_obj_t *s_np_playpause_label;
static lv_obj_t *s_np_bar;
static lv_obj_t *s_np_time;
static lv_timer_t *s_np_timer = nullptr;

static int s_cur_artist = -1;   // artist index the Tracks/Now-Playing screens show

static void tracks_screen_build(int artist_idx);
static void now_playing_screen_build();

// ---- helpers -----------------------------------------------------------------
static void fmt_time(float seconds, char *out, size_t out_len)
{
    if (seconds < 0) seconds = 0;
    int total = (int)seconds;
    snprintf(out, out_len, "%d:%02d", total / 60, total % 60);
}

static lv_obj_t *make_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *t = lv_label_create(parent);
    lv_obj_set_style_text_font(t, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(t, NG, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(t, 2, LV_PART_MAIN);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, 280);
    lv_label_set_text(t, text);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 40);
    return t;
}

static lv_obj_t *make_list_box(lv_obj_t *parent)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, 340, 300);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_set_style_bg_color(box, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_color(box, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(box, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(box, 4, LV_PART_MAIN);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_layout(box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    return box;
}

// One tappable row: white label on a dark card, red on press (native LVGL
// pressed-state recolour via the checked/pressed style would need extra
// wiring, so we just rely on the default press dimming - matches the Nothing
// palette's restraint elsewhere in this codebase).
static lv_obj_t *make_row(lv_obj_t *parent, const char *text)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x141414), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 6, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *l = lv_label_create(row);
    lv_obj_set_style_text_font(l, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, NW, LV_PART_MAIN);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, LV_PCT(90));
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);   // let the row take the tap
    return row;
}

static lv_obj_t *make_empty_msg(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, NG, LV_PART_MAIN);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, 280);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 20);
    return l;
}

// ---- Artists screen ------------------------------------------------------
static void on_artists_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_TOP || dir == LV_DIR_RIGHT) screen_return_to(s_return);
}

static void on_artist_row_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    tracks_screen_build(idx);
    lv_scr_load(s_tracks_scr);
}

static void artists_screen_populate()
{
    lv_obj_clean(s_artists_list);
    int n = music_lib_artist_count();
    if (n == 0) {
        make_empty_msg(s_artists_list,
            "No music found.\n\nCopy files to /music/<Artist>/ on the SD card.");
        return;
    }
    for (int i = 0; i < n; i++) {
        const MusicArtist *a = music_lib_artist(i);
        lv_obj_t *row = make_row(s_artists_list, a->name);
        lv_obj_add_event_cb(row, on_artist_row_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void artists_screen_build()
{
    s_artists_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_artists_scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(s_artists_scr, LV_OBJ_FLAG_SCROLLABLE);
    make_title(s_artists_scr, "MUSIC");
    s_artists_list = make_list_box(s_artists_scr);
    lv_obj_add_event_cb(s_artists_scr, on_artists_gesture, LV_EVENT_GESTURE, NULL);
}

// ---- Tracks screen ---------------------------------------------------------
static void on_tracks_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_TOP || dir == LV_DIR_RIGHT) lv_scr_load(s_artists_scr);
}

static void on_track_row_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (music_player_play(s_cur_artist, idx)) {
        now_playing_screen_build();
        lv_scr_load(s_playing_scr);
    }
}

static void tracks_screen_build(int artist_idx)
{
    s_cur_artist = artist_idx;
    const MusicArtist *a = music_lib_artist(artist_idx);

    if (!s_tracks_scr) {
        s_tracks_scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_tracks_scr, lv_color_black(), LV_PART_MAIN);
        lv_obj_clear_flag(s_tracks_scr, LV_OBJ_FLAG_SCROLLABLE);
        s_tracks_title = make_title(s_tracks_scr, "");
        s_tracks_list  = make_list_box(s_tracks_scr);
        lv_obj_add_event_cb(s_tracks_scr, on_tracks_gesture, LV_EVENT_GESTURE, NULL);
    }

    lv_label_set_text(s_tracks_title, a ? a->name : "");
    lv_obj_clean(s_tracks_list);
    if (!a || a->track_count == 0) {
        make_empty_msg(s_tracks_list, "No tracks.");
        return;
    }
    for (int i = 0; i < a->track_count; i++) {
        lv_obj_t *row = make_row(s_tracks_list, a->tracks[i].title);
        lv_obj_add_event_cb(row, on_track_row_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

// ---- Now Playing screen -----------------------------------------------------
static void on_playing_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    // Back leaves music PLAYING in the background - it runs on its own task
    // independent of any screen; only the Stop button below actually stops it.
    if (dir == LV_DIR_TOP || dir == LV_DIR_RIGHT) lv_scr_load(s_tracks_scr);
}

static void on_playpause_clicked(lv_event_t *)
{
    if (music_player_is_playing())      music_player_pause();
    else if (music_player_is_paused())  music_player_resume();
}

static void on_stop_clicked(lv_event_t *)
{
    music_player_stop();
    lv_scr_load(s_tracks_scr);
}

static void on_next_clicked(lv_event_t *) { music_player_next(); }
static void on_prev_clicked(lv_event_t *) { music_player_prev(); }

static void on_np_tick(lv_timer_t *)
{
    if (!s_playing_scr || lv_screen_active() != s_playing_scr) return;
    if (!music_player_is_playing() && !music_player_is_paused()) {
        // The track ended (or was never started) - fall back a level rather
        // than sit on a dead Now Playing screen.
        lv_scr_load(s_tracks_scr);
        return;
    }
    float pos = music_player_position_s(), dur = music_player_duration_s();
    lv_bar_set_range(s_np_bar, 0, dur > 0 ? (int32_t)dur : 1);
    lv_bar_set_value(s_np_bar, (int32_t)pos, LV_ANIM_OFF);
    char cur[16], tot[16], buf[40];
    fmt_time(pos, cur, sizeof(cur));
    fmt_time(dur, tot, sizeof(tot));
    snprintf(buf, sizeof(buf), "%s / %s", cur, tot);
    lv_label_set_text(s_np_time, buf);
    lv_label_set_text(s_np_playpause_label,
        music_player_is_playing() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void now_playing_screen_build()
{
    if (s_playing_scr) {
        // Already built - just refresh the text for the new track.
        lv_label_set_text(s_np_title, music_player_current_title());
        lv_label_set_text(s_np_artist, music_player_current_artist());
        return;
    }

    s_playing_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_playing_scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(s_playing_scr, LV_OBJ_FLAG_SCROLLABLE);

    s_np_title = lv_label_create(s_playing_scr);
    lv_obj_set_style_text_font(s_np_title, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_np_title, NW, LV_PART_MAIN);
    lv_label_set_long_mode(s_np_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_np_title, 320);
    lv_obj_set_style_text_align(s_np_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(s_np_title, music_player_current_title());
    lv_obj_align(s_np_title, LV_ALIGN_TOP_MID, 0, 90);

    s_np_artist = lv_label_create(s_playing_scr);
    lv_obj_set_style_text_font(s_np_artist, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_np_artist, NG, LV_PART_MAIN);
    lv_label_set_text(s_np_artist, music_player_current_artist());
    lv_obj_align(s_np_artist, LV_ALIGN_TOP_MID, 0, 120);

    s_np_bar = lv_bar_create(s_playing_scr);
    lv_obj_set_size(s_np_bar, 300, 6);
    lv_obj_align(s_np_bar, LV_ALIGN_TOP_MID, 0, 190);
    lv_obj_set_style_bg_color(s_np_bar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_np_bar, NR, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_np_bar, 3, LV_PART_MAIN);

    s_np_time = lv_label_create(s_playing_scr);
    lv_obj_set_style_text_font(s_np_time, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_np_time, NG, LV_PART_MAIN);
    lv_label_set_text(s_np_time, "0:00 / 0:00");
    lv_obj_align(s_np_time, LV_ALIGN_TOP_MID, 0, 204);

    // Transport row: prev - play/pause - next.
    lv_obj_t *prev = lv_button_create(s_playing_scr);
    lv_obj_set_size(prev, 60, 60);
    lv_obj_align(prev, LV_ALIGN_TOP_MID, -96, 240);
    lv_obj_set_style_bg_color(prev, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_radius(prev, 30, LV_PART_MAIN);
    lv_obj_add_event_cb(prev, on_prev_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *prev_l = lv_label_create(prev);
    lv_obj_set_style_text_font(prev_l, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(prev_l, NW, LV_PART_MAIN);
    lv_label_set_text(prev_l, LV_SYMBOL_PREV);
    lv_obj_center(prev_l);

    lv_obj_t *pp = lv_button_create(s_playing_scr);
    lv_obj_set_size(pp, 76, 76);
    lv_obj_align(pp, LV_ALIGN_TOP_MID, 0, 233);
    lv_obj_set_style_bg_color(pp, NR, LV_PART_MAIN);
    lv_obj_set_style_radius(pp, 38, LV_PART_MAIN);
    lv_obj_add_event_cb(pp, on_playpause_clicked, LV_EVENT_CLICKED, NULL);
    s_np_playpause_label = lv_label_create(pp);
    lv_obj_set_style_text_font(s_np_playpause_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_np_playpause_label, NW, LV_PART_MAIN);
    lv_label_set_text(s_np_playpause_label, LV_SYMBOL_PAUSE);
    lv_obj_center(s_np_playpause_label);

    lv_obj_t *next = lv_button_create(s_playing_scr);
    lv_obj_set_size(next, 60, 60);
    lv_obj_align(next, LV_ALIGN_TOP_MID, 96, 240);
    lv_obj_set_style_bg_color(next, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_radius(next, 30, LV_PART_MAIN);
    lv_obj_add_event_cb(next, on_next_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *next_l = lv_label_create(next);
    lv_obj_set_style_text_font(next_l, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(next_l, NW, LV_PART_MAIN);
    lv_label_set_text(next_l, LV_SYMBOL_NEXT);
    lv_obj_center(next_l);

    lv_obj_t *stop = lv_button_create(s_playing_scr);
    lv_obj_set_size(stop, 140, 42);
    lv_obj_align(stop, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(stop, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_color(stop, NR, LV_PART_MAIN);
    lv_obj_set_style_border_width(stop, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(stop, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(stop, on_stop_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *stop_l = lv_label_create(stop);
    lv_obj_set_style_text_font(stop_l, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(stop_l, NR, LV_PART_MAIN);
    lv_label_set_text(stop_l, "STOP");
    lv_obj_center(stop_l);

    lv_obj_add_event_cb(s_playing_scr, on_playing_gesture, LV_EVENT_GESTURE, NULL);

    if (!s_np_timer) s_np_timer = lv_timer_create(on_np_tick, 500, NULL);
}

// ---- public ------------------------------------------------------------------
void music_screen_show()
{
    if (!s_artists_scr) artists_screen_build();

    lv_obj_t *from = lv_screen_active();
    if (from != s_artists_scr && from != s_tracks_scr && from != s_playing_scr) {
        s_return = from;
    }

    if (!music_lib_is_scanned()) music_lib_scan();
    artists_screen_populate();
    lv_scr_load(s_artists_scr);
}

bool music_screen_is_active()
{
    lv_obj_t *cur = lv_screen_active();
    return cur == s_artists_scr || cur == s_tracks_scr || cur == s_playing_scr;
}
