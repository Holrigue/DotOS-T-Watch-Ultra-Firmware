// presence_screen.cpp - see presence_screen.h.
//
// Radar visual for the nearby-human BLE detector. A green sweep line rotates
// over three range rings; human_detector_snapshot() gives the devices in the
// rolling window, each drawn as a blip at a radius from its RSSI (stronger =
// nearer the centre) and a stable per-MAC angle. Blips brighten as the sweep
// passes and fade to a dim baseline, the classic radar afterglow.
//
// Everything here is plain LVGL objects (rings/blips are circular lv_objs, the
// sweep is a thin rotated rectangle) - no canvas - so it is simple and certain
// to compile. The blips are a fixed pool sized to the detector's table; each
// poll we place the active ones and hide the rest.
#include "presence_screen.h"
#include "human_detector.h"
#include "theme.h"

#include <LilyGoLib.h>
#include <math.h>

// Defined in main.cpp.
void screen_return_to(lv_obj_t *scr);

static const lv_color_t PW = lv_color_hex(0xFFFFFF);   // primary text
static const lv_color_t PG = lv_color_hex(0x9A9A9A);   // secondary text
static const lv_color_t PGRID = lv_color_make(0x30, 0x30, 0x30);
static const lv_color_t PGREEN = lv_color_make(0x2A, 0xE0, 0x60);

#define RADAR_R    130           // outer ring radius, px
#define RADAR_CY   (-6)          // radar centre offset below screen centre
#define BLIP_MAX   24            // must be >= human_detector's nearby table
#define BLIP_SIZE  16
static const float PRES_PI = 3.14159265358979f;

static lv_obj_t   *screen       = nullptr;
static lv_obj_t   *s_return     = nullptr;
static lv_obj_t   *s_sweep      = nullptr;
static lv_obj_t   *s_blips[BLIP_MAX];
static lv_obj_t   *s_count_lbl  = nullptr;
static lv_obj_t   *s_status_lbl = nullptr;
static lv_timer_t *s_poll_timer = nullptr;
static int         s_sweep_deg  = 0;
static bool        s_we_started = false;   // did WE turn the detector on?

// RSSI -> radius: strong (-40) hugs the centre, weak (-90) sits at the rim.
static int rssi_to_radius(int8_t rssi)
{
    int r = rssi;
    if (r > -40) r = -40;
    if (r < -90) r = -90;
    float frac = (float)(r + 40) / -50.0f;    // -40 -> 0.0, -90 -> 1.0
    return (int)(16.0f + frac * (float)(RADAR_R - 6 - 16));
}

static void make_ring(int radius)
{
    lv_obj_t *ring = lv_obj_create(screen);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, radius * 2, radius * 2);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, RADAR_CY);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_color(ring, PGRID, LV_PART_MAIN);
    lv_obj_set_style_border_width(ring, 2, LV_PART_MAIN);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
}

static void on_poll(lv_timer_t *)
{
    // Advance the sweep.
    s_sweep_deg = (s_sweep_deg + 5) % 360;
    if (s_sweep) {
        int16_t rot = (int16_t)(s_sweep_deg * 10);   // 0.1deg units, clockwise
        lv_obj_set_style_transform_rotation(s_sweep, rot, LV_PART_MAIN);
    }

    bool running = human_detector_is_running();
    HumanBlip blips[BLIP_MAX];
    int n = running ? human_detector_snapshot(blips, BLIP_MAX) : 0;

    for (int i = 0; i < BLIP_MAX; i++) {
        if (i >= n) { lv_obj_add_flag(s_blips[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_clear_flag(s_blips[i], LV_OBJ_FLAG_HIDDEN);

        int   r   = rssi_to_radius(blips[i].rssi);
        float th  = (float)blips[i].angle_deg * PRES_PI / 180.0f;
        int   dx  = (int)(r * sinf(th));          // 0 deg = up (north)
        int   dy  = (int)(-r * cosf(th));
        lv_obj_align(s_blips[i], LV_ALIGN_CENTER, dx, RADAR_CY + dy);

        // Afterglow: brightest just after the sweep passes the blip's angle,
        // fading round to a dim baseline.
        int trail = (s_sweep_deg - (int)blips[i].angle_deg + 360) % 360;
        lv_opa_t opa = (trail < 90) ? (lv_opa_t)(255 - (trail * 180 / 90)) : (lv_opa_t)70;
        lv_obj_set_style_bg_opa(s_blips[i], opa, LV_PART_MAIN);
    }

    if (s_count_lbl) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%d nearby", n);
        lv_label_set_text(s_count_lbl, buf);
    }
    if (s_status_lbl)
        lv_label_set_text(s_status_lbl, running ? "scanning \xE2\x80\xA2 no bearing"
                                                : "BLE unavailable");
}

static void on_unload(lv_event_t *)
{
    if (s_poll_timer) { lv_timer_del(s_poll_timer); s_poll_timer = nullptr; }
    if (s_we_started) {          // only stop what we started
        human_detector_stop();
        s_we_started = false;
    }
}

static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_TOP || dir == LV_DIR_RIGHT) screen_return_to(s_return);
}

static void build()
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_font(title, &font_argus_label_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, PG, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(title, 3, LV_PART_MAIN);
    lv_label_set_text(title, "PRESENCE");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);

    make_ring(RADAR_R);
    make_ring(RADAR_R * 2 / 3);
    make_ring(RADAR_R / 3);

    // Centre "you" dot.
    lv_obj_t *me = lv_obj_create(screen);
    lv_obj_remove_style_all(me);
    lv_obj_set_size(me, 10, 10);
    lv_obj_align(me, LV_ALIGN_CENTER, 0, RADAR_CY);
    lv_obj_set_style_radius(me, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(me, PW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(me, LV_OPA_COVER, LV_PART_MAIN);

    // Sweep: a thin bar pivoted at the radar centre, rotated in on_poll(). Its
    // top end sits on the centre and it extends up; pivot (1,0) = top-centre.
    s_sweep = lv_obj_create(screen);
    lv_obj_remove_style_all(s_sweep);
    lv_obj_set_size(s_sweep, 3, RADAR_R);
    lv_obj_align(s_sweep, LV_ALIGN_CENTER, 0, RADAR_CY + RADAR_R / 2);
    lv_obj_set_style_bg_color(s_sweep, PGREEN, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sweep, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(s_sweep, 1, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(s_sweep, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_sweep, LV_OBJ_FLAG_SCROLLABLE);

    // Blip pool, all hidden until on_poll() places the active ones.
    for (int i = 0; i < BLIP_MAX; i++) {
        s_blips[i] = lv_obj_create(screen);
        lv_obj_remove_style_all(s_blips[i]);
        lv_obj_set_size(s_blips[i], BLIP_SIZE, BLIP_SIZE);
        lv_obj_set_style_radius(s_blips[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_blips[i], PGREEN, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_blips[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(s_blips[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_blips[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    s_count_lbl = lv_label_create(screen);
    lv_obj_set_style_text_font(s_count_lbl, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_count_lbl, PW, LV_PART_MAIN);
    lv_label_set_text(s_count_lbl, "0 nearby");
    lv_obj_align(s_count_lbl, LV_ALIGN_BOTTOM_MID, 0, -46);

    s_status_lbl = lv_label_create(screen);
    lv_obj_set_style_text_font(s_status_lbl, &font_argus_label_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_lbl, PG, LV_PART_MAIN);
    lv_label_set_text(s_status_lbl, "scanning \xE2\x80\xA2 no bearing");
    lv_obj_align(s_status_lbl, LV_ALIGN_BOTTOM_MID, 0, -18);

    lv_obj_add_event_cb(screen, on_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(screen, on_unload, LV_EVENT_SCREEN_UNLOAD_START, NULL);
}

void presence_screen_show()
{
    if (!screen) build();

    lv_obj_t *from = lv_screen_active();
    if (from != screen) s_return = from;

    // Start the detector for the visual, remembering whether we were the one to
    // start it so on_unload only stops what it started.
    s_we_started = false;
    if (!human_detector_is_running())
        s_we_started = human_detector_start();

    if (!s_poll_timer) s_poll_timer = lv_timer_create(on_poll, 50, NULL);

    lv_scr_load(screen);
}

bool presence_screen_is_active() { return lv_screen_active() == screen; }
