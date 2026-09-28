// compass_screen.cpp - see compass_screen.h.
//
// RELATIVE heading screen. The T-Watch Ultra's BHI260AP is a 6DoF IMU
// (accel + gyro) with NO magnetometer - the vendored sensor examples are
// literally named BHI260AP_6DoF, and there is no BMM150/mag example - so a
// true magnetic-north compass is physically impossible on this board. The
// magnetometer-fused ROTATION_VECTOR virtual sensor produces no data here
// (the earlier version used it, which is why the needle never moved).
//
// Instead we use GAME_ROTATION_VECTOR (accel+gyro only, the same virtual
// sensor the motion-wake path and the vendored Euler example use, and the
// one that actually reports on this hardware). Its yaw is RELATIVE to an
// arbitrary zero at sensor start and drifts slowly over minutes (no
// magnetometer to correct it). The user presses SET NORTH to zero the dial
// against a known direction, then re-presses whenever drift accumulates.
//
// The pointer is a red navigation triangle (an arrowhead) centred on the
// dial that swings to keep pointing at the pinned north as the wrist turns.
// It is drawn on a small transparent ARGB canvas: on each poll we clear it
// and re-fill the rotated triangle by a point-in-triangle test, which uses
// only lv_canvas_fill_bg/lv_canvas_set_px (the APIs the vendored canvas
// example uses) so it is certain to compile against this LVGL, rather than
// relying on image-transform-on-canvas behaviour.
//
// The sensor is powered on only while this screen is shown (enabled on show,
// torn down on SCREEN_UNLOAD_START so any exit path stops it), mirroring
// main.cpp's motion-wake enable()/disable() pattern.
#include "compass_screen.h"
#include "theme.h"

#include <LilyGoLib.h>
#include <math.h>

// Defined in main.cpp.
void screen_return_to(lv_obj_t *scr);

static const lv_color_t NW = lv_color_hex(0xFFFFFF);   // primary text
static const lv_color_t NG = lv_color_hex(0x9A9A9A);   // secondary text
static const lv_color_t NR = lv_color_hex(0xE02020);   // accent red

// Navigation-triangle canvas. Square so it can hold the arrow at any rotation.
// Centred coords: apex points "up" (toward the N mark) at rest; on_poll rotates
// it by -heading so it stays aimed at the pinned north as the wrist turns.
#define ARROW_CV  64                 // canvas is ARROW_CV x ARROW_CV px
#define ARROW_C   (ARROW_CV / 2.0f)  // canvas centre (also the rotation pivot)
static const float ARROW_PI = 3.14159265358979f;

// Arrow shape in centre-relative coords (y grows downward, apex is negative y).
// Max reach from centre is the apex at 30px < ARROW_C (32), so no rotation ever
// pushes a vertex outside the canvas.
static const float ARROW_AX = 0.0f,   ARROW_AY = -30.0f;   // apex
static const float ARROW_LX = -17.0f, ARROW_LY = 18.0f;    // base left
static const float ARROW_RX = 17.0f,  ARROW_RY = 18.0f;    // base right

namespace {

volatile bool  s_have_heading  = false;
volatile float s_raw_yaw       = 0.0f;    // 0..360, relative to sensor-start zero
float          s_north_offset  = 0.0f;    // yaw the user pinned as "north" (SET NORTH)
bool           s_sensor_started = false;

// SensorDataParseCallback (SensorLib 0.3.4): void(uint8_t, uint8_t*, uint32_t,
// uint64_t*, void*) - non-const data pointer (its own .ino examples match).
void on_rotation_vector(uint8_t sensor_id, uint8_t *data, uint32_t size,
                         uint64_t *timestamp, void *user_data)
{
    (void)sensor_id; (void)timestamp; (void)user_data;
    if (size < 8) return;   // int16 quaternion x,y,z,w = 8 bytes (accuracy may follow)

    float roll, pitch, yaw;
    bhy2_quaternion_to_euler(data, &roll, &pitch, &yaw);
    s_raw_yaw      = yaw < 0.0f ? yaw + 360.0f : yaw;
    s_have_heading = true;
}

void sensor_enable()
{
    if (s_sensor_started) return;
    // GAME_ROTATION_VECTOR: 6DoF (accel+gyro), works on this magnetometer-less
    // board. Relative heading, drifts without a mag - hence the SET NORTH pin.
    instance.sensor.onResultEvent(SensorBHI260AP::GAME_ROTATION_VECTOR, on_rotation_vector);
    instance.sensor.configure(SensorBHI260AP::GAME_ROTATION_VECTOR, 10.0f /*Hz*/, 0);
    s_sensor_started = true;
    s_have_heading   = false;
}

void sensor_disable()
{
    if (!s_sensor_started) return;
    instance.sensor.configure(SensorBHI260AP::GAME_ROTATION_VECTOR, 0, 0);
    instance.sensor.removeResultEvent(SensorBHI260AP::GAME_ROTATION_VECTOR, on_rotation_vector);
    s_sensor_started = false;
}

// Displayed heading = raw yaw minus the pinned-north offset, normalized 0..360.
float display_heading()
{
    float h = s_raw_yaw - s_north_offset;
    h = fmodf(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    return h;
}

const char *cardinal(float deg)
{
    static const char *names[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    int idx = (int)((deg + 22.5f) / 45.0f) & 7;
    return names[idx];
}

// Signed area helper for the point-in-triangle test (>0 / <0 by winding).
inline float edge(float px, float py, float ax, float ay, float bx, float by)
{
    return (px - bx) * (ay - by) - (ax - bx) * (py - by);
}

}  // namespace

static lv_obj_t   *screen;
static lv_obj_t   *s_return = nullptr;
static lv_obj_t   *s_arrow  = nullptr;   // the triangle canvas
static lv_obj_t   *s_heading_label;
static lv_timer_t *s_poll_timer = nullptr;
static int         s_last_drawn = -1000; // last heading we rendered (whole deg)

LV_DRAW_BUF_DEFINE_STATIC(s_arrow_buf, ARROW_CV, ARROW_CV, LV_COLOR_FORMAT_ARGB8888);

// Clear the canvas and paint the arrowhead rotated so it points at pinned north.
static void draw_arrow(float heading)
{
    if (!s_arrow) return;
    lv_canvas_fill_bg(s_arrow, lv_color_black(), LV_OPA_TRANSP);

    // Rotate opposite the watch's own heading so the apex keeps aiming at the
    // pinned north as the wrist turns. Sign matches the old needle; flip if the
    // arrow turns the wrong way on real hardware.
    float th = -heading * ARROW_PI / 180.0f;
    float cs = cosf(th), sn = sinf(th);

    float ax = ARROW_C + (ARROW_AX * cs - ARROW_AY * sn);
    float ay = ARROW_C + (ARROW_AX * sn + ARROW_AY * cs);
    float lx = ARROW_C + (ARROW_LX * cs - ARROW_LY * sn);
    float ly = ARROW_C + (ARROW_LX * sn + ARROW_LY * cs);
    float rx = ARROW_C + (ARROW_RX * cs - ARROW_RY * sn);
    float ry = ARROW_C + (ARROW_RX * sn + ARROW_RY * cs);

    int minx = (int)floorf(fminf(ax, fminf(lx, rx)));
    int maxx = (int)ceilf (fmaxf(ax, fmaxf(lx, rx)));
    int miny = (int)floorf(fminf(ay, fminf(ly, ry)));
    int maxy = (int)ceilf (fmaxf(ay, fmaxf(ly, ry)));
    if (minx < 0) minx = 0; if (miny < 0) miny = 0;
    if (maxx > ARROW_CV - 1) maxx = ARROW_CV - 1;
    if (maxy > ARROW_CV - 1) maxy = ARROW_CV - 1;

    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float d1 = edge(px, py, ax, ay, lx, ly);
            float d2 = edge(px, py, lx, ly, rx, ry);
            float d3 = edge(px, py, rx, ry, ax, ay);
            bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
            if (!(neg && pos))
                lv_canvas_set_px(s_arrow, x, y, NR, LV_OPA_COVER);
        }
    }
    lv_obj_invalidate(s_arrow);
}

static void on_poll(lv_timer_t *)
{
    if (!s_have_heading) return;
    float heading = display_heading();

    int whole = (int)(heading + 0.5f);
    if (whole != s_last_drawn) {   // only repaint when it actually moved a degree
        draw_arrow(heading);
        s_last_drawn = whole;
    }

    char buf[16];
    snprintf(buf, sizeof(buf), "%3d\xC2\xB0 %s", (int)heading, cardinal(heading));
    lv_label_set_text(s_heading_label, buf);
}

static void on_set_north(lv_event_t *)
{
    // Pin the current raw yaw as north: the dial reads 0 here until re-pinned.
    if (s_have_heading) { s_north_offset = s_raw_yaw; s_last_drawn = -1000; }
}

static void on_unload(lv_event_t *)
{
    // Fires as soon as navigation away starts, regardless of path - the one
    // place that reliably powers the sensor back down again.
    sensor_disable();
    if (s_poll_timer) { lv_timer_del(s_poll_timer); s_poll_timer = nullptr; }
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
    lv_obj_set_style_text_color(title, NG, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(title, 3, LV_PART_MAIN);
    lv_label_set_text(title, "COMPASS");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Fixed ring: the pinned-north mark red, E/S/W grey. The ARROW rotates as
    // the watch turns; the ring itself never moves.
    lv_obj_t *ring = lv_obj_create(screen);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 200, 200);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, -14);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_color(ring, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_border_width(ring, 2, LV_PART_MAIN);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);

    struct { const char *label; int dx; int dy; lv_color_t color; } marks[4] = {
        { "N", 0, -90, NR }, { "E", 90, 0, NG }, { "S", 0, 90, NG }, { "W", -90, 0, NG },
    };
    for (auto &m : marks) {
        lv_obj_t *lbl = lv_label_create(ring);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, m.color, LV_PART_MAIN);
        lv_label_set_text(lbl, m.label);
        lv_obj_align(lbl, LV_ALIGN_CENTER, m.dx, m.dy);
    }

    // Arrow: a transparent canvas centred on the dial; draw_arrow() paints a red
    // navigation triangle into it, rotated to point at pinned north.
    LV_DRAW_BUF_INIT_STATIC(s_arrow_buf);
    s_arrow = lv_canvas_create(screen);
    lv_canvas_set_draw_buf(s_arrow, &s_arrow_buf);
    lv_obj_align(s_arrow, LV_ALIGN_CENTER, 0, -14);
    lv_obj_clear_flag(s_arrow, LV_OBJ_FLAG_SCROLLABLE);
    lv_canvas_fill_bg(s_arrow, lv_color_black(), LV_OPA_TRANSP);

    // Heading read-out below the dial so the arrow never covers it.
    s_heading_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_heading_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_heading_label, NW, LV_PART_MAIN);
    lv_label_set_text(s_heading_label, "---");
    lv_obj_align(s_heading_label, LV_ALIGN_BOTTOM_MID, 0, -84);

    // SET NORTH: pins the current facing as the dial's north. Needed because
    // this is a gyro-relative compass (no magnetometer on this board), so it
    // has no absolute reference and drifts - re-press when it wanders.
    lv_obj_t *btn = lv_button_create(screen);
    lv_obj_set_size(btn, 170, 54);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_bg_color(btn, NR, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 27, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, on_set_north, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(bl, NW, LV_PART_MAIN);
    lv_label_set_text(bl, "SET NORTH");
    lv_obj_center(bl);

    lv_obj_add_event_cb(screen, on_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(screen, on_unload, LV_EVENT_SCREEN_UNLOAD_START, NULL);
}

void compass_screen_show()
{
    if (!screen) build();

    lv_obj_t *from = lv_screen_active();
    if (from != screen) s_return = from;

    s_last_drawn = -1000;   // force a repaint on the first poll after re-entry
    sensor_enable();
    if (!s_poll_timer) s_poll_timer = lv_timer_create(on_poll, 200, NULL);

    lv_scr_load(screen);
}

bool compass_screen_is_active() { return lv_screen_active() == screen; }
