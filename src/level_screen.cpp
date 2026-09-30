// level_screen.cpp - digital bubble level (spirit level).
//
// Uses the BHI260AP accelerometer (the gravity vector) rather than the compass's
// gyro rotation-vector: gravity is absolute and does not drift, which is exactly
// what a level needs. The raw accel stream is the SAME one motion-wake uses, so
// we ride it through main.cpp's shared clock_accel_hold()/clock_accel_get() API
// instead of opening a second SensorXYZ (that would fight motion-wake over the
// single ACCEL_PASSTHROUGH virtual sensor).
//
// Two surfaces in one:
//   - a bullseye (the moving bubble) for levelling a flat surface in 2 axes;
//   - roll / pitch angle read-outs in degrees.
// The bubble turns green and the status reads LEVEL when both axes are within
// the tolerance. The sensor hold + wake-lock are taken on show and released on
// SCREEN_UNLOAD_START so any exit path tears them down.
#include "level_screen.h"
#include "theme.h"

#include <lvgl.h>
#include <math.h>
#include <stdio.h>

// From main.cpp.
void screen_return_to(lv_obj_t *scr);
void ui_keep_awake(bool on);
void clock_accel_hold(bool on);
bool clock_accel_get(float *x, float *y, float *z);

static const lv_color_t NW = lv_color_hex(0xFFFFFF);   // primary text / bubble
static const lv_color_t NG = lv_color_hex(0x9A9A9A);   // secondary text
static const lv_color_t GRN = lv_color_hex(0x22C063);  // level / in tolerance

// Geometry (screen is 410x502; the dial is centred a little above middle).
static constexpr int   DIAL_CY      = -20;    // dial centre y-offset from screen centre
static constexpr int   OUTER_R      = 150;    // outer ring radius (px)
static constexpr int   TOL_R        = 30;     // centre tolerance ring radius (px)
static constexpr int   BUB_D        = 46;     // bubble diameter (px)
static constexpr int   TRAVEL_R     = OUTER_R - BUB_D / 2 - 4;   // bubble centre travel limit
static constexpr float FULLSCALE_DEG = 22.0f; // tilt that pushes the bubble to the edge
static constexpr float LEVEL_TOL_DEG = 0.6f;  // "LEVEL" when both axes within this
static constexpr float EMA_A        = 0.30f;  // low-pass smoothing on the tilt

static lv_obj_t   *screen        = nullptr;
static lv_obj_t   *s_return      = nullptr;
static lv_obj_t   *s_bubble      = nullptr;
static lv_obj_t   *s_tol_ring    = nullptr;
static lv_obj_t   *s_status      = nullptr;
static lv_obj_t   *s_roll_lbl    = nullptr;
static lv_obj_t   *s_pitch_lbl   = nullptr;
static lv_timer_t *s_poll_timer  = nullptr;

static float s_f_roll = 0.0f, s_f_pitch = 0.0f;   // smoothed angles (deg)
static bool  s_seeded = false;

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void on_poll(lv_timer_t *)
{
    float x, y, z;
    if (!clock_accel_get(&x, &y, &z)) return;

    float mag = sqrtf(x * x + y * y + z * z);
    if (mag < 1e-4f) return;
    float gx = x / mag, gy = y / mag, gz = z / mag;

    // Tilt of the screen plane about each axis, from the gravity direction.
    // roll  = left/right tilt, pitch = front/back tilt. If the bubble moves the
    // wrong way on hardware, flip the sign on the matching line below.
    float roll  = atan2f(gx, gz) * 57.2957795f;
    float pitch = atan2f(gy, gz) * 57.2957795f;

    if (!s_seeded) { s_f_roll = roll; s_f_pitch = pitch; s_seeded = true; }
    else {
        s_f_roll  += EMA_A * (roll  - s_f_roll);
        s_f_pitch += EMA_A * (pitch - s_f_pitch);
    }

    // Bubble position: proportional to tilt, clamped to the ring. The bubble
    // rides toward the raised edge (air-bubble behaviour).
    float px = clampf(s_f_roll  / FULLSCALE_DEG, -1.0f, 1.0f) * TRAVEL_R;
    float py = clampf(-s_f_pitch / FULLSCALE_DEG, -1.0f, 1.0f) * TRAVEL_R;
    if (s_bubble) lv_obj_align(s_bubble, LV_ALIGN_CENTER, (int)(px + 0.5f), DIAL_CY + (int)(py + 0.5f));

    bool level = fabsf(s_f_roll) < LEVEL_TOL_DEG && fabsf(s_f_pitch) < LEVEL_TOL_DEG;
    lv_color_t c = level ? GRN : NW;
    if (s_bubble)   lv_obj_set_style_bg_color(s_bubble, c, LV_PART_MAIN);
    if (s_tol_ring) lv_obj_set_style_border_color(s_tol_ring, level ? GRN : lv_color_hex(0x444444), LV_PART_MAIN);
    if (s_status) {
        lv_label_set_text(s_status, level ? "LEVEL" : "");
        lv_obj_set_style_text_color(s_status, GRN, LV_PART_MAIN);
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "X %+.1f\xC2\xB0", (double)s_f_roll);
    if (s_roll_lbl) lv_label_set_text(s_roll_lbl, buf);
    snprintf(buf, sizeof(buf), "Y %+.1f\xC2\xB0", (double)s_f_pitch);
    if (s_pitch_lbl) lv_label_set_text(s_pitch_lbl, buf);
}

static void on_unload(lv_event_t *)
{
    clock_accel_hold(false);
    ui_keep_awake(false);
    if (s_poll_timer) { lv_timer_del(s_poll_timer); s_poll_timer = nullptr; }
}

static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) screen_return_to(s_return);
}

static lv_obj_t *ring(lv_obj_t *parent, int d, lv_color_t col, int border)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(o, col, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, border, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void build()
{
    screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(screen);
    lv_obj_set_style_text_font(title, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, NG, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(title, 3, LV_PART_MAIN);
    lv_label_set_text(title, "LEVEL");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 26);

    // Fixed outer ring + centre crosshair, and a centre tolerance ring the bubble
    // sits inside when level.
    lv_obj_t *outer = ring(screen, OUTER_R * 2, lv_color_hex(0x333333), 2);
    lv_obj_align(outer, LV_ALIGN_CENTER, 0, DIAL_CY);

    s_tol_ring = ring(screen, TOL_R * 2, lv_color_hex(0x444444), 2);
    lv_obj_align(s_tol_ring, LV_ALIGN_CENTER, 0, DIAL_CY);

    // Thin crosshair through the centre (two 1px rectangles).
    lv_obj_t *hbar = lv_obj_create(screen);
    lv_obj_remove_style_all(hbar);
    lv_obj_set_size(hbar, OUTER_R * 2, 1);
    lv_obj_set_style_bg_color(hbar, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hbar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(hbar, LV_ALIGN_CENTER, 0, DIAL_CY);
    lv_obj_t *vbar = lv_obj_create(screen);
    lv_obj_remove_style_all(vbar);
    lv_obj_set_size(vbar, 1, OUTER_R * 2);
    lv_obj_set_style_bg_color(vbar, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(vbar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(vbar, LV_ALIGN_CENTER, 0, DIAL_CY);

    // The bubble.
    s_bubble = lv_obj_create(screen);
    lv_obj_remove_style_all(s_bubble);
    lv_obj_set_size(s_bubble, BUB_D, BUB_D);
    lv_obj_set_style_radius(s_bubble, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bubble, NW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bubble, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_bubble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_bubble, LV_ALIGN_CENTER, 0, DIAL_CY);

    // LEVEL status sits inside the ring, above centre.
    s_status = lv_label_create(screen);
    lv_obj_set_style_text_font(s_status, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status, GRN, LV_PART_MAIN);
    lv_label_set_text(s_status, "");
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, DIAL_CY - OUTER_R + 22);

    // Angle read-outs below the dial.
    s_roll_lbl = lv_label_create(screen);
    lv_obj_set_style_text_font(s_roll_lbl, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_roll_lbl, NW, LV_PART_MAIN);
    lv_label_set_text(s_roll_lbl, "X --");
    lv_obj_align(s_roll_lbl, LV_ALIGN_BOTTOM_MID, -80, -28);

    s_pitch_lbl = lv_label_create(screen);
    lv_obj_set_style_text_font(s_pitch_lbl, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_pitch_lbl, NW, LV_PART_MAIN);
    lv_label_set_text(s_pitch_lbl, "Y --");
    lv_obj_align(s_pitch_lbl, LV_ALIGN_BOTTOM_MID, 80, -28);

    lv_obj_add_event_cb(screen, on_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(screen, on_unload, LV_EVENT_SCREEN_UNLOAD_START, NULL);
}

void level_screen_show()
{
    if (!screen) build();

    lv_obj_t *from = lv_screen_active();
    if (from != screen) s_return = from;

    s_seeded = false;
    clock_accel_hold(true);   // keep the accel stream up while we're open
    ui_keep_awake(true);
    if (!s_poll_timer) s_poll_timer = lv_timer_create(on_poll, 60, NULL);

    lv_scr_load(screen);
}

bool level_screen_is_active() { return lv_screen_active() == screen; }
