// music_screen.cpp - see music_screen.h.
//
// Four LVGL screens: Mode (Artist / Album / Track) -> Groups (artists or albums,
// skipped for Track) -> Tracks -> Now Playing. Lists use the Nothing palette
// (white / grey on black, red accent); Now Playing is the DotOS cassette (see
// docs/design/). Long titles roll horizontally, iPod-style, once they overflow.
// music_lib_scan() runs once, lazily, on the first open - not at boot, so a
// watch that never opens Music never pays the SD directory-scan cost.
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

static lv_obj_t *s_mode_scr;
static lv_obj_t *s_groups_scr;
static lv_obj_t *s_tracks_scr;
static lv_obj_t *s_playing_scr;
static lv_obj_t *s_return = nullptr;   // where the Mode screen's back-gesture goes

static lv_obj_t *s_groups_list;
static lv_obj_t *s_groups_title;
static lv_obj_t *s_tracks_list;
static lv_obj_t *s_tracks_title;
static lv_obj_t *s_tracks_status;   // why a track did not start (empty when fine)

static lv_obj_t *s_np_title;
static lv_obj_t *s_np_artist;
static lv_obj_t *s_np_playpause_label;
static lv_obj_t *s_np_bar;
static lv_obj_t *s_np_time;
static lv_timer_t *s_np_timer = nullptr;

static MusicBrowse s_mode  = MUSIC_BY_ARTIST;   // active browse mode
static int         s_group = -1;                // active group within that mode

static void groups_screen_build();
static void tracks_screen_build(int group);
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
    lv_label_set_long_mode(t, LV_LABEL_LONG_SCROLL_CIRCULAR);   // roll long group names
    lv_obj_set_width(t, 300);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
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

// One tappable row. Long titles roll horizontally once they overflow (the row
// clips, and the label scrolls circularly after LVGL's built-in start delay),
// so a long "Artist - Album - Title" is readable without truncation.
static lv_obj_t *make_row(lv_obj_t *parent, const char *text, lv_event_cb_t cb, int ud)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 40);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x141414), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 6, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(row, true, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    if (cb) lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, (void *)(intptr_t)ud);

    lv_obj_t *l = lv_label_create(row);
    lv_obj_set_style_text_font(l, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, NW, LV_PART_MAIN);
    lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(l, LV_PCT(88));
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

// ---- Mode screen (Artist / Album / Track) --------------------------------
static void on_mode_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_event_get_indev(e));
    if (dir == LV_DIR_TOP || dir == LV_DIR_RIGHT) screen_return_to(s_return);
}

static void on_mode_row_clicked(lv_event_t *e)
{
    s_mode = (MusicBrowse)(intptr_t)lv_event_get_user_data(e);
    if (s_mode == MUSIC_BY_TRACK) {
        tracks_screen_build(0);               // one implicit group holding everything
        lv_scr_load(s_tracks_scr);
    } else {
        groups_screen_build();
        lv_scr_load(s_groups_scr);
    }
}

static void mode_screen_build()
{
    s_mode_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_mode_scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(s_mode_scr, LV_OBJ_FLAG_SCROLLABLE);
    make_title(s_mode_scr, "MUSIC");
    lv_obj_t *box = make_list_box(s_mode_scr);
    make_row(box, "By artist", on_mode_row_clicked, MUSIC_BY_ARTIST);
    make_row(box, "By album",  on_mode_row_clicked, MUSIC_BY_ALBUM);
    make_row(box, "By track",  on_mode_row_clicked, MUSIC_BY_TRACK);
    lv_obj_add_event_cb(s_mode_scr, on_mode_gesture, LV_EVENT_GESTURE, NULL);
}

// ---- Groups screen (artists or albums) -----------------------------------
static void on_groups_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_event_get_indev(e));
    if (dir == LV_DIR_TOP || dir == LV_DIR_RIGHT) lv_scr_load(s_mode_scr);
}

static void on_group_row_clicked(lv_event_t *e)
{
    int g = (int)(intptr_t)lv_event_get_user_data(e);
    tracks_screen_build(g);
    lv_scr_load(s_tracks_scr);
}

static void groups_screen_build()
{
    if (!s_groups_scr) {
        s_groups_scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_groups_scr, lv_color_black(), LV_PART_MAIN);
        lv_obj_clear_flag(s_groups_scr, LV_OBJ_FLAG_SCROLLABLE);
        s_groups_title = make_title(s_groups_scr, "");
        s_groups_list  = make_list_box(s_groups_scr);
        lv_obj_add_event_cb(s_groups_scr, on_groups_gesture, LV_EVENT_GESTURE, NULL);
    }
    lv_label_set_text(s_groups_title, s_mode == MUSIC_BY_ALBUM ? "ALBUMS" : "ARTISTS");
    lv_obj_clean(s_groups_list);
    int n = music_lib_group_count(s_mode);
    if (n == 0) {
        make_empty_msg(s_groups_list, "No music found.\n\nCopy files to /music/ on the SD card.");
        return;
    }
    const char *placeholder = s_mode == MUSIC_BY_ALBUM ? "Unknown album" : "Unknown artist";
    for (int i = 0; i < n; i++) {
        const char *name = music_lib_group_name(s_mode, i);
        make_row(s_groups_list, (name && *name) ? name : placeholder, on_group_row_clicked, i);
    }
}

// ---- Tracks screen ---------------------------------------------------------
static void on_tracks_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_event_get_indev(e));
    if (dir == LV_DIR_TOP || dir == LV_DIR_RIGHT)
        lv_scr_load(s_mode == MUSIC_BY_TRACK ? s_mode_scr : s_groups_scr);
}

static void show_track_error(const char *msg)
{
    if (!s_tracks_status) return;
    lv_label_set_text(s_tracks_status, msg ? msg : "");
}

static void on_track_row_clicked(lv_event_t *e)
{
    int pos = (int)(intptr_t)lv_event_get_user_data(e);
    show_track_error("");
    if (music_player_play(s_mode, s_group, pos)) {
        now_playing_screen_build();
        lv_scr_load(s_playing_scr);
    } else {
        const char *why = music_player_last_error();
        show_track_error(*why ? why : "Could not start the track");
    }
}

static void tracks_screen_build(int group)
{
    s_group = group;

    if (!s_tracks_scr) {
        s_tracks_scr = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(s_tracks_scr, lv_color_black(), LV_PART_MAIN);
        lv_obj_clear_flag(s_tracks_scr, LV_OBJ_FLAG_SCROLLABLE);
        s_tracks_title = make_title(s_tracks_scr, "");
        s_tracks_list  = make_list_box(s_tracks_scr);
        s_tracks_status = lv_label_create(s_tracks_scr);
        lv_obj_set_style_text_font(s_tracks_status, theme_text_font(16), LV_PART_MAIN);
        lv_obj_set_style_text_color(s_tracks_status, NR, LV_PART_MAIN);
        lv_obj_set_style_text_align(s_tracks_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(s_tracks_status, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(s_tracks_status, 300);
        lv_label_set_text(s_tracks_status, "");
        lv_obj_align(s_tracks_status, LV_ALIGN_TOP_MID, 0, 402);
        lv_obj_add_event_cb(s_tracks_scr, on_tracks_gesture, LV_EVENT_GESTURE, NULL);
    }

    const char *gname = s_mode == MUSIC_BY_TRACK ? "ALL TRACKS" : music_lib_group_name(s_mode, group);
    lv_label_set_text(s_tracks_title, (gname && *gname) ? gname : "TRACKS");
    show_track_error("");
    lv_obj_clean(s_tracks_list);
    int n = music_lib_group_track_count(s_mode, group);
    if (n == 0) { make_empty_msg(s_tracks_list, "No tracks."); return; }
    for (int i = 0; i < n; i++) {
        int gi = music_lib_group_track(s_mode, group, i);
        const MusicTrack *t = music_lib_track(gi);
        make_row(s_tracks_list, t ? t->title : "?", on_track_row_clicked, i);
    }
}

// ---- Now Playing screen -----------------------------------------------------
static void on_playing_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_event_get_indev(e));
    // Back leaves music PLAYING in the background - only Eject stops it.
    if (dir == LV_DIR_TOP || dir == LV_DIR_RIGHT) lv_scr_load(s_tracks_scr);
}

static void on_playpause_clicked(lv_event_t *)
{
    if (music_player_is_playing())      music_player_pause();
    else if (music_player_is_paused())  music_player_resume();
}

static void on_next_clicked(lv_event_t *) { music_player_next(); }
static void on_prev_clicked(lv_event_t *) { music_player_prev(); }
static void on_volume_changed(lv_event_t *e)
{
    music_player_set_volume(lv_slider_get_value(lv_event_get_target_obj(e)));
}

static constexpr int CAS_X = 34, CAS_Y = 78, CAS_W = 342, CAS_H = 216;
static constexpr int REEL_L_CX = 52, REEL_R_CX = 198, REEL_CY = 39;
static constexpr int PACK_MAX = 44, PACK_MIN = 26;

static lv_obj_t *s_np_total;
static lv_obj_t *s_np_pack_l, *s_np_pack_r;
static lv_obj_t *s_np_spokes[6];
static lv_timer_t *s_reel_timer = nullptr;
static int s_reel_angle = 0;

static void on_eject_clicked(lv_event_t *)
{
    music_player_stop();
    lv_scr_load(s_tracks_scr);
}

static void on_np_tick(lv_timer_t *)
{
    if (!s_playing_scr || lv_screen_active() != s_playing_scr) return;
    if (!music_player_is_playing() && !music_player_is_paused()) {
        show_track_error(music_player_last_error());
        lv_scr_load(s_tracks_scr);
        return;
    }
    float pos = music_player_position_s(), dur = music_player_duration_s();
    lv_bar_set_range(s_np_bar, 0, dur > 0 ? (int32_t)dur : 1);
    lv_bar_set_value(s_np_bar, (int32_t)pos, LV_ANIM_OFF);
    char cur[16], tot[16];
    fmt_time(pos, cur, sizeof(cur));
    fmt_time(dur, tot, sizeof(tot));
    lv_label_set_text(s_np_time, cur);
    lv_label_set_text(s_np_total, tot);
    lv_label_set_text(s_np_playpause_label,
        music_player_is_playing() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    float p = dur > 0 ? pos / dur : 0.0f;
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    int left  = PACK_MAX - (int)((PACK_MAX - PACK_MIN) * p);
    int right = PACK_MIN + (int)((PACK_MAX - PACK_MIN) * p);
    lv_obj_set_size(s_np_pack_l, left, left);
    lv_obj_set_pos(s_np_pack_l, REEL_L_CX - left / 2, REEL_CY - left / 2);
    lv_obj_set_size(s_np_pack_r, right, right);
    lv_obj_set_pos(s_np_pack_r, REEL_R_CX - right / 2, REEL_CY - right / 2);
}

static void on_reel_tick(lv_timer_t *)
{
    if (!s_playing_scr || lv_screen_active() != s_playing_scr) return;
    if (!music_player_is_playing()) return;
    s_reel_angle = (s_reel_angle + 8) % 360;
    for (int i = 0; i < 6; i++)
        lv_arc_set_rotation(s_np_spokes[i], (i % 3) * 120 + s_reel_angle);
}

static void make_reel(lv_obj_t *win, int cx, lv_obj_t **pack, lv_obj_t **spokes)
{
    lv_obj_t *pk = lv_obj_create(win);
    lv_obj_remove_style_all(pk);
    lv_obj_set_size(pk, PACK_MAX, PACK_MAX);
    lv_obj_set_pos(pk, cx - PACK_MAX / 2, REEL_CY - PACK_MAX / 2);
    lv_obj_set_style_radius(pk, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pk, ARGUS_ACCENT_DIM, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pk, LV_OPA_COVER, LV_PART_MAIN);
    *pack = pk;

    lv_obj_t *ring = lv_obj_create(win);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 48, 48);
    lv_obj_set_pos(ring, cx - 24, REEL_CY - 24);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_color(ring, ARGUS_CREAM, LV_PART_MAIN);
    lv_obj_set_style_border_width(ring, 3, LV_PART_MAIN);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *sp = lv_arc_create(win);
        lv_obj_remove_style(sp, NULL, LV_PART_KNOB);
        lv_obj_remove_flag(sp, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(sp, 38, 38);
        lv_obj_set_pos(sp, cx - 19, REEL_CY - 19);
        lv_arc_set_bg_angles(sp, 0, 34);
        lv_arc_set_rotation(sp, i * 120);
        lv_obj_set_style_arc_width(sp, 7, LV_PART_MAIN);
        lv_obj_set_style_arc_color(sp, ARGUS_CREAM, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(sp, false, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(sp, LV_OPA_TRANSP, LV_PART_INDICATOR);
        spokes[i] = sp;
    }
    lv_obj_t *hub = lv_obj_create(win);
    lv_obj_remove_style_all(hub);
    lv_obj_set_size(hub, 12, 12);
    lv_obj_set_pos(hub, cx - 6, REEL_CY - 6);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hub, ARGUS_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(hub, ARGUS_CREAM, LV_PART_MAIN);
    lv_obj_set_style_border_width(hub, 2, LV_PART_MAIN);
}

static lv_obj_t *make_transport(lv_obj_t *parent, int x, int y, int size, int radius,
                                lv_color_t bg, lv_color_t fg, const char *symbol,
                                lv_event_cb_t cb, lv_obj_t **label_out)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, size, size);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, bg, LV_PART_MAIN);
    lv_obj_set_style_radius(b, radius, LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(b, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, fg, LV_PART_MAIN);
    lv_label_set_text(l, symbol);
    lv_obj_center(l);
    if (label_out) *label_out = l;
    return b;
}

static void now_playing_screen_build()
{
    if (s_playing_scr) {
        lv_label_set_text(s_np_title, music_player_current_title());
        lv_label_set_text(s_np_artist, music_player_current_artist());
        return;
    }

    s_playing_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_playing_scr, ARGUS_BG, LV_PART_MAIN);
    lv_obj_clear_flag(s_playing_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *hdr = lv_label_create(s_playing_scr);
    lv_obj_set_style_text_font(hdr, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(hdr, ARGUS_QUIET, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(hdr, 2, LV_PART_MAIN);
    lv_label_set_text(hdr, "NOW PLAYING");
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 34);

    lv_obj_t *cas = lv_obj_create(s_playing_scr);
    lv_obj_remove_style_all(cas);
    lv_obj_set_size(cas, CAS_W, CAS_H);
    lv_obj_set_pos(cas, CAS_X, CAS_Y);
    lv_obj_set_style_radius(cas, 30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cas, lv_color_make(0x1A, 0x1A, 0x1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cas, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(cas, lv_color_make(0x34, 0x34, 0x34), LV_PART_MAIN);
    lv_obj_set_style_border_width(cas, 2, LV_PART_MAIN);
    lv_obj_clear_flag(cas, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sticker = lv_obj_create(cas);
    lv_obj_remove_style_all(sticker);
    lv_obj_set_size(sticker, 298, 74);
    lv_obj_set_pos(sticker, 18, 12);
    lv_obj_set_style_radius(sticker, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sticker, ARGUS_CREAM, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sticker, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(sticker, true, LV_PART_MAIN);
    lv_obj_clear_flag(sticker, LV_OBJ_FLAG_SCROLLABLE);

    s_np_title = lv_label_create(sticker);
    lv_obj_set_style_text_font(s_np_title, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_np_title, lv_color_black(), LV_PART_MAIN);
    lv_label_set_long_mode(s_np_title, LV_LABEL_LONG_SCROLL_CIRCULAR);   // iPod-style roll
    lv_obj_set_width(s_np_title, 230);
    lv_label_set_text(s_np_title, music_player_current_title());
    lv_obj_set_pos(s_np_title, 14, 8);

    s_np_artist = lv_label_create(sticker);
    lv_obj_set_style_text_font(s_np_artist, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_np_artist, lv_color_make(0x55, 0x55, 0x55), LV_PART_MAIN);
    lv_label_set_long_mode(s_np_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_np_artist, 230);
    lv_label_set_text(s_np_artist, music_player_current_artist());
    lv_obj_set_pos(s_np_artist, 14, 38);

    lv_obj_t *chip = lv_obj_create(sticker);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 34, 12);
    lv_obj_set_pos(chip, 298 - 14 - 34, 12);
    lv_obj_set_style_radius(chip, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(chip, ARGUS_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *win = lv_obj_create(cas);
    lv_obj_remove_style_all(win);
    lv_obj_set_size(win, 254, 82);
    lv_obj_set_pos(win, 40, 98);
    lv_obj_set_style_radius(win, 41, LV_PART_MAIN);
    lv_obj_set_style_bg_color(win, lv_color_make(0x05, 0x05, 0x05), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_make(0x3A, 0x3A, 0x3A), LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 2; i++) {
        lv_obj_t *run = lv_obj_create(win);
        lv_obj_remove_style_all(run);
        lv_obj_set_size(run, REEL_R_CX - REEL_L_CX, 3);
        lv_obj_set_pos(run, REEL_L_CX, i == 0 ? 12 : 63);
        lv_obj_set_style_radius(run, 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(run, ARGUS_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(run, LV_OPA_COVER, LV_PART_MAIN);
    }
    make_reel(win, REEL_L_CX, &s_np_pack_l, &s_np_spokes[0]);
    make_reel(win, REEL_R_CX, &s_np_pack_r, &s_np_spokes[3]);

    lv_obj_t *eject = lv_button_create(cas);
    lv_obj_set_size(eject, 92, 24);
    lv_obj_set_pos(eject, (CAS_W - 4 - 92) / 2, 184);
    lv_obj_set_style_bg_color(eject, ARGUS_RAISED, LV_PART_MAIN);
    lv_obj_set_style_radius(eject, ARGUS_R_PILL, LV_PART_MAIN);
    lv_obj_set_style_border_width(eject, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(eject, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(eject, 10);
    lv_obj_add_event_cb(eject, on_eject_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *eject_l = lv_label_create(eject);
    lv_obj_set_style_text_font(eject_l, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(eject_l, ARGUS_QUIET, LV_PART_MAIN);
    lv_label_set_text(eject_l, "EJECT");
    lv_obj_center(eject_l);

    // ---- progress ----
    s_np_bar = lv_bar_create(s_playing_scr);
    lv_obj_set_size(s_np_bar, 322, 16);
    lv_obj_set_pos(s_np_bar, 44, 306);
    lv_obj_set_style_bg_color(s_np_bar, ARGUS_RAISED, LV_PART_MAIN);
    lv_obj_set_style_radius(s_np_bar, ARGUS_R_PILL, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_np_bar, ARGUS_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_np_bar, ARGUS_R_PILL, LV_PART_INDICATOR);

    s_np_time = lv_label_create(s_playing_scr);
    lv_obj_set_style_text_font(s_np_time, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_np_time, ARGUS_QUIET, LV_PART_MAIN);
    lv_label_set_text(s_np_time, "0:00");
    lv_obj_set_pos(s_np_time, 46, 328);

    s_np_total = lv_label_create(s_playing_scr);
    lv_obj_set_style_text_font(s_np_total, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_np_total, ARGUS_QUIET, LV_PART_MAIN);
    lv_label_set_text(s_np_total, "0:00");
    lv_obj_align(s_np_total, LV_ALIGN_TOP_RIGHT, -46, 328);

    // ---- transport: prev - play/pause - next ----
    make_transport(s_playing_scr, 46, 360, 80, 28, ARGUS_RAISED, ARGUS_CREAM,
                   LV_SYMBOL_PREV, on_prev_clicked, nullptr);
    make_transport(s_playing_scr, 161, 354, 92, ARGUS_R_PILL, ARGUS_ACCENT, lv_color_black(),
                   LV_SYMBOL_PAUSE, on_playpause_clicked, &s_np_playpause_label);
    make_transport(s_playing_scr, 294, 360, 80, 28, ARGUS_RAISED, ARGUS_CREAM,
                   LV_SYMBOL_NEXT, on_next_clicked, nullptr);

    // ---- volume (software; MAX98357A has no HW volume) ----
    lv_obj_t *vol_ico = lv_label_create(s_playing_scr);
    lv_obj_set_style_text_font(vol_ico, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(vol_ico, ARGUS_QUIET, LV_PART_MAIN);
    lv_label_set_text(vol_ico, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_pos(vol_ico, 44, 462);

    lv_obj_t *vol = lv_slider_create(s_playing_scr);
    lv_obj_set_size(vol, 280, 12);
    lv_obj_set_pos(vol, 80, 464);
    lv_slider_set_range(vol, 0, 100);
    lv_slider_set_value(vol, music_player_get_volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(vol, ARGUS_RAISED, LV_PART_MAIN);
    lv_obj_set_style_bg_color(vol, ARGUS_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(vol, ARGUS_CREAM, LV_PART_KNOB);
    lv_obj_clear_flag(vol, LV_OBJ_FLAG_GESTURE_BUBBLE);   // horizontal drag isn't a back swipe
    lv_obj_add_event_cb(vol, on_volume_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_add_event_cb(s_playing_scr, on_playing_gesture, LV_EVENT_GESTURE, NULL);

    if (!s_np_timer)   s_np_timer   = lv_timer_create(on_np_tick, 500, NULL);
    if (!s_reel_timer) s_reel_timer = lv_timer_create(on_reel_tick, 120, NULL);
}

// ---- public ------------------------------------------------------------------
void music_screen_show()
{
    if (!s_mode_scr) mode_screen_build();

    lv_obj_t *from = lv_screen_active();
    if (from != s_mode_scr && from != s_groups_scr && from != s_tracks_scr && from != s_playing_scr)
        s_return = from;

    if (!music_lib_is_scanned()) music_lib_scan();
    lv_scr_load(s_mode_scr);
}

bool music_screen_is_active()
{
    lv_obj_t *cur = lv_screen_active();
    return cur == s_mode_scr || cur == s_groups_scr || cur == s_tracks_scr || cur == s_playing_scr;
}
