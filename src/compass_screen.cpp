// compass_screen.cpp - see compass_screen.h.
//
// True-north heading screen. Configures the BHI260AP's ROTATION_VECTOR
// virtual sensor (absolute, magnetometer-fused) only while this screen is on
// screen, torn down on LV_EVENT_SCREEN_UNLOAD_START so the sensor stops no
// matter which gesture/shortcut navigates away (not just this screen's own
// back gesture) - mirrors main.cpp's motion-wake enable()/disable() pattern
// for the same sensor hub, but scoped to screen visibility instead of a
// persistent setting.
#include "compass_screen.h"
#include "theme.h"

#include <LilyGoLib.h>
#include <math.h>

// Defined in main.cpp.
void screen_return_to(lv_obj_t *scr);

static const lv_color_t NW = lv_color_hex(0xFFFFFF);   // primary text
static const lv_color_t NG = lv_color_hex(0x9A9A9A);   // secondary text
static const lv_color_t NR = lv_color_hex(0xE02020);   // accent red

namespace {

volatile bool    s_have_heading  = false;
volatile float    s_heading_deg   = 0.0f;
volatile uint16_t s_accuracy      = 0;      // 0 Unreliable .. 3 High (BHY2/BSX scale)
bool              s_sensor_started = false;

// SensorDataParseCallback (BoschSensorBase.hpp): void(uint8_t, const uint8_t*,
// uint32_t, uint64_t*, void*). The const-qualified data pointer matters - the
// vendored .ino example's free function uses a non-const uint8_t*, which
// mismatches this typedef under this toolchain's device build.
void on_rotation_vector(uint8_t sensor_id, const uint8_t *data, uint32_t size,
                         uint64_t *timestamp, void *user_data)
{
    (void)sensor_id; (void)timestamp; (void)user_data;
    if (size < 10) return;   // int16 x,y,z,w + uint16 accuracy = 10 bytes

    float roll, pitch, yaw;
    bhy2_quaternion_to_euler(data, &roll, &pitch, &yaw);
    s_heading_deg  = yaw < 0.0f ? yaw + 360.0f : yaw;
    s_accuracy     = (uint16_t)data[8] | ((uint16_t)data[9] << 8);
    s_have_heading = true;
}

void sensor_enable()
{
    if (s_sensor_started) return;
    instance.sensor.onResultEvent(SensorBHI260AP::ROTATION_VECTOR, on_rotation_vector);
    instance.sensor.configure(SensorBHI260AP::ROTATION_VECTOR, 10.0f /*Hz*/, 0);
    s_sensor_started = true;
    s_have_heading   = false;
}

void sensor_disable()
{
    if (!s_sensor_started) return;
    instance.sensor.configure(SensorBHI260AP::ROTATION_VECTOR, 0, 0);
    instance.sensor.removeResultEvent(SensorBHI260AP::ROTATION_VECTOR, on_rotation_vector);
    s_sensor_started = false;
}

const char *cardinal(float deg)
{
    static const char *names[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    int idx = (int)((deg + 22.5f) / 45.0f) & 7;
    return names[idx];
}

}  // namespace

static lv_obj_t   *screen;
static lv_obj_t   *s_return = nullptr;
static lv_obj_t   *s_needle;
static lv_obj_t   *s_heading_label;
static lv_obj_t   *s_calib_label;
static lv_timer_t *s_poll_timer = nullptr;

static void on_poll(lv_timer_t *)
{
    if (!s_have_heading) return;
    float    heading = s_heading_deg;
    uint16_t acc      = s_accuracy;

    // Needle rotates opposite the watch's own heading so it keeps pointing
    // at true north as the wrist turns. LVGL rotation is in 0.1 degree
    // units, clockwise-positive. NOT verified on real hardware yet - if the
    // needle turns the wrong way on the actual watch, flip this sign.
    int16_t rot = (int16_t)(-heading * 10.0f);
    rot = ((rot % 3600) + 3600) % 3600;
    lv_obj_set_style_transform_rotation(s_needle, rot, LV_PART_MAIN);

    char buf[16];
    snprintf(buf, sizeof(buf), "%3d\xC2\xB0 %s", (int)heading, cardinal(heading));
    lv_label_set_text(s_heading_label, buf);

    if (acc < 2) {
        lv_label_set_text(s_calib_label, "Calibrating - move your wrist in a figure-8");
        lv_obj_clear_flag(s_calib_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_calib_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_unload(lv_event_t *)
{
    // Fires as soon as navigation away starts, regardless of path (this
    // screen's own gesture handler, the global swipe-down-to-Tools jump, the
    // clock's swipe-home, ...) - the one place that reliably powers the
    // sensor back down again.
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
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    // Fixed ring: N marked red, E/S/W grey, at the four cardinal points. The
    // NEEDLE rotates to point true north as the watch turns - the ring
    // itself never moves.
    lv_obj_t *ring = lv_obj_create(screen);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, 220, 220);
    lv_obj_align(ring, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_color(ring, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_border_width(ring, 2, LV_PART_MAIN);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);

    struct { const char *label; int dx; int dy; lv_color_t color; } marks[4] = {
        { "N", 0, -100, NR }, { "E", 100, 0, NG }, { "S", 0, 100, NG }, { "W", -100, 0, NG },
    };
    for (auto &m : marks) {
        lv_obj_t *lbl = lv_label_create(ring);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, m.color, LV_PART_MAIN);
        lv_label_set_text(lbl, m.label);
        lv_obj_align(lbl, LV_ALIGN_CENTER, m.dx, m.dy);
    }

    // Needle: a tall red pill, pivoted at its centre, rotated in on_poll().
    s_needle = lv_obj_create(screen);
    lv_obj_set_size(s_needle, 8, 160);
    lv_obj_align(s_needle, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_radius(s_needle, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_needle, NR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_needle, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_needle, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_needle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_transform_pivot_x(s_needle, 4, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(s_needle, 80, LV_PART_MAIN);

    s_heading_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_heading_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_heading_label, NW, LV_PART_MAIN);
    lv_label_set_text(s_heading_label, "---");
    lv_obj_align(s_heading_label, LV_ALIGN_BOTTOM_MID, 0, -70);

    s_calib_label = lv_label_create(screen);
    lv_obj_set_style_text_font(s_calib_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_calib_label, NG, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_calib_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(s_calib_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_calib_label, 280);
    lv_label_set_text(s_calib_label, "");
    lv_obj_align(s_calib_label, LV_ALIGN_BOTTOM_MID, 0, -35);
    lv_obj_add_flag(s_calib_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_event_cb(screen, on_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(screen, on_unload, LV_EVENT_SCREEN_UNLOAD_START, NULL);
}

void compass_screen_show()
{
    if (!screen) build();

    lv_obj_t *from = lv_screen_active();
    if (from != screen) s_return = from;

    sensor_enable();
    if (!s_poll_timer) s_poll_timer = lv_timer_create(on_poll, 200, NULL);

    lv_scr_load(screen);
}

bool compass_screen_is_active() { return lv_screen_active() == screen; }
