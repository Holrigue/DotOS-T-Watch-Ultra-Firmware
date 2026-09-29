#include <Arduino.h>
#include "theme.h"
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <bosch/BoschSensorDataHelper.hpp>
#include "esp_wifi.h"
#include "esp_bt.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <SD.h>
#include <time.h>
#include <math.h>
#include "gps_screen.h"
#include "lora_screen.h"
#include "meshtastic.h"
#include "meshtastic_screen.h"
#include "nodes_screen.h"
#include "send_message_screen.h"
#include "map_screen.h"
#include "configuration_screen.h"
#include "channels_screen.h"
#include "settings_screen.h"
#include "screenshot.h"
#include "tools_screen.h"
#include "apps_screen.h"
#include "notifications_screen.h"
#include "notify_popup.h"
#include "notify/notify_center.h"   // notify::center().count() for the unread badge
#include "haptic.h"                 // global vibration intensity
#include "device_mode.h"
#include "ans.h"   // find channel (Find-My-Watch / Find-My-Phone)
#include "pet_screen.h"
#include "handshake.h"
#include "tpms_screen.h"
#include "timezone.h"
#include "clock_time.h"
#include "dot_font_5x7.h"   // ARGUS-Design-OS "Dot" face 5x7 dot-matrix digits
#include "sun_elevation.h" // solar elevation for automatic day/night brightness
#include <Preferences.h>
#include "detector_toggle.h" // shared detector on/off + NVS persistence (Dot badges, Tools)
#include "health_state.h"    // wearer health metrics (mirrored from Gadgetbridge)
#include "dot_tiles.h"       // two user-selectable data slots on the Dot face
#include "power_mgmt.h"      // PMU charge policy + battery longevity setting
#include "face_watch.h"      // Dot watchface customization (Tools > Face)
#include "tpms.h"
#include "pager_screen.h"
#include "pager.h"
#include "mouse_screen.h"
#include "usb_sd.h"
#include "usb_sd_screen.h"
#include "aprs.h"
#include "aprs_screen.h"
#include "tesla_cp_screen.h"
#include "wifi_screen.h"
#include "wifi_radio_screen.h"
#include "bluetooth_screen.h"
#include "analyze_screen.h"
#include "bt_analyze_screen.h"
#include "lora_analyze_screen.h"
#include "pingsweep.h"
#include "portscan.h"
#include "portscan_screen.h"
#include "wardriver_screen.h"
#include "nfc_screen.h"
#include "nfc_write_screen.h"
#include "stopwatch_screen.h"
#include "timer_screen.h"
#include "alarm.h"
#include "alarm_screen.h"
#include "calendar_screen.h"
#include "world_clock_screen.h"
#include "sun_moon_screen.h"
#include "time_screen.h"
#include "health_screen.h"
#include "flashlight_screen.h"
#include "argus_mode.h"
#include "spycam_screen.h"
#include "nfc_field_screen.h"
#include "pin_pad_screen.h"
#include "loot_screen.h"
#include "deauth_screen.h"
#include "tracker_timeline_screen.h"
#include "beacon_spam.h"
#include "deauth_attack.h"
#include "rogue_ap.h"
#include "probe_sniffer.h"
#include "offense_wipe.h"
#include "airtag.h"
#include "flipper.h"
#include "skimmer.h"
#include "evil_twin.h"
#include "flock.h"
#include "human_detector.h"
#include "threat_radar.h"
#include "hexhound.h"
#include "ble_scan_manager.h"
#include "radio_coexist.h"
#include "boot_prefs.h"
#include "charge_state.h"   // debounced Discharging/Charging/Topped readout
#include "detect_log_sd.h"   // detection-log retention sweep
#include <esp_wifi.h>   // esp_wifi_get_mode() for live WiFi-mode reads
#include "threat_radar_screen.h"
#include "tracker_rep.h"
#include "counter_tail.h"
#include "matrix_bg.h"
#include "background.h"
#include "nfc_icon.h"
#include "detect_pipeline.h"
#include "ble_detect_pipeline.h"
// WiFi threat-detection pipeline (evil-twin + beacon-flood -> HADES-red + log).
// ON, using PIGGYBACK activation (detect_pipeline.cpp): the pipeline attaches its
// beacon consumer ONLY while another WiFi scan (Evil Twin / Flock / Pwn /
// wardriver) is already running, and detaches when none remain. So it never
// powers WiFi on by itself, never flips a connected STA into monitor mode, and
// adds nothing to the boot path - it just enriches scans the user already started
// with threat posture (HADES-red accent + HexHound) and a forensic log.
#define ARGUS_WIFI_THREAT_PIPELINE 1
// BLE anti-stalking pipeline (tracker_ident + tail_detect, piggyback on the BLE
// scan manager). Starts OFF: flip to 1 only after an on-device battery test that
// it piggybacks cleanly (toggle AirTag/Flipper ON to bring BLE up) with no
// boot-loop. See ble_detect_pipeline.h.
#define ARGUS_BLE_THREAT_PIPELINE  1

#if ARGUS_COEX_MEASURE
static void coex_log_heap(const char *phase)
{
    unsigned largest_internal = (unsigned)heap_caps_get_largest_free_block(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    unsigned free_internal = (unsigned)heap_caps_get_free_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    unsigned largest_dma = (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    unsigned bt_status = (unsigned)esp_bt_controller_get_status();

    Serial.printf(
        "[COEX] phase=%s ms=%lu largest_internal=%u free_internal=%u "
        "largest_dma=%u bt_status=%u\n",
        phase, (unsigned long)millis(), largest_internal, free_internal,
        largest_dma, bt_status);
    Serial.flush();

    if (!instance.isCardReady()) return;
    File f = SD.open("/Settings/coexlog.txt", FILE_APPEND);
    if (!f) return;
    f.printf(
        "phase=%s ms=%lu reset=%u largest_internal=%u free_internal=%u "
        "largest_dma=%u bt_status=%u\n",
        phase, (unsigned long)millis(), (unsigned)esp_reset_reason(),
        largest_internal, free_internal, largest_dma, bt_status);
    f.flush();
    f.close();
}
#else
static inline void coex_log_heap(const char *) {}
#endif

// Modal dialog helper; defined lower down (near the low-mem section) but called
// from setup()'s boot-radio block above it, so it needs an early prototype.
void low_mem_show_dialog(const char *msg);

// Early prototypes so the face-switch plumbing (defined up here) can drive a
// full refresh of whichever face is active without depending on definition order.
static void update_clock();
static void update_dot_face(const struct tm *t);
static void build_dot_status_row(lv_obj_t *parent);
static void build_dot_usb(lv_obj_t *parent);
static void build_dot_tiles(lv_obj_t *parent);
static void update_dot_tiles(bool usb_present);
static const lv_font_t *dot_date_lvfont();
void clock_screen_apply_face_custom();   // re-apply Face-watch look (fonts/accent/order)
static void build_dot_bottom(lv_obj_t *parent);
static void build_dot_badges(lv_obj_t *parent);
static void build_dot_notif(lv_obj_t *parent);
static void update_dot_status();
static void dot_face_tick();
void main_loop_request_lvgl_priority(int cycles);   // defined with the main loop below
void clock_screen_show();                           // defined further down


static lv_obj_t *clock_screen;
static lv_obj_t *time_label;
static lv_span_t *s_span_hours;
static lv_span_t *s_span_colon;
static lv_span_t *s_span_rest;
static lv_span_t *s_span_ampm;   // AM/PM suffix in a smaller font than the digits
static lv_obj_t *date_label;
static lv_obj_t *gps_indicator;
static lv_obj_t *wardriver_container;
static lv_obj_t *wardriver_wifi_label;
static lv_obj_t *wardriver_bt_label;
static lv_obj_t *wifi_indicator;
static lv_obj_t *bt_indicator;
static lv_obj_t *sd_indicator;
static lv_obj_t *nfc_indicator;
static lv_obj_t *lora_container;
static lv_obj_t *lora_arc;
static lv_obj_t *lora_ball;
static lv_obj_t *lora_stick;
static lv_obj_t *dot_container = nullptr;   // ARGUS-Design-OS "Dot" face; built hidden
static lv_obj_t *dot_time_img  = nullptr;   // dot-matrix time raster (lv_image)
static uint32_t *dot_time_buf  = nullptr;   // ARGB8888 pixels in PSRAM
static lv_image_dsc_t dot_time_dsc;         // descriptor pointing at dot_time_buf
static lv_obj_t *dot_time_label = nullptr;  // font-mode hour (shown instead of the raster)

// Large Montserrat clock font used for the font-mode hour (also the digital
// face's base). Declared here so the Dot builder can reference it; the full set
// is externed again lower down next to the digital face.
extern "C" const lv_font_t lv_font_montserrat_clock_96;
// Status row: the six line-art icons are rasterised into one ARGB8888 sprite;
// the NFC state stays an LVGL label.
static lv_obj_t *dot_status_img = nullptr;
static uint32_t *dot_status_buf = nullptr;
static lv_image_dsc_t dot_status_dsc;
static lv_obj_t *dot_nfc_label  = nullptr;

// Notifications button: a persistent accent-coloured square in the bottom-left
// corner of the Dot face, always visible and always tappable (opens the
// notifications screen). A white envelope glyph appears inside it while there
// are unread notifications (phone notifs beyond the last-seen baseline, or
// unread Meshtastic messages) and clears once they are marked read. Replaces the
// old top-right red count pill.
static lv_obj_t *dot_notif_btn = nullptr;
static lv_obj_t *dot_notif_env = nullptr;
static uint32_t  s_notif_seen  = 0;   // notify::center().count() acknowledged on the last open

// Watch face selection. DotOS is now the only face; FACE_DIGITAL is kept solely
// as a safety fallback if the Dot layer fails to build (it needs a PSRAM ARGB
// buffer). FACE_ANALOG is retired — the enum value is left in place so the
// persisted /Settings/settings.txt numbering (clock_face=0|1|2) keeps meaning,
// but nothing selects or renders it any more.
enum ClockFace { FACE_DIGITAL = 0, FACE_ANALOG = 1, FACE_DOT = 2 };
static ClockFace clock_face = FACE_DOT;
static uint32_t last_update_ms   = 0;
static int      clock_utc_offset = -4; // hours; default US Eastern (EDT).
                                       // The RTC ALWAYS holds UTC; this shifts it
                                       // to local for display. Every RTC writer
                                       // (GPS, NTP, Manual Time, the build-time
                                       // seed below) must keep that invariant and
                                       // persist the offset that pairs with it,
                                       // or the face comes up hours off after a
                                       // reboot. GPS refines this (incl. DST)
                                       // from longitude when available.
static bool     manual_time_override = false; // user-set time; blocks GPS sync

// Set when the RTC came up unset and was seeded from the firmware build time,
// together with the offset used to convert that local build time to UTC. If
// timezone_load_on_boot() then restores a different offset, the seed is rebased
// so the face keeps showing the build time rather than sliding by the delta.
static bool     rtc_build_seeded     = false;
static int      rtc_build_seed_off   = 0;
static bool     clock_12h        = true;
static bool     clock_show_day   = true;
static bool     clock_show_date  = true;
static bool     clock_show_ampm  = true;
static bool     clock_show_secs  = true;
static bool     clock_vibrate    = false;

// Battery widget objects
static lv_obj_t *bat_fill;
static lv_obj_t *bat_label;

// Charging bolt drawn inside the battery body, left of the percentage.
// Built from lv_line strokes rather than LV_SYMBOL_CHARGE: bat_label is
// styled with font_argus_mono_16, a VT323 subset that only covers U+0020..U+007E
// and declares no .fallback, so the U+F0E7 symbol glyph resolved to nothing
// and the charge state rendered as an invisible trailing space. Same reason
// the timer/stopwatch icons above are primitives.
static lv_obj_t *bat_bolt;

// Debounced charge readout feeding bat_bolt. Fed one sample per 1 Hz tick.
static ChargeIndicator bat_charge;
// Alternates each tick while charging to breathe the bolt's opacity.
static bool bolt_pulse_dim = false;
static void update_charge_bolt();

// Bell glyph shown to the left of the battery when the alarm is enabled.
static lv_obj_t *alarm_indicator;
static lv_obj_t *bat_nub;

// Small green icons drawn next to the battery while a timer is counting
// down or the stopwatch is running. Built from primitives (ring + knob +
// hand) because LV_SYMBOL_* has no clock glyph.
static lv_obj_t *timer_indicator;
static lv_obj_t *stopwatch_indicator;

// Compact unread badge shown in the top status row to the left of the
// LoRa icon. Hidden when unread count is zero. The previous bottom-row
// envelope + count duplicated this number, so it was removed.
static lv_obj_t *mesh_top_count_label;

// AirTag sniffer indicator (above battery, shown only while scanner is active)
static lv_obj_t *airtag_indicator;
static lv_obj_t *airtag_count_label;

// Flipper Zero detector indicator (next to AirTag indicator)
static lv_obj_t *flipper_indicator;
static lv_obj_t *flipper_count_label;

// Card-skimmer detector indicator (next to Flipper indicator)
static lv_obj_t *skimmer_indicator;
static lv_obj_t *skimmer_count_label;

// Evil-twin WiFi attack indicator (next to Skimmer indicator)
static lv_obj_t *evil_twin_indicator;
static lv_obj_t *evil_twin_count_label;

// Flock/OUI surveillance-device indicator (left of AirTag indicator)
static lv_obj_t *flock_indicator;
static lv_obj_t *flock_count_label;

// Battery body geometry (pixels)
static constexpr int BAT_W      = 60;                       // halved from 120
static constexpr int BAT_H      = 20;
static constexpr int BAT_BORDER = 2;
static constexpr int BAT_INNER_W = BAT_W - 2 * BAT_BORDER;  // 56
static constexpr int BAT_INNER_H = BAT_H - 2 * BAT_BORDER;  // 16
static constexpr int BAT_NUB_W  = 10;
static constexpr int BAT_NUB_H  = 10;                       // scaled with body

static lv_color_t bat_color(int pct)
{
    // Dimmed ~50% from the previous palette so the fill doesn't dominate
    // the clock face. Same hue intent (green / yellow / red / dark red),
    // just lower luminance.
    if (pct >= 51) return lv_color_make(0x11, 0x66, 0x11); // dim green   (51-100)
    if (pct >= 35) return lv_color_make(0x99, 0x77, 0x00); // dim yellow  (35-50)
    if (pct >= 15) return lv_color_make(0x99, 0x22, 0x22); // dim red     (15-34)
    return             lv_color_make(0x55, 0x00, 0x00);    // very dark red (0-14)
}

static void build_battery_widget(lv_obj_t *screen)
{
    // Transparent container sized to hold body + nub
    lv_obj_t *container = lv_obj_create(screen);
    lv_obj_set_size(container, BAT_W + BAT_NUB_W, BAT_H);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(container, LV_ALIGN_BOTTOM_MID, 0, -10);

    // Battery body: black fill, white border, rounded corners
    lv_obj_t *bat_body = lv_obj_create(container);
    lv_obj_set_pos(bat_body, 0, 0);
    lv_obj_set_size(bat_body, BAT_W, BAT_H);
    lv_obj_set_style_bg_color(bat_body, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bat_body, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(bat_body, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(bat_body, BAT_BORDER, LV_PART_MAIN);
    lv_obj_set_style_radius(bat_body, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bat_body, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bat_body, LV_OBJ_FLAG_SCROLLABLE);

    // Fill bar — child of bat_body; positioned inside border, grows left-to-right.
    // Width is updated each second by update_battery().
    bat_fill = lv_obj_create(bat_body);
    lv_obj_set_pos(bat_fill, 0, 0);
    lv_obj_set_size(bat_fill, 0, BAT_INNER_H);
    lv_obj_set_style_bg_color(bat_fill, bat_color(100), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bat_fill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bat_fill, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bat_fill, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bat_fill, 0, LV_PART_MAIN);

    // Percentage label — drawn after fill so it renders on top. Sized
    // down to Montserrat 14 to fit inside the halved (16 px inner)
    // battery body without clipping the digits.
    bat_label = lv_label_create(bat_body);
    lv_obj_set_style_text_color(bat_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(bat_label, &font_argus_mono_16, LV_PART_MAIN);   // VT323 (brand readout)
    lv_label_set_text(bat_label, "--");
    lv_obj_align(bat_label, LV_ALIGN_CENTER, 0, 0);

    // Charging bolt - a 3-segment zigzag stroked in white, parked against the
    // left inside edge of the body. Created after bat_label so it draws on
    // top of both the fill and the digits.
    //
    // It cannot actually collide with the digits: VT323 16 is monospace at
    // adv_w 96 (6 px/glyph), so the widest reading, "100%", is 24 px centred
    // in the 56 px inner width and spans x=16..40. The bolt lives in x=3..13.
    //
    // White, not ARGUS_ACCENT / status_accent_active(): those flip to
    // threat-red on a tail, and a battery bolt is chrome, not a live-threat
    // surface. It must mean the same thing in Defense and Offense.
    //
    // The line IS the widget (no wrapper container) so the pulse can drive
    // line_opa directly. Setting `opa` on a parent instead would make LVGL
    // composite the subtree through an intermediate layer on every redraw,
    // which is pure waste for four strokes.
    //
    // Points: down-left, jog right, down-left again - the standard zigzag.
    static lv_point_precise_t bolt_pts[] = { {8, 1}, {3, 7}, {7, 8}, {1, 15} };
    bat_bolt = lv_line_create(bat_body);
    lv_line_set_points(bat_bolt, bolt_pts, 4);
    lv_obj_set_pos(bat_bolt, 3, 0);
    lv_obj_set_style_line_color(bat_bolt, lv_color_white(), 0);
    lv_obj_set_style_line_width(bat_bolt, 2, 0);
    lv_obj_set_style_line_rounded(bat_bolt, true, 0);
    lv_obj_clear_flag(bat_bolt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(bat_bolt, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_flag(bat_bolt, LV_OBJ_FLAG_HIDDEN);   // shown only on USB power

    // Terminal nub (positive terminal on the right)
    bat_nub = lv_obj_create(container);
    lv_obj_set_pos(bat_nub, BAT_W, (BAT_H - BAT_NUB_H) / 2);
    lv_obj_set_size(bat_nub, BAT_NUB_W, BAT_NUB_H);
    lv_obj_set_style_bg_color(bat_nub, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bat_nub, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bat_nub, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bat_nub, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bat_nub, 0, LV_PART_MAIN);
}

// ---------------------------------------------------------------------------
// ARGUS-Design-OS "Dot" watch face
//
// A self-contained dot-matrix face rendered on its own opaque layer
// (dot_container) that covers the normal clock background when active. Built
// hidden; clock_screen_set_face() shows it. All coordinates are the native
// 410x502 portrait space, straight from docs/dotface/dotface_final.svg.
//
// Strict palette: white = active, gray = idle, red = notifications/detections,
// near-black background. Kept as helpers so every Dot widget draws from one
// source of truth.
// ---------------------------------------------------------------------------
static inline lv_color_t dot_white()     { return lv_color_hex(0xFFFFFF); }
static inline lv_color_t dot_gray()      { return lv_color_hex(0x5C5C5C); }
static inline lv_color_t dot_red()       { return lv_color_hex(0xE02020); }
// True black, not the mockup's #0A0A0A: on this AMOLED any non-zero value keeps
// every background pixel faintly lit, which reads as a dark red cast in the dark
// (the red subpixels lead at the lowest levels) and costs battery. 0x000000
// switches those pixels fully off.
static inline lv_color_t dot_bg()        { return lv_color_hex(0x000000); }
static inline lv_color_t dot_seg_empty() { return lv_color_hex(0x3A3A3A); }

// Dot-matrix time raster geometry, in the native 410x502 face space straight
// from docs/dotface/dotface_final.svg. The raster is an ARGB8888 image (exact
// colours, no RGB565 byte-swap surprises on the strict palette) that covers the
// HH:MM block; only lit dots are opaque, the rest stays transparent so the face
// background shows through.
static constexpr int   DOT_TIME_X    = 44;      // raster origin on the face
static constexpr int   DOT_TIME_Y    = 184;
static constexpr int   DOT_TIME_W    = 308;     // covers x 44..352
static constexpr int   DOT_TIME_H    = 96;      // covers y 184..280
static constexpr int   DOT_CELL      = 14;      // grid pitch
static constexpr int   DOT_ROW_Y0    = 190;     // top-row centre y
static constexpr float DOT_DIGIT_X[4] = { 50.0f, 119.44f, 220.24f, 289.68f };
static constexpr float DOT_COLON_X   = 197.84f;
static constexpr int   DOT_COLON_Y0  = 218;
static constexpr int   DOT_COLON_Y1  = 246;
static constexpr float DOT_R         = 5.2f;    // digit dot radius
static constexpr float DOT_COLON_R   = 4.42f;   // colon dot radius

// Stamp one filled anti-aliasing-free disc into an ARGB8888 buffer. Centre is
// in raster-local pixels; out-of-range pixels are skipped.
static void dot_plot_disc(uint32_t *buf, int w, int h,
                          float cx, float cy, float r, uint32_t argb)
{
    int x0 = (int)floorf(cx - r), x1 = (int)ceilf(cx + r);
    int y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
    float r2 = r * r;
    for (int y = y0; y <= y1; y++) {
        if (y < 0 || y >= h) continue;
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || x >= w) continue;
            float dx = (float)x + 0.5f - cx;
            float dy = (float)y + 0.5f - cy;
            if (dx * dx + dy * dy <= r2) buf[y * w + x] = argb;
        }
    }
}

// Stamp a thick line segment (rounded caps) into an ARGB8888 buffer, by the
// distance from each pixel to the segment. Coordinates are raster-local.
static void dot_plot_seg(uint32_t *buf, int w, int h,
                         float x0, float y0, float x1, float y1,
                         float width, uint32_t argb)
{
    float hw = width * 0.5f;
    int minx = (int)floorf(fminf(x0, x1) - hw - 1), maxx = (int)ceilf(fmaxf(x0, x1) + hw + 1);
    int miny = (int)floorf(fminf(y0, y1) - hw - 1), maxy = (int)ceilf(fmaxf(y0, y1) + hw + 1);
    float dx = x1 - x0, dy = y1 - y0;
    float len2 = dx * dx + dy * dy;
    for (int y = miny; y <= maxy; y++) {
        if (y < 0 || y >= h) continue;
        for (int x = minx; x <= maxx; x++) {
            if (x < 0 || x >= w) continue;
            float px = (float)x + 0.5f - x0, py = (float)y + 0.5f - y0;
            float t = len2 > 0 ? (px * dx + py * dy) / len2 : 0.0f;
            if (t < 0) t = 0; if (t > 1) t = 1;
            float cx = px - t * dx, cy = py - t * dy;
            if (cx * cx + cy * cy <= hw * hw) buf[y * w + x] = argb;
        }
    }
}

// Stamp a stroked circular arc, centre (cx,cy) radius r, spanning [a0,a1]
// degrees measured clockwise from +x in this y-down raster (so 180..360 is the
// top half, matching the SVG arcs). Coordinates are raster-local.
static void dot_plot_arc(uint32_t *buf, int w, int h,
                         float cx, float cy, float r, float a0, float a1,
                         float width, uint32_t argb)
{
    float hw = width * 0.5f;
    int minx = (int)floorf(cx - r - hw - 1), maxx = (int)ceilf(cx + r + hw + 1);
    int miny = (int)floorf(cy - r - hw - 1), maxy = (int)ceilf(cy + r + hw + 1);
    for (int y = miny; y <= maxy; y++) {
        if (y < 0 || y >= h) continue;
        for (int x = minx; x <= maxx; x++) {
            if (x < 0 || x >= w) continue;
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            if (fabsf(dist - r) > hw) continue;
            float ang = atan2f(dy, dx) * 57.29578f;
            if (ang < 0) ang += 360.0f;
            bool in = (a0 <= a1) ? (ang >= a0 && ang <= a1) : (ang >= a0 || ang <= a1);
            if (in) buf[y * w + x] = argb;
        }
    }
}

// Fill a triangle (used for the GPS pin's tapered point). Coordinates are
// raster-local; a pixel is inside when it sits on the same side of all edges.
static void dot_fill_tri(uint32_t *buf, int w, int h,
                         float ax, float ay, float bx, float by,
                         float ccx, float ccy, uint32_t argb)
{
    int minx = (int)floorf(fminf(ax, fminf(bx, ccx))), maxx = (int)ceilf(fmaxf(ax, fmaxf(bx, ccx)));
    int miny = (int)floorf(fminf(ay, fminf(by, ccy))), maxy = (int)ceilf(fmaxf(ay, fmaxf(by, ccy)));
    for (int y = miny; y <= maxy; y++) {
        if (y < 0 || y >= h) continue;
        for (int x = minx; x <= maxx; x++) {
            if (x < 0 || x >= w) continue;
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
            float d2 = (px - ccx) * (by - ccy) - (bx - ccx) * (py - ccy);
            float d3 = (px - ax) * (ccy - ay) - (ccx - ax) * (py - ay);
            bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
            if (!(neg && pos)) buf[y * w + x] = argb;
        }
    }
}

// ---- Accent line + date --------------------------------------------------------
//
// Accent: a daily-step progress bar under the time (x=50 y=300, 245x3). The rail
// is a full-width red line; a white fill grows over it left-to-right in
// proportion to steps / goal, so more steps = more white and a reached goal
// paints the whole line white. With no goal set (or no step data yet) it stays
// plain red. Driven by update_dot_accent() from the 1 Hz Dot tick.
//
// Date: same logic as the stock date_label (honours Show day / Show date), in
// the Dot face's compact single-line form, e.g. "THUR 15/02" (DD/MM), gray
// #9A9A9A monospace at x=50, baseline y=338.
static constexpr int DOT_ACCENT_W = 245;
static lv_obj_t *dot_accent      = nullptr;   // red rail (full width)
static lv_obj_t *dot_accent_fill = nullptr;   // white step-progress fill
static lv_obj_t *dot_date_label  = nullptr;

static void build_dot_accent_date(lv_obj_t *parent)
{
    dot_accent = lv_obj_create(parent);
    lv_obj_remove_style_all(dot_accent);
    lv_obj_set_size(dot_accent, DOT_ACCENT_W, 3);
    lv_obj_set_pos(dot_accent, 50, 300);
    lv_obj_set_style_bg_color(dot_accent, dot_red(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot_accent, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(dot_accent, LV_OBJ_FLAG_CLICKABLE);

    // White fill on top of the red rail, same origin; width set from progress.
    dot_accent_fill = lv_obj_create(parent);
    lv_obj_remove_style_all(dot_accent_fill);
    lv_obj_set_size(dot_accent_fill, 0, 3);
    lv_obj_set_pos(dot_accent_fill, 50, 300);
    lv_obj_set_style_bg_color(dot_accent_fill, dot_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot_accent_fill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(dot_accent_fill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(dot_accent_fill, LV_OBJ_FLAG_CLICKABLE);

    dot_date_label = lv_label_create(parent);
    lv_obj_set_style_text_font(dot_date_label, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(dot_date_label, lv_color_hex(0x9A9A9A), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(dot_date_label, 3, LV_PART_MAIN);
    lv_label_set_text(dot_date_label, "");
    lv_obj_set_pos(dot_date_label, 50, 324);
}

// Last painted date string, file-scope so clock_screen_apply_face_custom() can
// force a repaint when the date order changes.
static char s_dot_date_last[24] = { '\x01', '\0' };

static void update_dot_date(const struct tm *t)
{
    if (!dot_date_label) return;
    static const char *const kDay[7]  = { "SUN", "MON", "TUES", "WED", "THUR", "FRI", "SAT" };
    static const char *const kMon[12] = { "JAN","FEB","MAR","APR","MAY","JUN",
                                          "JUL","AUG","SEP","OCT","NOV","DEC" };
    int wd = (t->tm_wday >= 0 && t->tm_wday < 7)  ? t->tm_wday : 0;
    int mo = (t->tm_mon  >= 0 && t->tm_mon  < 12) ? t->tm_mon  : 0;

    // Date part per the chosen order (Tools > Face).
    char datepart[16] = "";
    if (clock_show_date) {
        int d = t->tm_mday, m = mo + 1, y = t->tm_year + 1900;
        switch (face_date_order()) {
            case FACE_ORDER_MDY:  snprintf(datepart, sizeof datepart, "%02d/%02d", m, d);       break;
            case FACE_ORDER_ISO:  snprintf(datepart, sizeof datepart, "%04d-%02d-%02d", y, m, d); break;
            case FACE_ORDER_DMON: snprintf(datepart, sizeof datepart, "%02d %s", d, kMon[mo]);   break;
            case FACE_ORDER_DMY:
            default:              snprintf(datepart, sizeof datepart, "%02d/%02d", d, m);        break;
        }
    }

    char buf[24];
    if (clock_show_day && clock_show_date) snprintf(buf, sizeof(buf), "%s %s", kDay[wd], datepart);
    else if (clock_show_day)               snprintf(buf, sizeof(buf), "%s", kDay[wd]);
    else if (clock_show_date)              snprintf(buf, sizeof(buf), "%s", datepart);
    else                                   buf[0] = '\0';

    // Only touch the label when the text actually changes (this runs at 1 Hz).
    if (strcmp(buf, s_dot_date_last) == 0) return;
    strncpy(s_dot_date_last, buf, sizeof(s_dot_date_last) - 1);
    s_dot_date_last[sizeof(s_dot_date_last) - 1] = '\0';
    lv_label_set_text(dot_date_label, buf);
}

// Paint the red step-progress fill over the white accent rail. Width tracks
// steps / daily goal (0..100%); a reached goal fills the whole rail. Cheap and
// only repaints when the pixel width actually changes.
static void update_dot_accent()
{
    if (!dot_accent_fill) return;
    int pct = (int)health_model().step_progress_pct();   // 0 when no goal / no steps
    int w   = DOT_ACCENT_W * pct / 100;
    if (w < 0) w = 0;
    if (w > DOT_ACCENT_W) w = DOT_ACCENT_W;

    static int last_w = -1;
    if (w == last_w) return;
    last_w = w;

    if (w <= 0) {
        lv_obj_add_flag(dot_accent_fill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(dot_accent_fill, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(dot_accent_fill, w);
    }
}

// Builds the Dot face layer, hidden. Populated incrementally (status row, USB
// indicator, accent, date, detection badges, bottom row); for now it carries
// the opaque background panel and the dot-matrix time raster.
static void build_dot_face(lv_obj_t *screen)
{
    dot_container = lv_obj_create(screen);
    lv_obj_remove_style_all(dot_container);
    lv_obj_set_size(dot_container, 410, 502);
    lv_obj_set_pos(dot_container, 0, 0);
    lv_obj_set_style_bg_color(dot_container, dot_bg(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot_container, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(dot_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dot_container, LV_OBJ_FLAG_HIDDEN);   // shown by set_face()

    // Time raster: ARGB8888 in PSRAM, refreshed each minute (same PSRAM
    // image-descriptor pattern as background.cpp's wallpaper rasters).
    size_t px = (size_t)DOT_TIME_W * (size_t)DOT_TIME_H;
    dot_time_buf = (uint32_t *)heap_caps_malloc(px * 4u, MALLOC_CAP_SPIRAM);
    if (dot_time_buf) {
        memset(dot_time_buf, 0, px * 4u);
        dot_time_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
        dot_time_dsc.header.cf     = LV_COLOR_FORMAT_ARGB8888;
        dot_time_dsc.header.flags  = 0;
        dot_time_dsc.header.w      = DOT_TIME_W;
        dot_time_dsc.header.h      = DOT_TIME_H;
        dot_time_dsc.header.stride = DOT_TIME_W * 4;
        dot_time_dsc.data_size     = (uint32_t)(px * 4u);
        dot_time_dsc.data          = (const uint8_t *)dot_time_buf;

        dot_time_img = lv_image_create(dot_container);
        lv_image_set_src(dot_time_img, &dot_time_dsc);
        lv_obj_set_pos(dot_time_img, DOT_TIME_X, DOT_TIME_Y);
    }

    // Font-mode hour: a big Montserrat clock label occupying the same time band,
    // hidden unless the user picks a regular hour font (Tools > Face). Centred
    // horizontally; the raster and the label are never shown at once.
    dot_time_label = lv_label_create(dot_container);
    lv_obj_set_style_text_font(dot_time_label, &lv_font_montserrat_clock_96, LV_PART_MAIN);
    lv_obj_set_style_text_color(dot_time_label, dot_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(dot_time_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(dot_time_label, DOT_TIME_W);
    lv_obj_set_pos(dot_time_label, DOT_TIME_X, DOT_TIME_Y);
    lv_label_set_text(dot_time_label, "");
    lv_obj_add_flag(dot_time_label, LV_OBJ_FLAG_HIDDEN);

    build_dot_status_row(dot_container);
    build_dot_usb(dot_container);
    build_dot_tiles(dot_container);
    build_dot_accent_date(dot_container);
    build_dot_bottom(dot_container);
    build_dot_badges(dot_container);
    build_dot_notif(dot_container);

    clock_screen_apply_face_custom();   // accent colour + date font from saved state
}

// Minute+mode key of the last painted time, file-scope so a live font switch
// (clock_screen_apply_face_custom) can force the next tick to repaint.
static int s_dot_time_key = -1;

// Refreshes the Dot face for the given local time. Renders HH:MM as white dots
// on the 5x7 custom grid (or a regular font label); honours the 12h/24h setting.
static void update_dot_face(const struct tm *t)
{
    update_dot_date(t);   // cheap, and must follow the show-day/date settings live

    int hh = t->tm_hour;
    if (clock_12h) { hh %= 12; if (hh == 0) hh = 12; }
    int mm = t->tm_min;

    // Hour digits: the dot-matrix raster (Nothing look, default) or a regular
    // Montserrat clock label. Only one is ever shown; the other is hidden.
    bool font_mode = (face_hour_font() != FACE_HOUR_DOTS);
    if (dot_time_img) {
        if (font_mode) lv_obj_add_flag(dot_time_img, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_clear_flag(dot_time_img, LV_OBJ_FLAG_HIDDEN);
    }
    if (dot_time_label) {
        if (font_mode) lv_obj_clear_flag(dot_time_label, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_add_flag(dot_time_label, LV_OBJ_FLAG_HIDDEN);
    }

    // Repaint only on a change; the key folds in the render mode so a live font
    // switch repaints at once (clock_screen_apply_face_custom() also resets it).
    // s_dot_time_key is file-scope for that reset.
    int key = (font_mode ? 100000 : 0) + hh * 100 + mm;
    if (key == s_dot_time_key) return;
    s_dot_time_key = key;

    if (font_mode) {
        if (dot_time_label) lv_label_set_text_fmt(dot_time_label, "%02d:%02d", hh, mm);
        return;
    }

    if (!dot_time_buf || !dot_time_img) return;
    int digits[4] = { hh / 10, hh % 10, mm / 10, mm % 10 };

    memset(dot_time_buf, 0, (size_t)DOT_TIME_W * (size_t)DOT_TIME_H * 4u);
    const uint32_t white = 0xFFFFFFFFu;   // ARGB8888, opaque white

    for (int d = 0; d < 4; d++) {
        for (int row = 0; row < DOT_GLYPH_ROWS; row++) {
            for (int col = 0; col < DOT_GLYPH_COLS; col++) {
                if (!dot_glyph_lit(digits[d], col, row)) continue;
                float cx = DOT_DIGIT_X[d] + (float)(col * DOT_CELL) - DOT_TIME_X;
                float cy = (float)(DOT_ROW_Y0 + row * DOT_CELL)     - DOT_TIME_Y;
                dot_plot_disc(dot_time_buf, DOT_TIME_W, DOT_TIME_H, cx, cy, DOT_R, white);
            }
        }
    }
    dot_plot_disc(dot_time_buf, DOT_TIME_W, DOT_TIME_H,
                  DOT_COLON_X - DOT_TIME_X, (float)(DOT_COLON_Y0 - DOT_TIME_Y), DOT_COLON_R, white);
    dot_plot_disc(dot_time_buf, DOT_TIME_W, DOT_TIME_H,
                  DOT_COLON_X - DOT_TIME_X, (float)(DOT_COLON_Y1 - DOT_TIME_Y), DOT_COLON_R, white);

    // Re-point the image at its (mutated) buffer so LVGL drops any cached decode
    // and re-reads the pixels, mirroring background.cpp's refresh.
    lv_image_set_src(dot_time_img, NULL);
    lv_image_set_src(dot_time_img, &dot_time_dsc);
    lv_obj_invalidate(dot_time_img);
}

// ---- Status row --------------------------------------------------------------
//
// The six line-art icons (LoRa, SD, Bluetooth, WiFi, Wardriver, GPS) are drawn
// into one ARGB8888 sprite that spans the icon band; NFC and the mesh count are
// labels, the mesh badge a pill. Coordinates below are the exact face-space
// values from dotface_final.svg; each icon draws at its absolute position minus
// the sprite origin. Colours: white = active, gray = idle, per the same state
// predicates the stock status icons already read.
static constexpr int DOT_STAT_X = 44;    // extended left (was 96) so the heart fills the far-left corner
static constexpr int DOT_STAT_Y = 44;
static constexpr int DOT_STAT_W = 304;   // covers x 44..348
static constexpr int DOT_STAT_H = 32;    // covers y 44..76

static void dot_draw_lora(uint32_t *b, int w, int h, uint32_t c)
{
    const float ox = DOT_STAT_X, oy = DOT_STAT_Y;
    dot_plot_seg (b, w, h, 107 - ox, 60 - oy, 107 - ox, 70 - oy, 1.8f, c);   // stick
    dot_plot_disc(b, w, h, 107 - ox, 58 - oy, 3.2f, c);                      // ball
    dot_plot_arc (b, w, h, 107 - ox, 58 - oy, 5.0f, 180, 360, 1.8f, c);      // top arc
}

static void dot_draw_sd(uint32_t *b, int w, int h, uint32_t c)
{
    const float ox = DOT_STAT_X, oy = DOT_STAT_Y;
    // Card outline with the cut top-right corner (closed polyline).
    const float px[6] = { 184-ox, 192-ox, 196-ox, 196-ox, 184-ox, 184-ox };
    const float py[6] = { 49-oy,  49-oy,  53-oy,  67-oy,  67-oy,  49-oy  };
    for (int i = 0; i < 5; i++) dot_plot_seg(b, w, h, px[i], py[i], px[i+1], py[i+1], 1.6f, c);
    dot_plot_seg(b, w, h, 188-ox, 62-oy, 188-ox, 67-oy, 1.6f, c);   // contacts
    dot_plot_seg(b, w, h, 192-ox, 62-oy, 192-ox, 67-oy, 1.6f, c);
}

static void dot_draw_bt(uint32_t *b, int w, int h, uint32_t c)
{
    const float ox = DOT_STAT_X, oy = DOT_STAT_Y;
    const float ax[4] = { 222-ox, 226-ox, 218-ox, 222-ox }, ay[4] = { 50-oy, 54-oy, 62-oy, 66-oy };
    const float bx[4] = { 222-ox, 218-ox, 226-ox, 222-ox }, by[4] = { 50-oy, 54-oy, 62-oy, 66-oy };
    for (int i = 0; i < 3; i++) dot_plot_seg(b, w, h, ax[i], ay[i], ax[i+1], ay[i+1], 1.6f, c);
    for (int i = 0; i < 3; i++) dot_plot_seg(b, w, h, bx[i], by[i], bx[i+1], by[i+1], 1.6f, c);
}

static void dot_draw_wifi(uint32_t *b, int w, int h, uint32_t c)
{
    const float ox = DOT_STAT_X, oy = DOT_STAT_Y;
    dot_plot_arc (b, w, h, 258-ox, 62-oy, 9.0f, 180, 360, 1.7f, c);
    dot_plot_arc (b, w, h, 258-ox, 65-oy, 6.0f, 180, 360, 1.7f, c);
    dot_plot_arc (b, w, h, 258-ox, 68-oy, 3.0f, 180, 360, 1.7f, c);
    dot_plot_disc(b, w, h, 258-ox, 70.5f-oy, 1.3f, c);
}

static void dot_draw_radar(uint32_t *b, int w, int h, uint32_t c)
{
    const float ox = DOT_STAT_X, oy = DOT_STAT_Y;
    dot_plot_arc (b, w, h, 295-ox, 58-oy, 7.0f, 0, 360, 1.4f, c);    // outer ring
    dot_plot_arc (b, w, h, 295-ox, 58-oy, 3.5f, 0, 360, 1.1f, c);    // inner ring
    dot_plot_seg (b, w, h, 295-ox, 58-oy, 301-ox, 54-oy, 1.6f, c);   // sweep
    dot_plot_disc(b, w, h, 295-ox, 58-oy, 1.2f, c);                  // centre
}

static void dot_draw_gps(uint32_t *b, int w, int h, uint32_t c)
{
    const float ox = DOT_STAT_X, oy = DOT_STAT_Y;
    const float cx = 330 - ox, cy = 58 - oy;
    dot_plot_disc(b, w, h, cx, cy - 2, 7.0f, c);                                 // bulb
    dot_fill_tri (b, w, h, cx - 6.0f, cy - 1.0f, cx + 6.0f, cy - 1.0f, cx, cy + 9.0f, c); // point
    dot_plot_disc(b, w, h, cx, cy - 2, 2.2f, 0x00000000u);                       // hole
}

// Leftmost status icon: a small filled heart at cx=63 that lights when the
// phone relay is actively feeding health data (red = live, gray = idle). It
// fills the far-left corner of the status row; the Meshtastic unread pill sits
// just to its right. Two round lobes plus a downward point, the classic
// silhouette.
static void dot_draw_heart(uint32_t *b, int w, int h, uint32_t c)
{
    const float ox = DOT_STAT_X, oy = DOT_STAT_Y;
    const float cx = 63 - ox, cy = 59 - oy;
    dot_plot_disc(b, w, h, cx - 3.0f, cy - 2.0f, 3.4f, c);                        // left lobe
    dot_plot_disc(b, w, h, cx + 3.0f, cy - 2.0f, 3.4f, c);                        // right lobe
    dot_fill_tri (b, w, h, cx - 6.2f, cy - 1.0f, cx + 6.2f, cy - 1.0f, cx, cy + 7.0f, c); // point
}

// Creates the status-row widgets, hidden state driven later by update_dot_status().
static void build_dot_status_row(lv_obj_t *parent)
{
    size_t px = (size_t)DOT_STAT_W * (size_t)DOT_STAT_H;
    dot_status_buf = (uint32_t *)heap_caps_malloc(px * 4u, MALLOC_CAP_SPIRAM);
    if (dot_status_buf) {
        memset(dot_status_buf, 0, px * 4u);
        dot_status_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
        dot_status_dsc.header.cf     = LV_COLOR_FORMAT_ARGB8888;
        dot_status_dsc.header.flags  = 0;
        dot_status_dsc.header.w      = DOT_STAT_W;
        dot_status_dsc.header.h      = DOT_STAT_H;
        dot_status_dsc.header.stride = DOT_STAT_W * 4;
        dot_status_dsc.data_size     = (uint32_t)(px * 4u);
        dot_status_dsc.data          = (const uint8_t *)dot_status_buf;
        dot_status_img = lv_image_create(parent);
        lv_image_set_src(dot_status_img, &dot_status_dsc);
        lv_obj_set_pos(dot_status_img, DOT_STAT_X, DOT_STAT_Y);
    }

    // NFC label, centred on x=146 (face centre is 205).
    dot_nfc_label = lv_label_create(parent);
    lv_obj_set_style_text_font(dot_nfc_label, &font_argus_mono_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(dot_nfc_label, dot_white(), LV_PART_MAIN);
    lv_label_set_text(dot_nfc_label, "NFC");
    lv_obj_align(dot_nfc_label, LV_ALIGN_TOP_MID, 146 - 205, 50);

    // The unread indicator moved out of the status row into a persistent
    // bottom-left notifications button (build_dot_notif); nothing else lives here.
}

// Persistent notifications button: bottom-left accent square, always visible and
// tappable (opens the notifications screen). A white envelope shows inside while
// there are unread notifications; tapping marks the current batch seen so the
// envelope clears on return. Accent colour is re-applied by
// clock_screen_apply_face_custom().
static void on_dot_notif_clicked(lv_event_t *)
{
    // Acknowledge the current batch (mark read) before opening the list, so the
    // envelope is clear when the wearer returns home; new arrivals re-light it.
    s_notif_seen = (uint32_t)notify::center().count();
    meshtastic_mark_read();
    notifications_screen_show();
}

static void build_dot_notif(lv_obj_t *parent)
{
    dot_notif_btn = lv_obj_create(parent);
    lv_obj_remove_style_all(dot_notif_btn);
    lv_obj_set_size(dot_notif_btn, 46, 46);
    lv_obj_set_pos(dot_notif_btn, 22, 418);
    lv_obj_set_style_radius(dot_notif_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot_notif_btn, lv_color_hex(face_accent_rgb()), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot_notif_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(dot_notif_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dot_notif_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(dot_notif_btn, on_dot_notif_clicked, LV_EVENT_CLICKED, NULL);

    dot_notif_env = lv_label_create(dot_notif_btn);
    lv_obj_set_style_text_font(dot_notif_env, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(dot_notif_env, dot_white(), LV_PART_MAIN);
    lv_label_set_text(dot_notif_env, LV_SYMBOL_ENVELOPE);
    lv_obj_center(dot_notif_env);
    lv_obj_add_flag(dot_notif_env, LV_OBJ_FLAG_HIDDEN);   // shown by update_dot_status when unread
}

// Recolours the status icons from the same live predicates the stock status
// bar reads, and refreshes the NFC label + mesh badge. Only redraws the sprite
// on a state edge so the 1 Hz tick stays cheap.
static void update_dot_status()
{
    if (!dot_status_buf || !dot_status_img) return;

    bool lora = lora_screen_is_powered() || pager_is_running() || tpms_is_running()
             || aprs_is_running() || lora_analyze_is_running();
    bool bt   = btStarted();
    wifi_mode_t wm = WIFI_MODE_NULL; esp_wifi_get_mode(&wm);
    bool wifi = (wm != WIFI_MODE_NULL);
    bool sd   = instance.isCardReady();
    bool nfc  = instance.pmu.isEnableDLDO1();
    bool wd   = wardriver_is_running();
    bool gps  = gps_screen_is_powered();
    bool hlth = health_data_fresh();
    // Unread indicator (drives the bottom-left envelope): phone notifications
    // beyond the last-seen baseline, or any unread Meshtastic message. The
    // baseline can only be as high as the current stored count (notifs may be
    // retracted or cleared), so clamp it down first.
    int notify_cnt = (int)notify::center().count();
    if (notify_cnt < 0) notify_cnt = 0;
    if ((uint32_t)notify_cnt < s_notif_seen) s_notif_seen = (uint32_t)notify_cnt;
    bool has_unread = ((uint32_t)notify_cnt > s_notif_seen) || (meshtastic_get_unread() > 0);

    uint32_t state = (uint32_t)lora | (uint32_t)bt << 1 | (uint32_t)wifi << 2
                   | (uint32_t)sd << 3 | (uint32_t)nfc << 4 | (uint32_t)wd << 5
                   | (uint32_t)gps << 6 | (uint32_t)has_unread << 7
                   | (uint32_t)hlth << 8;
    static uint32_t last_state = 0xFFFFFFFFu;
    if (state == last_state) return;
    last_state = state;

    const uint32_t W = 0xFFFFFFFFu, G = 0xFF5C5C5Cu;   // opaque white / gray
    memset(dot_status_buf, 0, (size_t)DOT_STAT_W * (size_t)DOT_STAT_H * 4u);
    dot_draw_lora (dot_status_buf, DOT_STAT_W, DOT_STAT_H, lora ? W : G);
    dot_draw_sd   (dot_status_buf, DOT_STAT_W, DOT_STAT_H, sd   ? W : G);
    dot_draw_bt   (dot_status_buf, DOT_STAT_W, DOT_STAT_H, bt   ? W : G);
    dot_draw_wifi (dot_status_buf, DOT_STAT_W, DOT_STAT_H, wifi ? W : G);
    dot_draw_radar(dot_status_buf, DOT_STAT_W, DOT_STAT_H, wd   ? W : G);
    dot_draw_gps  (dot_status_buf, DOT_STAT_W, DOT_STAT_H, gps  ? W : G);
    dot_draw_heart(dot_status_buf, DOT_STAT_W, DOT_STAT_H, hlth ? 0xFFE53935u : G);   // red when live
    lv_image_set_src(dot_status_img, NULL);
    lv_image_set_src(dot_status_img, &dot_status_dsc);
    lv_obj_invalidate(dot_status_img);

    lv_obj_set_style_text_color(dot_nfc_label, nfc ? dot_white() : dot_gray(), LV_PART_MAIN);

    // Envelope glyph on the persistent notifications button: shown while unread,
    // hidden once the batch has been marked seen. No count — a plain mail icon.
    if (dot_notif_env) {
        if (has_unread) lv_obj_clear_flag(dot_notif_env, LV_OBJ_FLAG_HIDDEN);
        else            lv_obj_add_flag(dot_notif_env, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---- USB connection indicator ------------------------------------------------
//
// Two rows of 8 dots at y=130 (left x=55..153, right x=247..345, 14 px pitch).
// Idle: static gray dots, no label. As soon as USB is present, every dot loops
// white -> red -> white with a ~80 ms cascade from left to right (a wave), and
// the centre label says why:
//   charge only   bolt   centred at x=200
//   data only     "DATA" centred at x=200
//   both          bolt at x=178 + "DATA" at x=208 (grouped, centred)
// "Charge" is the debounced PMU state (bat_charge: Charging or Topped, i.e.
// VBUS present). "Data" is the USB-SD mass-storage mode (usb_sd_is_running()),
// the only host data transfer this firmware can observe: TinyUSB is not up at
// boot, so a plain cable to a PC with no mode active reads as charge only.
//
// 16 lightweight objects animated by one lv_timer; the timer only runs while
// the Dot face is on screen and USB is present.
static constexpr int      DOT_USB_N        = 16;
static constexpr int      DOT_USB_Y        = 130;
static constexpr int      DOT_USB_R        = 3;
static constexpr uint32_t DOT_USB_TICK_MS  = 40;     // ~25 fps
static constexpr uint32_t DOT_USB_PERIOD   = 1200;   // one white->red->white cycle
static constexpr uint32_t DOT_USB_STAGGER  = 80;     // cascade delay per dot

static lv_obj_t   *dot_usb_dots[DOT_USB_N];
static lv_obj_t   *dot_usb_bolt  = nullptr;
static lv_obj_t   *dot_usb_data  = nullptr;
static lv_timer_t *dot_usb_timer = nullptr;
static bool        dot_usb_live  = false;   // wave currently running

static int dot_usb_x(int i)
{
    return (i < 8) ? 55 + i * 14 : 247 + (i - 8) * 14;
}

static void dot_usb_set_all(lv_color_t c)
{
    for (int i = 0; i < DOT_USB_N; i++)
        lv_obj_set_style_bg_color(dot_usb_dots[i], c, LV_PART_MAIN);
}

// Show/hide the whole dot line. When unplugged the row belongs to the data
// tiles, so the dots are hidden rather than left as an idle gray strip.
static void dot_usb_set_dots_hidden(bool hidden)
{
    for (int i = 0; i < DOT_USB_N; i++) {
        if (hidden) lv_obj_add_flag(dot_usb_dots[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_clear_flag(dot_usb_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

// Wave frame: each dot's phase lags its left neighbour by DOT_USB_STAGGER.
// Red weight follows a raised cosine, so 0 = white, peak = full red.
static void dot_usb_anim_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    for (int i = 0; i < DOT_USB_N; i++) {
        uint32_t ph = (now + DOT_USB_PERIOD * 16 - (uint32_t)i * DOT_USB_STAGGER) % DOT_USB_PERIOD;
        float    k  = 0.5f - 0.5f * cosf(6.2831853f * (float)ph / (float)DOT_USB_PERIOD);
        lv_obj_set_style_bg_color(dot_usb_dots[i],
            lv_color_mix(dot_red(), dot_white(), (uint8_t)(k * 255.0f)), LV_PART_MAIN);
    }
}

static void build_dot_usb(lv_obj_t *parent)
{
    for (int i = 0; i < DOT_USB_N; i++) {
        lv_obj_t *d = lv_obj_create(parent);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, DOT_USB_R * 2, DOT_USB_R * 2);
        lv_obj_set_pos(d, dot_usb_x(i) - DOT_USB_R, DOT_USB_Y - DOT_USB_R);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(d, dot_seg_empty(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
        dot_usb_dots[i] = d;
    }

    dot_usb_bolt = lv_label_create(parent);
    lv_obj_set_style_text_font(dot_usb_bolt, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(dot_usb_bolt, dot_white(), LV_PART_MAIN);
    lv_label_set_text(dot_usb_bolt, LV_SYMBOL_CHARGE);
    lv_obj_add_flag(dot_usb_bolt, LV_OBJ_FLAG_HIDDEN);

    dot_usb_data = lv_label_create(parent);
    lv_obj_set_style_text_font(dot_usb_data, &font_argus_mono_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(dot_usb_data, dot_white(), LV_PART_MAIN);
    lv_label_set_text(dot_usb_data, "DATA");
    lv_obj_add_flag(dot_usb_data, LV_OBJ_FLAG_HIDDEN);

    dot_usb_timer = lv_timer_create(dot_usb_anim_cb, DOT_USB_TICK_MS, NULL);
    lv_timer_pause(dot_usb_timer);
}

// Park the wave: timer paused, dots back to idle gray. Safe to call repeatedly.
static void dot_usb_stop()
{
    if (!dot_usb_timer || !dot_usb_live) return;
    lv_timer_pause(dot_usb_timer);
    dot_usb_set_all(dot_seg_empty());
    dot_usb_live = false;
}

// 1 Hz: work out the USB state, start/stop the wave and place the label(s).
static void update_dot_usb()
{
    if (!dot_usb_timer) return;

    bool charge = bat_charge.state() != ChargeState::Discharging;
    bool data   = usb_sd_is_running();
    bool usb    = charge || data;

    // Unplugged: hand the row to the two data tiles and hide the dot line.
    // Plugged: the charge/data wave owns the row and the tiles step aside.
    dot_usb_set_dots_hidden(!usb);
    update_dot_tiles(usb);

    // Label layout only changes on a state edge.
    static int last = -1;
    int key = (charge ? 1 : 0) | (data ? 2 : 0);
    if (key != last) {
        last = key;
        // Centre each label on its x; y centres on the dot row (~127 / ~134
        // baselines in the SVG). Face centre x is 205.
        if (charge) {
            lv_obj_align(dot_usb_bolt, LV_ALIGN_TOP_MID, (data ? 178 : 200) - 205, DOT_USB_Y - 11);
            lv_obj_clear_flag(dot_usb_bolt, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(dot_usb_bolt, LV_OBJ_FLAG_HIDDEN);
        }
        if (data) {
            lv_obj_align(dot_usb_data, LV_ALIGN_TOP_MID, (charge ? 208 : 200) - 205, DOT_USB_Y - 9);
            lv_obj_clear_flag(dot_usb_data, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(dot_usb_data, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (usb) {
        if (!dot_usb_live) {
            lv_timer_resume(dot_usb_timer);
            dot_usb_live = true;
        }
    } else {
        dot_usb_stop();
    }
}

// ---- Customizable data tiles (two slots on the y=130 row) -------------------
//
// When the watch is unplugged the USB dot line has nothing to show, so it is
// replaced by two tiles the wearer picks (tap a slot -> choose an indicator;
// long-press to re-pick). Choices persist in NVS (dot_tiles.*). One indicator is
// a button (Meshtastic) whose tap opens the chat; the rest are read-outs mirrored
// from the health model. Plugged back in, the charge/data wave reclaims the row.
static lv_obj_t *dot_tile_hit[2] = { nullptr, nullptr };
static lv_obj_t *dot_tile_tag[2] = { nullptr, nullptr };
static lv_obj_t *dot_tile_val[2] = { nullptr, nullptr };
static constexpr int DOT_TILE_CX[2] = { 104, 296 };   // left / right group centres

// Kind -> short tag (shown above the value) + menu label (shown in the picker).
struct DotTileDef { DotTileKind kind; const char *tag; const char *menu; };
static const DotTileDef kDotTileDefs[] = {
    { DOT_TILE_SLEEP,     "SLEEP", "Sleep score"    },
    { DOT_TILE_STEP_GOAL, "GOAL",  "Step goal"      },
    { DOT_TILE_STEPS,     "STEPS", "Daily steps"    },
    { DOT_TILE_BPM,       "BPM",   "BPM (high/low)" },
    { DOT_TILE_MESH,      "LoRa",  "Meshtastic chat"},
    { DOT_TILE_FIND,      "FIND",  "Find (ring phone)"},
};
static constexpr int DOT_TILE_DEF_N = sizeof(kDotTileDefs) / sizeof(kDotTileDefs[0]);

static void open_tile_picker(int slot);
bool find_ring_phone();   // defined below: notify the phone to ring
static void sys_notify(uint32_t uid, const char *title, const char *body);  // defined below
static constexpr uint32_t SYS_UID_FIND = 0x5A5E0005;

static void on_dot_tile_short(lv_event_t *e)
{
    int s = (int)(intptr_t)lv_event_get_user_data(e);
    if (s < 0 || s > 1) return;
    DotTileKind k = dot_tiles_get(s);
    // Button tiles act on tap; a read-out tile (or empty slot) opens the picker
    // so a plain tap can (re)choose it.
    if (k == DOT_TILE_MESH) { meshtastic_screen_show(); return; }
    if (k == DOT_TILE_FIND) {
        bool ok = find_ring_phone();
        sys_notify(SYS_UID_FIND, "Find",
                   ok ? "Ringing your phone..." : "Phone not connected");
        return;
    }
    open_tile_picker(s);
}

static void on_dot_tile_long(lv_event_t *e)
{
    int s = (int)(intptr_t)lv_event_get_user_data(e);
    if (s >= 0 && s <= 1) open_tile_picker(s);   // long-press always re-picks
}

// Two slots on the idle USB line. Each is a single centred line, TITLE then
// value left-to-right, using most of the tile width for a big, all-white
// read-out. Montserrat (not a subset font) guarantees the digits, '/', '%' and
// 'k' always render.
static constexpr int DOT_TILE_W = 172;
static constexpr int DOT_TILE_H = 46;

static void build_dot_tiles(lv_obj_t *parent)
{
    for (int s = 0; s < 2; s++) {
        lv_obj_t *hit = lv_obj_create(parent);
        lv_obj_remove_style_all(hit);
        lv_obj_set_size(hit, DOT_TILE_W, DOT_TILE_H);
        lv_obj_set_pos(hit, DOT_TILE_CX[s] - DOT_TILE_W / 2, DOT_USB_Y - DOT_TILE_H / 2);
        lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(hit, LV_OBJ_FLAG_HIDDEN);   // shown by update_dot_tiles when unplugged
        // Title + value on one row, centred together (a hidden label drops out of
        // the flex, so single-item states self-centre).
        lv_obj_set_flex_flow(hit, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(hit, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(hit, 8, LV_PART_MAIN);
        lv_obj_add_event_cb(hit, on_dot_tile_short, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)s);
        lv_obj_add_event_cb(hit, on_dot_tile_long,  LV_EVENT_LONG_PRESSED,  (void *)(intptr_t)s);

        lv_obj_t *tag = lv_label_create(hit);
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(tag, dot_white(), LV_PART_MAIN);
        lv_obj_set_style_text_letter_space(tag, 1, LV_PART_MAIN);
        lv_label_set_text(tag, "");
        lv_obj_clear_flag(tag, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *val = lv_label_create(hit);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(val, dot_white(), LV_PART_MAIN);
        lv_label_set_text(val, "");
        lv_obj_clear_flag(val, LV_OBJ_FLAG_CLICKABLE);

        dot_tile_hit[s] = hit;
        dot_tile_tag[s] = tag;
        dot_tile_val[s] = val;
    }
}

// Compact step count: 842, 8.3k, 12k.
static void dot_tile_fmt_steps(char *buf, size_t n, uint32_t steps)
{
    if (steps >= 10000)     snprintf(buf, n, "%luk", (unsigned long)(steps / 1000));
    else if (steps >= 1000) snprintf(buf, n, "%lu.%luk",
                                     (unsigned long)(steps / 1000), (unsigned long)((steps % 1000) / 100));
    else                    snprintf(buf, n, "%lu", (unsigned long)steps);
}

// Paint one slot from its current kind. Values gray out when stale or missing.
static void dot_tile_render(int s)
{
    lv_obj_t *tag = dot_tile_tag[s];
    lv_obj_t *val = dot_tile_val[s];
    if (!tag || !val) return;

    DotTileKind k = dot_tiles_get(s);

    // Empty slot: a single centred "+ add" invite (the title label drops out).
    if (k == DOT_TILE_NONE) {
        lv_obj_add_flag(tag, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_set_style_text_color(val, dot_gray(), LV_PART_MAIN);
        lv_label_set_text(val, "+ add");
        return;
    }
    lv_obj_clear_flag(tag, LV_OBJ_FLAG_HIDDEN);

    // Meshtastic button: title + envelope glyph, always "live" (white).
    if (k == DOT_TILE_MESH) {
        lv_obj_set_style_text_color(tag, dot_white(), LV_PART_MAIN);
        lv_label_set_text(tag, "LoRa");
        lv_obj_set_style_text_font(val, &lv_font_montserrat_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(val, dot_white(), LV_PART_MAIN);
        lv_label_set_text(val, LV_SYMBOL_ENVELOPE);
        return;
    }

    // Find button: title + bell glyph. One tap rings the phone.
    if (k == DOT_TILE_FIND) {
        lv_obj_set_style_text_color(tag, dot_white(), LV_PART_MAIN);
        lv_label_set_text(tag, "FIND");
        lv_obj_set_style_text_font(val, &lv_font_montserrat_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(val, dot_white(), LV_PART_MAIN);
        lv_label_set_text(val, LV_SYMBOL_BELL);
        return;
    }

    // Read-out tiles from the health model.
    health::HealthData &h = health_model();
    uint32_t now = millis();
    const char *tagtxt = "";
    char buf[16] = "--";
    lv_color_t col = dot_gray();   // default: no data -> gray "--"

    switch (k) {
    case DOT_TILE_SLEEP:
        tagtxt = "SLEEP";
        if (h.has_sleep_score()) {
            snprintf(buf, sizeof buf, "%u", h.sleep_score());
            col = h.sleep_stale(now) ? dot_gray() : dot_white();
        }
        break;
    case DOT_TILE_STEP_GOAL:
        tagtxt = "GOAL";
        if (h.step_goal() > 0) {
            snprintf(buf, sizeof buf, "%u%%", h.step_progress_pct());
            col = h.steps_stale(now) ? dot_gray() : dot_white();
        }
        break;
    case DOT_TILE_STEPS:
        tagtxt = "STEPS";
        if (h.has_steps()) {
            dot_tile_fmt_steps(buf, sizeof buf, h.steps());
            col = h.steps_stale(now) ? dot_gray() : dot_white();
        }
        break;
    case DOT_TILE_BPM:
        tagtxt = "BPM";
        if (h.has_hr_range()) {
            snprintf(buf, sizeof buf, "%u/%u", h.hr_high(), h.hr_low());
            col = h.hr_range_stale(now) ? dot_gray() : dot_white();
        } else if (h.has_hr()) {
            snprintf(buf, sizeof buf, "%u", h.hr());
            col = h.hr_stale(now) ? dot_gray() : dot_white();
        }
        break;
    default:
        break;
    }

    // Title always white for legibility; value white when live, grey when the
    // reading is stale or missing (so "old data" still reads at a glance).
    lv_obj_set_style_text_color(tag, dot_white(), LV_PART_MAIN);
    lv_label_set_text(tag, tagtxt);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(val, col, LV_PART_MAIN);
    lv_label_set_text(val, buf);
}

static void update_dot_tiles(bool usb_present)
{
    for (int s = 0; s < 2; s++) {
        if (!dot_tile_hit[s]) continue;
        if (usb_present) {
            lv_obj_add_flag(dot_tile_hit[s], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(dot_tile_hit[s], LV_OBJ_FLAG_HIDDEN);
            dot_tile_render(s);
        }
    }
}

// ---- Tile picker (tap a slot) -----------------------------------------------
static lv_obj_t *s_tile_picker = nullptr;

static void close_tile_picker()
{
    if (s_tile_picker) { lv_obj_delete_async(s_tile_picker); s_tile_picker = nullptr; }
}

static void on_tile_picker_scrim(lv_event_t *e)
{
    // Only a tap on the scrim itself (outside the card) closes without a change.
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) close_tile_picker();
}

static void on_tile_picker_choice(lv_event_t *e)
{
    int packed = (int)(intptr_t)lv_event_get_user_data(e);
    int slot = (packed >> 8) & 0xFF;
    DotTileKind kind = (DotTileKind)(packed & 0xFF);
    dot_tiles_set(slot, kind);
    close_tile_picker();
    update_dot_tiles(false);   // repaint now (the picker is only reachable unplugged)
}

static void open_tile_picker(int slot)
{
    if (s_tile_picker || slot < 0 || slot > 1) return;

    DotTileKind current = dot_tiles_get(slot);
    DotTileKind other   = dot_tiles_get(slot ^ 1);

    // Offer every indicator except the one the OTHER slot already holds (unless
    // it is this slot's own current pick, so re-opening shows it selected).
    DotTileKind rows[DOT_TILE_DEF_N];
    int nrows = 0;
    for (int i = 0; i < DOT_TILE_DEF_N; i++) {
        DotTileKind k = kDotTileDefs[i].kind;
        if (k == other && k != current) continue;
        rows[nrows++] = k;
    }
    bool with_clear = (current != DOT_TILE_NONE);
    int total = nrows + (with_clear ? 1 : 0);

    // Dark scrim over everything; tap outside the card to dismiss.
    s_tile_picker = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_tile_picker);
    lv_obj_set_size(s_tile_picker, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_tile_picker, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_tile_picker, 160, LV_PART_MAIN);
    lv_obj_add_flag(s_tile_picker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_tile_picker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_tile_picker, on_tile_picker_scrim, LV_EVENT_CLICKED, NULL);

    int card_h = 12 + 30 + total * 46 + 12;
    lv_obj_t *card = lv_obj_create(s_tile_picker);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 264, card_h);
    lv_obj_center(card);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);   // swallow taps so they don't dismiss
    lv_obj_set_style_radius(card, 22, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, dot_bg(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(card, 60, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(card);
    lv_obj_set_style_text_font(title, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, dot_gray(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(title, 2, LV_PART_MAIN);
    lv_label_set_text(title, slot == 0 ? "LEFT SLOT" : "RIGHT SLOT");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    int y = 42;
    for (int i = 0; i < total; i++) {
        bool clear_row = (with_clear && i == nrows);
        DotTileKind k  = clear_row ? DOT_TILE_NONE : rows[i];
        const char *label = "Clear slot";
        if (!clear_row)
            for (int d = 0; d < DOT_TILE_DEF_N; d++)
                if (kDotTileDefs[d].kind == k) { label = kDotTileDefs[d].menu; break; }
        bool selected = (k == current);

        lv_obj_t *row = lv_obj_create(card);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 240, 40);
        lv_obj_set_pos(row, 12, y);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(row, 12, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, selected ? 34 : 12, LV_PART_MAIN);
        if (selected) {
            lv_obj_set_style_border_color(row, dot_red(), LV_PART_MAIN);   // on-palette accent
            lv_obj_set_style_border_opa(row, 220, LV_PART_MAIN);
            lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        }
        lv_obj_add_event_cb(row, on_tile_picker_choice, LV_EVENT_CLICKED,
                            (void *)(intptr_t)((slot << 8) | (int)k));

        lv_obj_t *rl = lv_label_create(row);
        lv_obj_set_style_text_font(rl, theme_text_font(16), LV_PART_MAIN);
        lv_obj_set_style_text_color(rl, clear_row ? dot_gray() : lv_color_white(), LV_PART_MAIN);
        lv_label_set_text(rl, label);
        lv_obj_align(rl, LV_ALIGN_LEFT_MID, 14, 0);
        lv_obj_clear_flag(rl, LV_OBJ_FLAG_CLICKABLE);

        y += 46;
    }
}

// ---- Bottom row: stopwatch / timer / alarm + 13-segment battery -------------
//
// Same state as the stock face (stopwatch_is_running, timer_is_running,
// alarm_is_enabled, PMU battery %), but at the fixed Dot positions instead of
// the stock right-to-left packing: icons are always drawn, white when active,
// gray when idle. Battery is 13 discrete segments (11 px wide, 3 px gap, from
// x=140), white when filled, #3A3A3A when empty; the percentage is anchored on
// its RIGHT edge at x=350.88 so it stays aligned from "0%" to "100%".
// Stopwatch / timer / alarm-bell status icons. Moved up beside the date row
// (was the bottom-left corner at 52,424) so they sit to the RIGHT of the date;
// the freed bottom-left corner now holds the notifications button. The icon
// shapes are drawn at raster-local (absolute - origin), so origin and every
// absolute literal in the three draw helpers below shifted by the same delta
// (+198, -106): the sprite renders identically, only relocated.
static constexpr int DOT_BOT_X = 250;   // covers x 250..328 (right of the date)
static constexpr int DOT_BOT_Y = 318;   // covers y 318..344 (date-row level)
static constexpr int DOT_BOT_W = 78;
static constexpr int DOT_BOT_H = 26;
static constexpr int DOT_BAT_SEGS = 12;   // 12 (was 13): one fewer frees room for the % text

static lv_obj_t *dot_bot_img = nullptr;
static uint32_t *dot_bot_buf = nullptr;
static lv_image_dsc_t dot_bot_dsc;
static lv_obj_t *dot_bat_seg[DOT_BAT_SEGS];
static lv_obj_t *dot_bat_pct = nullptr;

static void dot_fill_rect(uint32_t *b, int w, int h, int x0, int y0, int x1, int y1, uint32_t c)
{
    for (int y = y0; y < y1; y++) {
        if (y < 0 || y >= h) continue;
        for (int x = x0; x < x1; x++)
            if (x >= 0 && x < w) b[y * w + x] = c;
    }
}

static void dot_draw_stopwatch(uint32_t *b, int w, int h, uint32_t c)   // chronometre, cx=63
{
    const float ox = DOT_BOT_X, oy = DOT_BOT_Y;
    dot_plot_arc(b, w, h, 261 - ox, 332 - oy, 8.0f, 0, 360, 1.6f, c);
    dot_plot_seg(b, w, h, 261 - ox, 332 - oy, 265 - ox, 327 - oy, 1.6f, c);
}

static void dot_draw_timer(uint32_t *b, int w, int h, uint32_t c)       // minuteur, cx=89
{
    const float ox = DOT_BOT_X, oy = DOT_BOT_Y;
    dot_plot_arc(b, w, h, 287 - ox, 332 - oy, 8.0f, 0, 360, 1.6f, c);
    dot_fill_rect(b, w, h, 283 - DOT_BOT_X, 321 - DOT_BOT_Y, 291 - DOT_BOT_X, 324 - DOT_BOT_Y, c);   // cap
    dot_plot_seg(b, w, h, 287 - ox, 332 - oy, 283 - ox, 327 - oy, 1.6f, c);
}

static void dot_draw_bell(uint32_t *b, int w, int h, uint32_t c)        // alarme, x=118
{
    const float ox = DOT_BOT_X, oy = DOT_BOT_Y;
    dot_plot_disc(b, w, h, 316 - ox, 330 - oy, 6.0f, c);                              // dome
    dot_fill_rect(b, w, h, 310 - DOT_BOT_X, 330 - DOT_BOT_Y, 322 - DOT_BOT_X, 334 - DOT_BOT_Y, c); // body
    dot_fill_tri (b, w, h, 310 - ox, 334 - oy, 322 - ox, 334 - oy, 324 - ox, 337 - oy, c);   // flare
    dot_fill_tri (b, w, h, 310 - ox, 334 - oy, 324 - ox, 337 - oy, 308 - ox, 337 - oy, c);
    dot_plot_disc(b, w, h, 316 - ox, 339 - oy, 1.6f, c);                              // clapper
}

static void build_dot_bottom(lv_obj_t *parent)
{
    size_t px = (size_t)DOT_BOT_W * (size_t)DOT_BOT_H;
    dot_bot_buf = (uint32_t *)heap_caps_malloc(px * 4u, MALLOC_CAP_SPIRAM);
    if (dot_bot_buf) {
        memset(dot_bot_buf, 0, px * 4u);
        dot_bot_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
        dot_bot_dsc.header.cf     = LV_COLOR_FORMAT_ARGB8888;
        dot_bot_dsc.header.flags  = 0;
        dot_bot_dsc.header.w      = DOT_BOT_W;
        dot_bot_dsc.header.h      = DOT_BOT_H;
        dot_bot_dsc.header.stride = DOT_BOT_W * 4;
        dot_bot_dsc.data_size     = (uint32_t)(px * 4u);
        dot_bot_dsc.data          = (const uint8_t *)dot_bot_buf;
        dot_bot_img = lv_image_create(parent);
        lv_image_set_src(dot_bot_img, &dot_bot_dsc);
        lv_obj_set_pos(dot_bot_img, DOT_BOT_X, DOT_BOT_Y);
    }

    for (int i = 0; i < DOT_BAT_SEGS; i++) {
        lv_obj_t *s = lv_obj_create(parent);
        lv_obj_remove_style_all(s);
        lv_obj_set_size(s, 11, 16);
        lv_obj_set_pos(s, 140 + i * 14, 430);
        lv_obj_set_style_bg_color(s, dot_seg_empty(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
        dot_bat_seg[i] = s;
    }

    // Percentage centered in the gap to the right of the 12-segment bar (which
    // ends at x=305). A center-aligned box over x 306..356 keeps "9%" and "100%"
    // alike balanced in that gap instead of drifting against the bar, and its y
    // lines the text up with the bar row (bar center y=438).
    dot_bat_pct = lv_label_create(parent);
    lv_obj_set_width(dot_bat_pct, 50);
    lv_obj_set_style_text_align(dot_bat_pct, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(dot_bat_pct, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(dot_bat_pct, lv_color_hex(0x8A8A8A), LV_PART_MAIN);
    lv_label_set_text(dot_bat_pct, "");
    lv_obj_set_pos(dot_bat_pct, 306, 427);
}

// 1 Hz: redraw the icons / segments / percentage only when something changed.
static void update_dot_bottom()
{
    bool sw = stopwatch_is_running();
    bool tm = timer_is_running();
    bool al = alarm_is_enabled();
    int  pct = instance.pmu.getBatteryPercent();
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    static int last = -1;
    int key = (sw ? 1 : 0) | (tm ? 2 : 0) | (al ? 4 : 0) | (pct << 3);
    if (key == last) return;
    last = key;

    if (dot_bot_buf && dot_bot_img) {
        const uint32_t W = 0xFFFFFFFFu, G = 0xFF5C5C5Cu;
        memset(dot_bot_buf, 0, (size_t)DOT_BOT_W * (size_t)DOT_BOT_H * 4u);
        dot_draw_stopwatch(dot_bot_buf, DOT_BOT_W, DOT_BOT_H, sw ? W : G);
        dot_draw_timer    (dot_bot_buf, DOT_BOT_W, DOT_BOT_H, tm ? W : G);
        dot_draw_bell     (dot_bot_buf, DOT_BOT_W, DOT_BOT_H, al ? W : G);
        lv_image_set_src(dot_bot_img, NULL);
        lv_image_set_src(dot_bot_img, &dot_bot_dsc);
        lv_obj_invalidate(dot_bot_img);
    }

    int filled = (pct * DOT_BAT_SEGS + 50) / 100;   // 67% -> 8 of 12
    for (int i = 0; i < DOT_BAT_SEGS; i++)
        lv_obj_set_style_bg_color(dot_bat_seg[i], i < filled ? dot_white() : dot_seg_empty(), LV_PART_MAIN);

    if (dot_bat_pct) lv_label_set_text_fmt(dot_bat_pct, "%d", pct);   // number only, no '%'
}

// ---- Detection badges -----------------------------------------------------------
//
// Five always-visible badges, centred in the gap between the date and the
// battery bar (row at y=372, enlarged from the original y=365 dotface_final.svg
// placement), each bound to one detector through detector_toggle, the same control point the
// Tools tiles use, so the two surfaces always agree. Three states:
//   off      gray #5C5C5C outline + label, no count
//   armed    white outline + label, no count (running, nothing seen yet)
//   hit      red pill, white label, red count beside it (running and count > 0)
// A tap toggles the detector, in every mode (Daily included, by the owner's
// choice: unlike the Tools grid, the badges are not gated).
struct DotBadgeSpec {
    Detector    det;
    const char *text;
    int         pill_x, pill_w;   // pill rect (y=372, h=26)
    int         count_x;          // count text left edge
};
static const DotBadgeSpec kDotBadges[] = {
    { Detector::Flock,    "Flock",  40, 50,  92 },
    { Detector::EvilTwin, "EvilT", 110, 50, 162 },
    { Detector::AirTag,   "AirT",  180, 50, 232 },
    { Detector::Flipper,  "Flip",  250, 50, 302 },
    { Detector::Skimmer,  "Skim",  320, 50, 372 },
};
static constexpr int DOT_BADGE_N = sizeof(kDotBadges) / sizeof(kDotBadges[0]);

struct DotBadge { lv_obj_t *hit, *pill, *label, *count; int shown; };
static DotBadge dot_badges[DOT_BADGE_N];

static void update_dot_badges(bool force);

static void on_dot_badge_clicked(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= DOT_BADGE_N) return;
    Detector d = kDotBadges[i].det;
    bool was = detector_is_running(d);
    bool now = detector_toggle(d);
    if (!was && !now) {
        low_mem_show_dialog(
            "#ff5555 RADIO BUSY#\n\n"
            "This detector needs a radio\n"
            "another feature is using.\n\n"
            "Turn that off, then try again.");
    }
    update_dot_badges(true);
}

static void build_dot_badges(lv_obj_t *parent)
{
    for (int i = 0; i < DOT_BADGE_N; i++) {
        const DotBadgeSpec &s = kDotBadges[i];
        DotBadge &b = dot_badges[i];
        // Invisible hit area around pill + count: a finger-sized tap target.
        // Row dropped to y=372 (pill h=26) so it sits centred in the gap between
        // the date (~y324) and the battery bar (~y430), and enlarged for
        // readability (label font 10 -> 14).
        const int hx = s.pill_x - 4, hy = 366;
        b.hit = lv_obj_create(parent);
        lv_obj_remove_style_all(b.hit);
        lv_obj_set_pos(b.hit, hx, hy);
        lv_obj_set_size(b.hit, (s.count_x + 14) - hx, 40);
        lv_obj_clear_flag(b.hit, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(b.hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b.hit, on_dot_badge_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        b.pill = lv_obj_create(b.hit);
        lv_obj_remove_style_all(b.pill);
        lv_obj_set_pos(b.pill, s.pill_x - hx, 372 - hy);
        lv_obj_set_size(b.pill, s.pill_w, 26);
        lv_obj_set_style_radius(b.pill, 5, LV_PART_MAIN);
        lv_obj_set_style_border_width(b.pill, 1, LV_PART_MAIN);
        lv_obj_clear_flag(b.pill, LV_OBJ_FLAG_CLICKABLE);   // let the hit area take the tap

        b.label = lv_label_create(b.pill);
        lv_obj_set_style_text_font(b.label, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_label_set_text(b.label, s.text);
        lv_obj_center(b.label);

        b.count = lv_label_create(b.hit);
        lv_obj_set_style_text_font(b.count, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(b.count, dot_red(), LV_PART_MAIN);
        lv_label_set_text(b.count, "");
        lv_obj_set_pos(b.count, s.count_x - hx, 376 - hy);
        lv_obj_add_flag(b.count, LV_OBJ_FLAG_HIDDEN);

        b.shown = -1;
    }
}

// Restyle each badge only when its state or count changed (1 Hz, or forced
// right after a tap so the badge answers immediately).
static void update_dot_badges(bool force)
{
    for (int i = 0; i < DOT_BADGE_N; i++) {
        DotBadge &b = dot_badges[i];
        if (!b.hit) continue;
        Detector d   = kDotBadges[i].det;
        bool     on  = detector_is_running(d);
        int      cnt = on ? detector_count(d) : 0;
        // 0 off, 1 armed, 2+ hit (encodes the count so a new hit repaints).
        int state = !on ? 0 : (cnt > 0 ? 2 + (cnt > 999 ? 999 : cnt) : 1);
        if (!force && state == b.shown) continue;
        b.shown = state;

        if (state >= 2) {
            lv_obj_set_style_bg_color(b.pill, dot_red(), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(b.pill, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_color(b.pill, dot_red(), LV_PART_MAIN);
            lv_obj_set_style_text_color(b.label, dot_white(), LV_PART_MAIN);
            lv_label_set_text_fmt(b.count, "%d", cnt);
            lv_obj_clear_flag(b.count, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_color_t c = on ? dot_white() : dot_gray();
            lv_obj_set_style_bg_opa(b.pill, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_color(b.pill, c, LV_PART_MAIN);
            lv_obj_set_style_text_color(b.label, c, LV_PART_MAIN);
            lv_obj_add_flag(b.count, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// Per-second Dot-face refresh hook, called from the 1 Hz status block. No-op
// unless the Dot face is the active one (and parks the USB wave otherwise so it
// never animates off screen). Grows as Dot components land.
static void dot_face_tick()
{
    if (clock_face != FACE_DOT || !dot_container
        || lv_screen_active() != clock_screen) {
        dot_usb_stop();
        close_tile_picker();   // never leave the picker up after leaving the face
        return;
    }
    update_dot_status();
    update_dot_usb();
    update_dot_bottom();
    update_dot_badges(false);
    update_dot_accent();   // step-progress fill on the accent rail
}

// ---- Face-watch customization (Tools > Face) --------------------------------
// The LVGL font for the Dot date line per the current choice.
static const lv_font_t *dot_date_lvfont()
{
    switch (face_date_font()) {
        case FACE_DATE_MONO: return &font_argus_mono_16;
        case FACE_DATE_ORBITRON:
        default:             return &font_argus_label_20;
    }
}

// Re-apply the saved Dot-face look (accent colour + date font) and force the
// time/date to repaint with the current hour font + date order. Called at build
// and by the Face screen whenever a choice changes.
void clock_screen_apply_face_custom()
{
    if (dot_accent)
        lv_obj_set_style_bg_color(dot_accent, lv_color_hex(face_accent_rgb()), LV_PART_MAIN);
    if (dot_notif_btn)
        lv_obj_set_style_bg_color(dot_notif_btn, lv_color_hex(face_accent_rgb()), LV_PART_MAIN);
    if (dot_date_label)
        lv_obj_set_style_text_font(dot_date_label, dot_date_lvfont(), LV_PART_MAIN);

    // Force the next paint to redo the time + date with the new mode / order.
    s_dot_time_key     = -1;
    s_dot_date_last[0] = '\x01';
    s_dot_date_last[1] = '\0';

    // Repaint now if the Dot face is live, rather than waiting for the 1 Hz tick.
    if (clock_face == FACE_DOT && dot_container && lv_screen_active() == clock_screen) {
        time_t now = time(NULL);
        struct tm lt;
        localtime_r(&now, &lt);
        update_dot_face(&lt);
        lv_obj_invalidate(dot_container);
    }
}

// Getters for the Face screen (which also drives the shared 12h / wallpaper setters).
bool clock_screen_get_12h()       { return clock_12h; }
bool clock_screen_get_wallpaper() { return background_is_enabled(); }

// Status-bar "active" accent, threat-aware (ARGUS -> HADES). Normally the
// bright steel-blue ARGUS_ACCENT_ACTIVE; the instant Threat Radar flags a tail
// (top level >= TR_LVL_LIKELY) every live status icon flips to HADES red, so a
// glance at the clock face shows the watch has "opened its red eyes". Refreshed
// each second by the status-icon update loop, so it flips live and clears on its
// own once the tail goes stale. Shares argus_accent()'s exact threshold so all
// themed surfaces agree.
static inline lv_color_t status_accent_active()
{
    return threatradar_top_level() >= TR_LVL_LIKELY ? HADES_RED : ARGUS_ACCENT_ACTIVE;
}

static void update_lora_indicator()
{
    // Green whenever the shared SX1262 radio is in use — by LoRa/meshtastic,
    // the pager scanner, the TPMS scanner, APRS, or the LoRa analyzer.
    bool in_use = lora_screen_is_powered() || pager_is_running()
               || tpms_is_running() || aprs_is_running()
               || lora_analyze_is_running();
    lv_color_t color = in_use
        ? status_accent_active()
        : lv_color_make(0x33, 0x33, 0x33);
    lv_obj_set_style_arc_color(lora_arc,  color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(lora_ball,  color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(lora_stick, color, LV_PART_MAIN);
}

// Antenna icon: half-circle (∩) above a ball on a stick.
// All three parts are children of lora_container so realign_status_icons()
// can move the whole widget as one unit.
static void build_lora_indicator(lv_obj_t *screen)
{
    lora_container = lv_obj_create(screen);
    lv_obj_set_size(lora_container, 24, 36);
    lv_obj_set_style_bg_opa(lora_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lora_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(lora_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(lora_container, LV_ALIGN_TOP_RIGHT, -195, 54); // placeholder; realign() overrides

    // Stick: 2×12 px, centered at x=12, from y=24 down
    lora_stick = lv_obj_create(lora_container);
    lv_obj_set_pos(lora_stick, 11, 24);
    lv_obj_set_size(lora_stick, 2, 12);
    lv_obj_set_style_bg_color(lora_stick, lv_color_make(0x33,0x33,0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lora_stick, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_stick, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(lora_stick, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lora_stick, 0, LV_PART_MAIN);

    // Ball: 8×8 circle, center at (12, 20)
    lora_ball = lv_obj_create(lora_container);
    lv_obj_set_pos(lora_ball, 8, 16);
    lv_obj_set_size(lora_ball, 8, 8);
    lv_obj_set_style_bg_color(lora_ball, lv_color_make(0x33,0x33,0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lora_ball, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_ball, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(lora_ball, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lora_ball, 0, LV_PART_MAIN);

    // Arc: top half ∩, center at (12, 20), radius 11.
    // LVGL angles: 0°=east, 90°=south, 180°=west, 270°=north — clockwise.
    // 180°→360° traces west→north→east = top half ∩.
    lora_arc = lv_arc_create(lora_container);
    lv_obj_set_pos(lora_arc, 1, 9);
    lv_obj_set_size(lora_arc, 22, 22);
    lv_obj_set_style_bg_opa(lora_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(lora_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lora_arc, 0, LV_PART_MAIN);
    lv_arc_set_angles(lora_arc, 180, 360);
    lv_arc_set_bg_angles(lora_arc, 0, 360);
    lv_obj_set_style_arc_color(lora_arc, lv_color_make(0x33,0x33,0x33), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(lora_arc, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(lora_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(lora_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(lora_arc, LV_OBJ_FLAG_SCROLLABLE);
}

// Chains all status icons left of GPS, accommodating GPS label width changes
// (e.g. when satellite count is appended). Call after updating GPS label text
// and after lv_obj_update_layout(clock_screen).
static void realign_status_icons()
{
    lv_obj_update_layout(clock_screen);
    lv_obj_align_to(wardriver_container, gps_indicator,        LV_ALIGN_OUT_LEFT_MID, -5, 0);
    lv_obj_align_to(wifi_indicator,      wardriver_container, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    lv_obj_align_to(bt_indicator,        wifi_indicator,      LV_ALIGN_OUT_LEFT_MID, -5, 0);
    lv_obj_align_to(sd_indicator,        bt_indicator,        LV_ALIGN_OUT_LEFT_MID, -5, 0);
    lv_obj_align_to(nfc_indicator,       sd_indicator,        LV_ALIGN_OUT_LEFT_MID, -5, 0);
    lv_obj_align_to(lora_container,      nfc_indicator,       LV_ALIGN_OUT_LEFT_MID, -5, 0);
    if (mesh_top_count_label) {
        lv_obj_align_to(mesh_top_count_label, lora_container, LV_ALIGN_OUT_LEFT_MID, -4, 0);
    }
}

static void update_bt_indicator()
{
    bool on = btStarted();
    lv_color_t color = on
        ? status_accent_active()  // steel-blue — BT active (HADES red on a tail)
        : lv_color_make(0x33, 0x33, 0x33); // gray  — BT off
    lv_obj_set_style_text_color(bt_indicator, color, LV_PART_MAIN);
}

static void update_wifi_indicator()
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    bool on = (mode != WIFI_MODE_NULL);
    lv_color_t color = on
        ? status_accent_active()  // steel-blue — radio active (HADES red on a tail)
        : lv_color_make(0x33, 0x33, 0x33); // gray  — radio off
    lv_obj_set_style_text_color(wifi_indicator, color, LV_PART_MAIN);
}

// Tracks whether the SD was successfully mounted the last time we checked.
// Initialized in setup() to match the state after instance.begin().
static bool sd_was_ready = false;

static void update_sd_indicator()
{
    // Combined "is the SD writable by us?" — both insertion and USB-SD
    // claim state matter. Tracked here (rather than just sd_was_ready)
    // so the settings nudge below also fires when the host computer
    // mounts/unmounts the card over USB-MSC.
    bool prev_usable = sd_was_ready && !usb_sd_is_running();
    bool prev_ready  = sd_was_ready;
    if (sd_was_ready) {
        // Use the physical card-detect pin on XL9555 IO10 (active-LOW) for removal.
        // This is a live I2C read, unlike SD.sectorSize() which is cached at mount time.
        if (instance.io.digitalRead(EXPANDS_SD_DET) == HIGH) {
            instance.uninstallSD();
            sd_was_ready = false;
        }
    } else {
        // Poll for insertion: installSD() reads the physical card-detect pin on
        // XL9555 IO10 before attempting SD.begin(), so it returns false fast
        // when no card is present without stressing the SPI bus.
        if (instance.installSD()) {
            sd_was_ready = true;
        }
    }
    lv_color_t color = sd_was_ready
        ? status_accent_active()  // steel-blue — card mounted (HADES red on a tail)
        : lv_color_make(0x33, 0x33, 0x33); // gray  — no card
    lv_obj_set_style_text_color(sd_indicator, color, LV_PART_MAIN);

    // On any SD-usability state edge (card insert/eject *or* USB-SD
    // claim/release), nudge the settings screen so it can grey out /
    // re-enable the "Screenshot long press" toggle accordingly.
    bool now_usable = sd_was_ready && !usb_sd_is_running();
    if (now_usable != prev_usable || sd_was_ready != prev_ready)
        settings_screen_apply_sd_state();
}

static void update_nfc_indicator()
{
    bool on = instance.pmu.isEnableDLDO1();
    lv_color_t color = on
        ? status_accent_active()
        : lv_color_make(0x33, 0x33, 0x33);
    lv_obj_set_style_image_recolor(nfc_indicator, color, LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(nfc_indicator, LV_OPA_COVER, LV_PART_MAIN);
}

static void update_battery()
{
    int pct = instance.pmu.getBatteryPercent();
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    lv_color_t color = bat_color(pct);

    lv_obj_set_width(bat_fill, (int32_t)pct * BAT_INNER_W / 100);
    lv_obj_set_style_bg_color(bat_fill, color, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bat_nub, color, LV_PART_MAIN);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(bat_label, buf);

    update_charge_bolt();
}

// Render the charging bolt from the debounced PMU state. Called from
// update_battery() on the 1 Hz tick, which is also what paces the pulse.
//
//   Discharging  bolt hidden
//   Charging     bolt pulses between full and 40% opacity, one step per tick
//   Topped       bolt steady at full opacity (on the charger, cell full)
//
// Two USB states rather than one because isCharging() alone goes false the
// moment the AXP2101 terminates, so a watch sitting full on the charger would
// look identical to one running on the cell. A slow 1 Hz breathe reads as
// "energy flowing in"; it is deliberately not a hard on/off blink, which on
// this watch face means fault or alert.
static void update_charge_bolt()
{
    if (!bat_bolt) return;

    // bat_charge is advanced once per 1 Hz tick by charge_state_tick() (which
    // runs on every face, including Dot); here we only read the settled state.
    ChargeState st = bat_charge.state();

    // Only touch the hidden flag on an actual edge. lv_obj_add_flag()
    // invalidates whenever LV_OBJ_FLAG_HIDDEN is in the set, so re-asserting
    // it every tick would queue a redraw of this rect once a second for the
    // entire time the watch is off the charger.
    static ChargeState prev = ChargeState::Discharging;
    bool was_visible = prev != ChargeState::Discharging;
    bool is_visible  = st  != ChargeState::Discharging;
    prev = st;

    if (!is_visible) {
        if (was_visible) lv_obj_add_flag(bat_bolt, LV_OBJ_FLAG_HIDDEN);
        bolt_pulse_dim = false;
        return;
    }

    if (!was_visible) lv_obj_clear_flag(bat_bolt, LV_OBJ_FLAG_HIDDEN);

    if (st == ChargeState::Charging)
        bolt_pulse_dim = !bolt_pulse_dim;
    else
        bolt_pulse_dim = false;   // Topped: hold steady

    // Same edge-only discipline: setting a local style prop refreshes and
    // invalidates whether or not the value actually changed, and Topped can
    // sit unchanged for hours on the dock.
    lv_opa_t want = bolt_pulse_dim ? LV_OPA_40 : LV_OPA_COVER;
    static lv_opa_t applied = LV_OPA_TRANSP;   // never a legal rendered value
    if (want != applied || !was_visible) {
        lv_obj_set_style_line_opa(bat_bolt, want, LV_PART_MAIN);
        applied = want;
    }
}

// Pack the alarm / stopwatch / timer indicators flush against the left
// edge of the battery widget, right-to-left in priority order. The
// rightmost slot is filled first, then the next slot inward, etc., so
// whichever subset is currently enabled always sits in a tidy row up
// against the battery instead of leaving gaps at fixed positions.
//
// Order from rightmost (closest to battery) outward:
//   alarm  → stopwatch  → timer
// Run from the 1 s clock tick and any time enabled state changes.
static void layout_battery_indicators()
{
    if (!alarm_indicator || !stopwatch_indicator || !timer_indicator) return;

    struct Entry { lv_obj_t *obj; bool on; };
    Entry order[] = {
        { alarm_indicator,     alarm_is_enabled()    },
        { stopwatch_indicator, stopwatch_is_running() },
        { timer_indicator,     timer_is_running()    },
    };

    // Battery container is BAT_W + BAT_NUB_W = 130 px wide, centred on
    // BOTTOM_MID, so its left edge sits at x_offset = -65. Each indicator
    // gets a 26 px-wide slot (20 px icon + 6 px gap), with the rightmost
    // slot's centre 8 px outside the battery's left edge.
    constexpr int BAT_LEFT_OFFSET = -(BAT_W + BAT_NUB_W) / 2;  // -35
    constexpr int GAP             = 8;
    constexpr int SLOT_W          = 26;
    // Match the battery's BOTTOM_MID offset (-10) so the indicator
    // row's bottom edge sits flush with the battery's bottom edge,
    // sharing a baseline at the very bottom of the visible disc.
    constexpr int Y_OFFSET        = -10;

    int filled = 0;
    for (auto &e : order) {
        if (e.on) {
            int cx = BAT_LEFT_OFFSET - GAP - SLOT_W / 2 - filled * SLOT_W;
            lv_obj_align(e.obj, LV_ALIGN_BOTTOM_MID, cx, Y_OFFSET);
            lv_obj_clear_flag(e.obj, LV_OBJ_FLAG_HIDDEN);
            filled++;
        } else {
            lv_obj_add_flag(e.obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// Build a tiny 20×24 stopwatch/timer-style icon from primitives. The two
// variants share a green circular ring and an angled "hand"; the
// distinguishing feature is the cap on top — `wide_cap` true draws a wide
// flat knob (timer/kitchen-clock look) instead of the narrow stem of a
// stopwatch button. `hand_rotation_deci_deg` lets each variant point its
// hand a different direction so they read as two clearly distinct icons.
static lv_obj_t *build_clock_icon(lv_obj_t *parent, bool wide_cap,
                                  int16_t hand_rotation_deci_deg)
{
    // const (not constexpr): ARGUS_ACCENT uses the runtime lv_color_make(); the
    // stock code's LV_COLOR_MAKE brace-init was constexpr, but our themed accent
    // is centralized as a function-form macro (needed for the ternary/arg sites).
    static const lv_color_t green = ARGUS_ACCENT;

    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_set_size(icon, 20, 24);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    // Cap on top — stopwatch button (narrow) vs timer knob (wide flat).
    lv_obj_t *cap = lv_obj_create(icon);
    if (wide_cap) lv_obj_set_pos(cap, 3, 0), lv_obj_set_size(cap, 14, 4);
    else          lv_obj_set_pos(cap, 7, 0), lv_obj_set_size(cap,  6, 4);
    lv_obj_set_style_bg_color(cap, green, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(cap, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cap, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cap, 0, LV_PART_MAIN);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_SCROLLABLE);

    // The face — a green ring (transparent fill, 3 px green border).
    lv_obj_t *ring = lv_obj_create(icon);
    lv_obj_set_pos(ring, 0, 4);
    lv_obj_set_size(ring, 20, 20);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(ring, green, LV_PART_MAIN);
    lv_obj_set_style_border_width(ring, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ring, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);

    // Hand — a thin vertical rectangle pivoted at its bottom, rotated to
    // point in the direction the caller chose. Mounted inside the ring so
    // it gets clipped by the round face naturally if it ever goes long.
    lv_obj_t *hand = lv_obj_create(ring);
    lv_obj_set_pos(hand, 7, -2);   // bottom of hand sits at ring centre
    lv_obj_set_size(hand, 2, 9);
    lv_obj_set_style_bg_color(hand, green, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hand, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(hand, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(hand, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hand, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(hand, 1, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(hand, 9, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(hand, hand_rotation_deci_deg, LV_PART_MAIN);
    lv_obj_clear_flag(hand, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(hand, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
    return icon;
}

static volatile bool back_btn_pressed = false;
static void IRAM_ATTR on_back_btn_isr() { back_btn_pressed = true; }

static void update_wardriver_indicator()
{
    bool running = wardriver_is_running();
    int  wc = running ? wardriver_get_wifi_count() : 0;
    int  bc = running ? wardriver_get_bt_count()   : 0;

    if (running) {
        lv_obj_set_style_text_color(wardriver_wifi_label,
            ARGUS_ACCENT_ACTIVE, LV_PART_MAIN);
        if (wc > 0)
            lv_label_set_text_fmt(wardriver_wifi_label, LV_SYMBOL_EYE_OPEN " %d", wc);
        else
            lv_label_set_text(wardriver_wifi_label, LV_SYMBOL_EYE_OPEN);

        if (bc > 0) {
            lv_label_set_text_fmt(wardriver_bt_label, " %d", bc);
            lv_obj_clear_flag(wardriver_bt_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(wardriver_bt_label, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_set_style_text_color(wardriver_wifi_label,
            ARGUS_TEXT_DIM, LV_PART_MAIN);
        lv_label_set_text(wardriver_wifi_label, LV_SYMBOL_EYE_OPEN);
        lv_obj_add_flag(wardriver_bt_label, LV_OBJ_FLAG_HIDDEN);
    }

    realign_status_icons();
}

// ---- Pull-down from the top edge (any screen) -------------------------------
//
// An Android-style pull that starts on the top edge of ANY screen opens the
// notification shade (the Notify screen, brightness slider on top). The touch
// indev's PRESSED / RELEASED events (LVGL 9 forwards both to the indev) give the
// exact start and end points, which a plain LV_EVENT_GESTURE does not expose.
static int32_t s_press_x = -1, s_press_y = -1;
static constexpr int32_t TOP_EDGE_PX  = 60;   // a pull must start this close to the top
static constexpr int32_t PULL_MIN_PX  = 70;   // and travel at least this far down

bool touch_started_at_top_edge()
{
    return s_press_y >= 0 && s_press_y < TOP_EDGE_PX;
}

static void on_touch_pressed(lv_event_t *e)
{
    lv_indev_t *indev = (lv_indev_t *)lv_event_get_user_data(e);
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s_press_x = p.x;
    s_press_y = p.y;
}

static void on_touch_released(lv_event_t *e)
{
    lv_indev_t *indev = (lv_indev_t *)lv_event_get_user_data(e);
    bool from_top = touch_started_at_top_edge();
    int32_t x0 = s_press_x, y0 = s_press_y;
    s_press_x = s_press_y = -1;
    if (!from_top) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int32_t dx = p.x - x0, dy = p.y - y0;
    if (dy < PULL_MIN_PX || dy <= (dx < 0 ? -dx : dx)) return;   // not a downward pull

    // Never over the PIN pad (it guards Offense; the shade must not leak past it),
    // and not again if the shade is already up (the clock's own swipe-down got
    // there first on this same touch).
    if (pin_pad_screen_is_active() || notifications_screen_is_active()) return;
    notifications_screen_show();
}

// Hook every pointer indev. Call once after the LVGL input devices exist.
static void watch_touch_pulls()
{
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i)) {
        if (lv_indev_get_type(i) != LV_INDEV_TYPE_POINTER) continue;
        lv_indev_add_event_cb(i, on_touch_pressed,  LV_EVENT_PRESSED,  i);
        lv_indev_add_event_cb(i, on_touch_released, LV_EVENT_RELEASED, i);
    }
}

// Go back to the screen a transient surface (the shade) was opened over. The
// clock needs its full synchronous repaint; any other screen is simply reloaded.
void screen_return_to(lv_obj_t *scr)
{
    if (!scr || scr == clock_screen || !lv_obj_is_valid(scr)) {
        clock_screen_show();
        return;
    }
    lv_scr_load(scr);
    lv_obj_invalidate(scr);
    main_loop_request_lvgl_priority(12);
}

static void on_clock_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    // Daily keeps the innocent surfaces reachable. Meshtastic (mesh comms) and Time
    // are day-to-day features allowed in every mode; Wardriver (recon) and the Tools
    // grid are gated to Defense/Offense so a Daily glance/confiscation reveals nothing.
    //
    // Home navigation:
    //   swipe right-to-left  -> Recon grid (gated in Daily)
    //   swipe left-to-right  -> Wardriver, then Meshtastic, then Nodes (each a
    //                           further swipe the same way; Daily skips the
    //                           gated Wardriver and lands on Meshtastic)
    //   swipe down           -> notification shade (every mode)
    //   swipe up             -> Tools (clock utilities + Health, every mode)
    bool daily = (argus_mode_current() == ArgusMode::Daily);
    if (dir == LV_DIR_LEFT) {
        apps_screen_show();                    // unified Apps launcher, every mode
    } else if (dir == LV_DIR_RIGHT) {
        if (!daily) {
            wardriver_screen_show();           // recon, gated in Daily
        } else {
            meshtastic_mark_read();            // comms, available in every mode
            meshtastic_screen_show();
        }
    } else if (dir == LV_DIR_BOTTOM) {   // swipe down from clock face
        notifications_screen_show();     // shade: notifications + brightness, every mode
    } else if (dir == LV_DIR_TOP) {      // swipe up from clock face
        apps_screen_show();              // unified Apps launcher, every mode
    }
}

// DotOS is the only watch face now. The `mode` argument is ignored (kept so the
// settings screen and legacy callers need no signature change) — the Dot layer
// is always selected. The digital span-group time_label is the safety fallback,
// shown only if the Dot layer failed to build (its PSRAM raster buffer could not
// be allocated). Whichever comes up is driven to the current time at once so
// there is no one-tick stale frame right after loading the clock.
void clock_screen_set_face(int /*mode*/)
{
    lv_obj_add_flag(time_label, LV_OBJ_FLAG_HIDDEN);
    if (dot_container) lv_obj_add_flag(dot_container, LV_OBJ_FLAG_HIDDEN);

    struct tm t;
    instance.rtc.getDateTime(&t);
    clocktime::tm_utc_to_local(&t, clock_utc_offset);

    if (dot_container) {
        clock_face = FACE_DOT;
        lv_obj_clear_flag(dot_container, LV_OBJ_FLAG_HIDDEN);
        update_dot_face(&t);
        update_dot_status();
    } else {
        clock_face = FACE_DIGITAL;   // fallback if the Dot layer failed to build
        lv_obj_clear_flag(time_label, LV_OBJ_FLAG_HIDDEN);
        update_clock();
    }
}

// Called by the LoRa screen when LoRa power is toggled, for an immediate icon
// refresh. The actual state is read live in update_lora_indicator() (which also
// runs once a second), so the argument is no longer needed.
void clock_screen_set_lora_active(bool active)
{
    (void)active;
    update_lora_indicator();
}

// Positions AirTag, Flipper, Skimmer, EvilTwin, and Flock indicators relative
// to each other and the mesh envelope. From right to left: mesh, AirTag,
// Flipper, Skimmer, EvilTwin, Flock. Each indicator only takes a slot when
// it's visible, so when one is hidden the others slide right to fill in.
// Spacing: 65 px per slot; mesh occupies the rightmost position (x ~ 0).
static void update_scan_indicators()
{
    if (!airtag_indicator || !flock_indicator ||
        !flipper_indicator || !skimmer_indicator ||
        !evil_twin_indicator) return;

    bool airtag_vis   = airtag_is_running()    || airtag_get_count()    > 0;
    // Flipper / Skimmer / EvilTwin indicators show only when at least one
    // detection has happened — they stay hidden while the wardriver /
    // detector is idle or running with zero hits.
    bool flipper_vis  = (flipper_get_count()   > 0);
    bool skimmer_vis  = (skimmer_get_count()   > 0);
    bool eviltwin_vis = (evil_twin_get_count() > 0);
    bool flock_vis    = (flock_get_count()     > 0);

    if (airtag_vis) {
        lv_obj_clear_flag(airtag_indicator, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(airtag_count_label, "%d", airtag_get_count());
    } else {
        lv_obj_add_flag(airtag_indicator, LV_OBJ_FLAG_HIDDEN);
    }

    if (flipper_vis) {
        lv_obj_clear_flag(flipper_indicator, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(flipper_count_label, "%d", flipper_get_count());
    } else {
        lv_obj_add_flag(flipper_indicator, LV_OBJ_FLAG_HIDDEN);
    }

    if (skimmer_vis) {
        lv_obj_clear_flag(skimmer_indicator, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(skimmer_count_label, "%d", skimmer_get_count());
    } else {
        lv_obj_add_flag(skimmer_indicator, LV_OBJ_FLAG_HIDDEN);
    }

    if (eviltwin_vis) {
        lv_obj_clear_flag(evil_twin_indicator, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(evil_twin_count_label, "%d", evil_twin_get_count());
    } else {
        lv_obj_add_flag(evil_twin_indicator, LV_OBJ_FLAG_HIDDEN);
    }

    if (flock_vis) {
        lv_obj_clear_flag(flock_indicator, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(flock_count_label, "%d", flock_get_count());
    } else {
        lv_obj_add_flag(flock_indicator, LV_OBJ_FLAG_HIDDEN);
    }

    if (!airtag_vis && !flipper_vis && !skimmer_vis &&
        !eviltwin_vis && !flock_vis) return;

    // Row order, left -> right:
    //   flock | evil_twin | airtag | flipper | skimmer | (mesh)
    // Skimmer + Flipper sit to the right of AirTag; EvilTwin + Flock
    // stay on AirTag's left. Pack right-most slot first and step left
    // by 65 px per visible indicator so hidden ones collapse without
    // leaving a gap.
    int slot = 0;
    if (skimmer_vis) {
        lv_obj_align(skimmer_indicator, LV_ALIGN_BOTTOM_MID, slot, -60);
        slot -= 65;
    }
    if (flipper_vis) {
        lv_obj_align(flipper_indicator, LV_ALIGN_BOTTOM_MID, slot, -60);
        slot -= 65;
    }
    if (airtag_vis) {
        lv_obj_align(airtag_indicator, LV_ALIGN_BOTTOM_MID, slot, -60);
        slot -= 65;
    }
    if (eviltwin_vis) {
        lv_obj_align(evil_twin_indicator, LV_ALIGN_BOTTOM_MID, slot, -60);
        slot -= 65;
    }
    if (flock_vis) {
        lv_obj_align(flock_indicator, LV_ALIGN_BOTTOM_MID, slot, -60);
    }
}

// Called by meshtastic.cpp when a new message arrives or all are read
void clock_screen_set_mesh_count(int count)
{
    if (!mesh_top_count_label) return;
    if (count <= 0) {
        lv_obj_add_flag(mesh_top_count_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(mesh_top_count_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(mesh_top_count_label, "%d", count);
        realign_status_icons();
    }
}

// Called by gps_screen when GPS power state changes
void clock_screen_set_gps_active(bool active)
{
    lv_color_t color = active
        ? ARGUS_ACCENT_ACTIVE
        : lv_color_make(0x33, 0x33, 0x33);
    lv_obj_set_style_text_color(gps_indicator, color, LV_PART_MAIN);
    if (!active) {
        lv_label_set_text(gps_indicator, LV_SYMBOL_GPS);
        realign_status_icons();
    }
}

static void update_clock(); // forward declaration

void clock_screen_set_12h(bool use_12h)
{
    clock_12h = use_12h;
    update_clock();   // refresh the displayed string + rescale the label
}

// True when the watch face shows time in 12-hour form. The Dot face honours the
// same clock_12h setting (its Facewatch 12h toggle reuses this setter). The
// alarm screen consults this to decide whether to show its AM/PM selector.
bool clock_screen_uses_12h()
{
    return clock_12h;
}

void clock_screen_set_show_day(bool show)
{
    clock_show_day = show;
    update_clock();
}

void clock_screen_set_show_date(bool show)
{
    clock_show_date = show;
    update_clock();
}

void clock_screen_set_show_ampm(bool show)
{
    clock_show_ampm = show;
    update_clock();
}

void clock_screen_set_show_secs(bool show)
{
    clock_show_secs = show;
    update_clock();
}

void clock_screen_set_vibrate(bool enabled)
{
    clock_vibrate = enabled;
}

// Called by settings screen to enable/disable the Matrix rain background
void clock_screen_set_matrix(bool enabled)
{
    matrix_bg_set_enabled(enabled);
}

// Called by settings screen to enable/disable the SD-card wallpaper. Coexists
// with the matrix rain: when both are on the faint image sits behind the rain.
void clock_screen_set_wallpaper(bool enabled)
{
    background_set_enabled(enabled);
}

// Dim timer state — updated by settings screen callbacks
static uint32_t s_dim_timeout_ms   = 0;   // 0 = disabled
static uint8_t  s_dim_brightness   = DEVICE_MAX_BRIGHTNESS_LEVEL / 4;
static uint32_t s_last_activity_ms = 0;
static bool     s_is_dimmed        = false;
static uint32_t s_dimmed_at_ms     = 0;   // when the dim timer last fired

// ---- Automatic brightness from the sun ---------------------------------------
//
// Optional (Settings > Auto brightness). The active brightness follows the sun
// at the watch's position: the user's Settings level in daylight, a straight
// ramp through civil twilight (sun 0 to 6 degrees below the horizon), and
// AUTO_NIGHT_FACTOR of it once it is dark outside. The season comes for free
// with the date, so in Quebec in late September it dims from ~18:50 and is at
// the night level by ~19:20.
//
// The position is the last GPS fix, saved to NVS (namespace "argusloc") so it
// survives reboots and GPS-off days; the watch needs ONE fix ever for this to
// work. Until then the factor stays 1 (plain Settings brightness).
static constexpr float    AUTO_NIGHT_FACTOR = 0.35f;
static constexpr uint32_t AUTO_TICK_MS      = 60000;   // sun moves slowly
static bool   s_auto_bright   = false;
static bool   s_loc_valid     = false;
static float  s_loc_lat       = 0.0f, s_loc_lon = 0.0f;
static float  s_sun_factor    = 1.0f;
static uint32_t s_sun_last_ms = 0;

static void sun_location_load()
{
    Preferences p;
    if (!p.begin("argusloc", true)) return;
    s_loc_valid = p.getBool("valid", false);
    s_loc_lat   = p.getFloat("lat", 0.0f);
    s_loc_lon   = p.getFloat("lon", 0.0f);
    p.end();
}

// Remember a GPS fix for the sun calculation. Writes NVS only for a first fix
// or a move of ~10 km or more, so a GPS left on never wears the flash.
static void sun_location_note_fix()
{
    if (!gps_screen_is_powered() || !instance.gps.location.isValid()) return;
    float lat = (float)instance.gps.location.lat();
    float lon = (float)instance.gps.location.lng();
    if (s_loc_valid && fabsf(lat - s_loc_lat) < 0.1f && fabsf(lon - s_loc_lon) < 0.1f) return;
    s_loc_valid = true; s_loc_lat = lat; s_loc_lon = lon;
    Preferences p;
    if (!p.begin("argusloc", false)) return;
    p.putBool("valid", true);
    p.putFloat("lat", lat);
    p.putFloat("lon", lon);
    p.end();
}

static void sun_factor_update()
{
    if (!s_auto_bright || !s_loc_valid) { s_sun_factor = 1.0f; return; }
    struct tm t;
    instance.rtc.getDateTime(&t);   // RTC holds UTC
    t.tm_isdst = 0;                 // UTC: no DST (mktime reads this field)
    time_t epoch = mktime(&t);      // normalise to fill tm_yday
    struct tm u;
    gmtime_r(&epoch, &u);
    double elev = sun_elevation_deg(s_loc_lat, s_loc_lon, u.tm_year + 1900, u.tm_yday,
                                    u.tm_hour, u.tm_min, u.tm_sec);
    s_sun_factor = sun_brightness_factor(elev, AUTO_NIGHT_FACTOR);
}

// The brightness the panel should show while awake and undimmed: the Settings
// level, scaled by the sun when Auto brightness is on.
static uint8_t active_brightness()
{
    int level = (int)(settings_get_brightness() * s_sun_factor + 0.5f);
    if (level < 1) level = 1;
    if (level > DEVICE_MAX_BRIGHTNESS_LEVEL) level = DEVICE_MAX_BRIGHTNESS_LEVEL;
    return (uint8_t)level;
}

int clock_screen_active_brightness() { return active_brightness(); }

bool clock_screen_has_sun_location() { return s_loc_valid; }

// ---- Battery saver: screen fully off after the dim --------------------------
//
// Optional (Settings > Battery saver). SAVER_OFF_AFTER_MS after the dim timer
// fires, the panel is put to sleep (power cut, ~10 mA saved per the LilyGo
// docs) and LVGL stops rendering. A touch, a button press, wrist motion (when
// Motion brightens screen is on) or an incoming notification wakes it. The
// waking touch lands on a full-screen blocker on the top layer so it can never
// click whatever sits under the finger on a screen the user cannot see.
static constexpr uint32_t SAVER_OFF_AFTER_MS = 10000;
static bool      s_batt_saver   = false;
static bool      s_display_off  = false;
static lv_obj_t *s_wake_blocker = nullptr;

// ---- CPU frequency scaling --------------------------------------------------
//
// The ESP32-S3 idles far cheaper at a lower core clock. While the panel is off
// nothing renders and the loop only polls a few cheap inputs, so the full
// 240 MHz is pure waste. Drop to 80 MHz then — the lowest clock that still
// keeps the WiFi/BLE radios and their PLL usable, so a background detector scan
// keeps running — and restore 240 MHz the instant the screen comes back so the
// UI never feels sluggish. Going below 80 MHz would stall the radios.
static constexpr uint32_t CPU_MHZ_AWAKE = 240;
static constexpr uint32_t CPU_MHZ_IDLE  = 80;
static bool s_cpu_low = false;

// Auto battery saver: below 20% on the cell (and not charging) the screen-off
// saver is forced on regardless of the Settings toggle, to stretch the last of
// the charge. Released with hysteresis (back above 25%, or once charging) so it
// cannot flap around the threshold. Tracked separately from s_batt_saver so it
// never overwrites the user's own choice — when the cell recovers, the manual
// toggle is whatever they left it.
static constexpr int LOW_BATT_ENTER_PCT = 20;
static constexpr int LOW_BATT_EXIT_PCT  = 25;
static bool s_low_batt_saver = false;

static void cpu_set_low(bool low)
{
    if (low == s_cpu_low) return;
    s_cpu_low = low;
    setCpuFrequencyMhz(low ? CPU_MHZ_IDLE : CPU_MHZ_AWAKE);
}

static bool touch_is_down()
{
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i))
        if (lv_indev_get_type(i) == LV_INDEV_TYPE_POINTER &&
            lv_indev_get_state(i) == LV_INDEV_STATE_PRESSED) return true;
    return false;
}

static void drop_wake_blocker()
{
    if (s_wake_blocker) { lv_obj_delete_async(s_wake_blocker); s_wake_blocker = nullptr; }
}

static void on_wake_blocker_up(lv_event_t *) { drop_wake_blocker(); }

static void hide_dim_gate();   // defined below; the off-state uses the wake blocker instead

static void display_off()
{
    if (s_display_off) return;
    s_display_off = true;
    hide_dim_gate();   // the full-off state wakes on any tap via the wake blocker
    s_wake_blocker = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_wake_blocker);
    lv_obj_set_size(s_wake_blocker, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_wake_blocker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_wake_blocker, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_wake_blocker, on_wake_blocker_up, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_wake_blocker, on_wake_blocker_up, LV_EVENT_PRESS_LOST, NULL);
    lv_timer_pause(lv_display_get_refr_timer(lv_display_get_default()));
    instance.sleepDisplay();
    // Nothing renders now: stretch a background BLE scan to ~15% duty and drop
    // the core clock to 80 MHz. Both are restored the moment the screen wakes.
    ble_scan_set_low_duty(true);
    cpu_set_low(true);
}

static void display_on()
{
    if (!s_display_off) return;
    s_display_off = false;
    // Restore full clock and scan responsiveness before repainting so the wake
    // frame renders at 240 MHz.
    cpu_set_low(false);
    ble_scan_set_low_duty(false);
    instance.wakeupDisplay();
    lv_timer_resume(lv_display_get_refr_timer(lv_display_get_default()));
    // A touch that is still down keeps the blocker until it lifts; any other
    // wake source (button, motion, notification) drops it right away.
    if (!touch_is_down()) drop_wake_blocker();
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
}

// ---- Dim wake-gate: swipe up to wake -----------------------------------------
//
// While the screen is dimmed (but still on), a full-screen top-layer gate
// captures touches so a stray pocket/sleeve tap can neither wake the watch nor
// click the UI behind it. A first touch reveals a subtle "swipe up to wake"
// hint; only a swipe up actually wakes. Hardware buttons wake directly and
// bypass this gate, so there is always a reliable way back even if a swipe is
// missed.
static void dim_reset_activity();   // forward: the gate wakes through it
static lv_obj_t *s_dim_gate  = nullptr;
static lv_obj_t *s_dim_card  = nullptr;   // frosted "glass" hint card (revealed on touch)
static lv_obj_t *s_dim_arrow = nullptr;   // up chevron inside the card
static bool      s_dim_gate_armed = false;   // false for the first 15 s: a tap just wakes

// Swipe-up-to-wake arms this long after the screen dims, not at the same instant:
// the first glance can still wake with a tap; once the watch is clearly set down a
// deliberate swipe up is required (saves battery, avoids pocket wakes).
static constexpr uint32_t DIM_GATE_ARM_MS = 15000;

// When the hint is revealed we briefly lift the panel brightness so the white
// "swipe up to wake" pops OUT of the dim and is easy to read, while a black scrim
// keeps everything behind it artificially dimmed. Because the backdrop is a
// near-black scrim (on this AMOLED, black pixels emit nothing), the higher
// brightness lights only the small hint card - so brushing the screen in a dark
// room does not light it up. The bright state auto-fades back after a few seconds
// if no swipe follows, so a stray touch cannot leave the panel lit.
static constexpr uint8_t  DIM_GATE_REVEAL_BRIGHTNESS = 100;   // ~40%, readable
static constexpr uint32_t DIM_GATE_REVEAL_MS         = 4000;
static lv_timer_t        *s_dim_reveal_timer = nullptr;

static void hide_dim_gate()
{
    if (s_dim_reveal_timer) { lv_timer_delete(s_dim_reveal_timer); s_dim_reveal_timer = nullptr; }
    if (s_dim_gate) { lv_obj_delete_async(s_dim_gate); s_dim_gate = nullptr; }
    s_dim_card  = nullptr;
    s_dim_arrow = nullptr;
    s_dim_gate_armed = false;
}

// Fade the revealed hint back to the plain dimmed state: drop the scrim, hide the
// card, and restore the dim brightness so the panel goes dark again. The gate
// itself stays (still armed), so the next touch can reveal it afresh.
static void dim_gate_conceal(lv_timer_t *t)
{
    if (s_dim_reveal_timer) { lv_timer_delete(s_dim_reveal_timer); s_dim_reveal_timer = nullptr; }
    if (!s_dim_gate) return;
    lv_obj_set_style_bg_opa(s_dim_gate, LV_OPA_TRANSP, LV_PART_MAIN);   // remove scrim
    if (s_dim_card) lv_obj_add_flag(s_dim_card, LV_OBJ_FLAG_HIDDEN);
    if (!s_display_off && s_is_dimmed) instance.setBrightness(s_dim_brightness);
}

static void on_dim_gate_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev && lv_indev_get_gesture_dir(indev) == LV_DIR_TOP) dim_reset_activity();
}

// lv_anim exec callbacks: gentle vertical drift of the chevron, and a fade-in of
// the whole card. Kept as free functions so no capturing lambda is needed.
static void dim_arrow_drift(void *o, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)o, v, LV_PART_MAIN);
}
static void dim_card_fade(void *o, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, LV_PART_MAIN);
}

static void on_dim_gate_pressed(lv_event_t *)
{
    // First 15 s after dimming: a tap simply wakes (swipe-up gate not armed yet).
    if (!s_dim_gate_armed) { dim_reset_activity(); return; }

    // Armed: reveal the frosted hint once and let it breathe; only a swipe up
    // (on_dim_gate_gesture) wakes from here, and hardware buttons always do.
    if (!s_dim_card || !lv_obj_has_flag(s_dim_card, LV_OBJ_FLAG_HIDDEN)) return;

    // Darken everything behind (scrim) and lift the brightness so the white hint
    // pops out of the dim without lighting the room (see the constants above).
    lv_obj_set_style_bg_color(s_dim_gate, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dim_gate, 165, LV_PART_MAIN);          // ~65% scrim
    if (!s_display_off) instance.setBrightness(DIM_GATE_REVEAL_BRIGHTNESS);

    lv_obj_set_style_opa(s_dim_card, LV_OPA_TRANSP, LV_PART_MAIN);   // start clear, fade in
    lv_obj_clear_flag(s_dim_card, LV_OBJ_FLAG_HIDDEN);

    // Auto-fade back to the dark dimmed state if no swipe follows.
    if (s_dim_reveal_timer) lv_timer_delete(s_dim_reveal_timer);
    s_dim_reveal_timer = lv_timer_create(dim_gate_conceal, DIM_GATE_REVEAL_MS, NULL);
    lv_timer_set_repeat_count(s_dim_reveal_timer, 1);

    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, s_dim_card);
    lv_anim_set_values(&fade, 0, 255);
    lv_anim_set_time(&fade, 260);
    lv_anim_set_exec_cb(&fade, dim_card_fade);
    lv_anim_start(&fade);

    if (s_dim_arrow) {
        lv_anim_t rise;
        lv_anim_init(&rise);
        lv_anim_set_var(&rise, s_dim_arrow);
        lv_anim_set_values(&rise, 5, -6);
        lv_anim_set_time(&rise, 900);
        lv_anim_set_playback_time(&rise, 900);
        lv_anim_set_repeat_count(&rise, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_exec_cb(&rise, dim_arrow_drift);
        lv_anim_start(&rise);
    }
}

static void show_dim_gate()
{
    if (s_dim_gate) return;
    s_dim_gate_armed = false;

    // Full-screen transparent gate: captures every touch so a stray tap on the
    // dimmed screen can neither wake the watch nor click the UI behind it. Until
    // it arms (15 s) a tap wakes; after, only a swipe up does.
    s_dim_gate = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_dim_gate);
    lv_obj_set_size(s_dim_gate, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(s_dim_gate, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_dim_gate, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(s_dim_gate, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_dim_gate, on_dim_gate_gesture, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_dim_gate, on_dim_gate_pressed, LV_EVENT_PRESSED, NULL);

    // Frosted "glass" card, centred, hidden until the first touch (once armed). A
    // translucent light fill over the dark dimmed screen reads as frosted glass
    // and lifts the words off the background so they stay legible. (A true
    // framebuffer blur is too costly on this software-rendered display, so this is
    // the cheap stand-in for the "water" look.)
    s_dim_card = lv_obj_create(s_dim_gate);
    lv_obj_remove_style_all(s_dim_card);
    lv_obj_set_size(s_dim_card, 300, 132);
    lv_obj_center(s_dim_card);
    lv_obj_clear_flag(s_dim_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_dim_card, LV_OBJ_FLAG_CLICKABLE);   // taps fall through to the gate
    lv_obj_set_style_radius(s_dim_card, 28, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_dim_card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_dim_card, 28, LV_PART_MAIN);          // ~11% frosted
    lv_obj_set_style_border_color(s_dim_card, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_dim_card, 90, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_dim_card, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_dim_card, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_dim_card, 110, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_dim_card, 24, LV_PART_MAIN);
    lv_obj_add_flag(s_dim_card, LV_OBJ_FLAG_HIDDEN);

    s_dim_arrow = lv_label_create(s_dim_card);
    lv_obj_set_style_text_font(s_dim_arrow, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_dim_arrow, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(s_dim_arrow, LV_SYMBOL_UP);
    lv_obj_align(s_dim_arrow, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *txt = lv_label_create(s_dim_card);
    lv_obj_set_style_text_font(txt, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(txt, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(txt, 2, LV_PART_MAIN);
    lv_label_set_text(txt, "swipe up to wake");
    lv_obj_align(txt, LV_ALIGN_BOTTOM_MID, 0, -20);
}

bool clock_screen_display_is_off() { return s_display_off; }

void clock_screen_set_battery_saver(bool on)
{
    s_batt_saver = on;
    if (!on) display_on();
}

void clock_screen_set_auto_brightness(bool on)
{
    s_auto_bright = on;
    sun_factor_update();
    s_sun_last_ms = millis();
    if (!s_is_dimmed && !s_display_off) instance.setBrightness(active_brightness());
}

void clock_screen_set_dim_timeout(uint32_t ms)
{
    s_dim_timeout_ms = ms;
    // Reset activity timer and restore brightness when timeout changes
    s_last_activity_ms = millis();
    display_on();
    if (s_is_dimmed) {
        s_is_dimmed = false;
        instance.setBrightness(active_brightness());
    }
}

void clock_screen_set_dim_brightness(uint8_t level)
{
    s_dim_brightness = level;
    // If already dimmed, apply new level immediately
    if (s_is_dimmed && !s_display_off) instance.setBrightness(s_dim_brightness);
}

static void dim_reset_activity()
{
    s_last_activity_ms = millis();
    hide_dim_gate();          // waking: drop the swipe-to-wake gate
    cpu_set_low(false);       // restore 240 MHz now so the wake frame is snappy
    display_on();
    if (s_is_dimmed) {
        s_is_dimmed = false;
        // Wake to the active brightness (Settings level, sun-scaled when Auto
        // brightness is on), not the hardware maximum.
        instance.setBrightness(active_brightness());
    }
}

// Keep-awake: some screens are meant to be watched without touching (the
// compass, the presence radar), so they must not dim out or fall back to the
// home clock under the inactivity timer. They raise this while shown and drop
// it on exit; the dim loop skips dimming entirely while it is set. Raising it
// also resets the activity clock so there is no stale-timeout dim in the gap.
static bool s_keep_awake = false;
void ui_keep_awake(bool on)
{
    s_keep_awake = on;
    dim_reset_activity();   // fresh timeout both on entry and on the exit back
}

// Put the panel back to whatever it should show right now: the dim level while
// dimmed, otherwise the active brightness. Used after a temporary override (the
// notification banner's brightness boost) ends.
void clock_screen_restore_brightness()
{
    if (s_display_off) return;
    instance.setBrightness(s_is_dimmed ? s_dim_brightness : active_brightness());
}

// 1 Hz: follow the sun, remember GPS fixes, and let the battery saver switch
// the screen off once it has been dimmed long enough.
// Locally-generated "system" notifications (battery, etc.), published through
// the same pipeline as phone notifications: banner + list + unread badge. Each
// event carries a STABLE uid so a re-fire updates in place instead of stacking,
// and the callers guard each so it fires once on its edge.
static constexpr uint32_t SYS_UID_SAVER = 0x5A5E0001;
static constexpr uint32_t SYS_UID_CRIT  = 0x5A5E0002;
static constexpr uint32_t SYS_UID_FULL  = 0x5A5E0003;
static constexpr uint32_t SYS_UID_PAIR  = 0x5A5E0004;

static void sys_notify(uint32_t uid, const char *title, const char *body)
{
    notify::Notification n;
    n.uid      = uid;
    n.category = notify::Category::System;
    snprintf(n.app,   sizeof(n.app),   "System");
    snprintf(n.title, sizeof(n.title), "%s", title);
    snprintf(n.body,  sizeof(n.body),  "%s", body);
    notify::publish(n);
}

// ---- Find My Watch / Find My Phone -----------------------------------------
// Bidirectional "find" over the ANS find characteristic (companion app only):
//  - phone -> watch: the phone writes a ring op; the watch wakes, shows a bold
//    flashing overlay and buzzes until Stopped or a timeout (find_alert_*).
//  - watch -> phone: the Find screen's button calls find_ring_phone(), which
//    notifies the phone so the app rings/vibrates.
static lv_obj_t   *s_find_overlay = nullptr;
static lv_timer_t *s_find_timer   = nullptr;
static uint32_t    s_find_ticks   = 0;
static constexpr uint32_t FIND_ALERT_PERIOD_MS = 650;
static constexpr uint32_t FIND_ALERT_TICKS_MAX = 30;   // ~20 s then give up

static void find_alert_stop();

static void find_alert_tick(lv_timer_t *)
{
    if (!s_find_overlay) return;
    instance.vibrator();                                   // insistent buzz
    bool on = (s_find_ticks & 1) == 0;                     // flash for visibility
    lv_obj_set_style_bg_color(s_find_overlay,
        on ? lv_color_hex(0xE02020) : lv_color_black(), LV_PART_MAIN);
    if (++s_find_ticks >= FIND_ALERT_TICKS_MAX) find_alert_stop();
}

static void on_find_stop(lv_event_t *)
{
    find_alert_stop();   // now notifies the phone (find_notify 0x00) itself
}

static void find_alert_start()
{
    if (s_find_overlay) return;
    dim_reset_activity();      // wake + active brightness so the alert is seen
    s_find_ticks = 0;

    s_find_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_find_overlay);
    lv_obj_set_size(s_find_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_find_overlay, lv_color_hex(0xE02020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_find_overlay, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_find_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(s_find_overlay);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(t, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(t, "FINDING WATCH");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -50);

    lv_obj_t *sub = lv_label_create(s_find_overlay);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(sub, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(sub, "Your phone is looking for this watch");
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, -6);

    lv_obj_t *btn = lv_button_create(s_find_overlay);
    lv_obj_set_size(btn, 170, 62);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 96);
    lv_obj_set_style_bg_color(btn, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 31, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, on_find_stop, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(bl, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(bl, "STOP");
    lv_obj_center(bl);

    // Find must be felt and heard no matter the comfort settings: force the
    // strongest buzz effect, and play the loud doorbell chime at max volume
    // through the speaker (reusing the alarm's I2S chime).
    haptic_force_max();
    alarm_play_chime_loop(100);
    instance.vibrator();
    s_find_timer = lv_timer_create(find_alert_tick, FIND_ALERT_PERIOD_MS, NULL);
}

static void find_alert_stop()
{
    bool was_active = (s_find_overlay != nullptr);
    if (s_find_timer)   { lv_timer_delete(s_find_timer);   s_find_timer   = nullptr; }
    if (s_find_overlay) { lv_obj_delete_async(s_find_overlay); s_find_overlay = nullptr; }
    alarm_stop_chime_loop();   // silence the speaker
    haptic_reapply();          // restore the user's normal buzz strength
    // Tell the phone the alert ended, by ANY path - manual STOP, the ~20s
    // timeout, or a phone-sent stop. Previously only the STOP button notified,
    // so a timeout left the companion app stuck on "ringing the watch". Guarded
    // on was_active so stopping with nothing running emits no spurious notify.
    if (was_active) ans::find_notify(0x00);
}

// The phone write arrives on the BLE host task, which must not touch LVGL. Latch
// the op here and let the main loop (LVGL thread) act on it in find_pump().
static volatile uint8_t s_find_req = 0xFF;   // 0xFF = nothing pending
static void find_handler(uint8_t op) { s_find_req = op; }

static void find_pump()
{
    uint8_t op = s_find_req;
    if (op == 0xFF) return;
    s_find_req = 0xFF;
    if (op == 0x01) find_alert_start();
    else            find_alert_stop();
}

// Public (used by find_screen.cpp): ring the phone from the watch. Returns false
// if no phone is connected over the companion channel.
bool find_ring_phone()      { return ans::find_notify(0x01); }
bool find_phone_connected() { return ans::is_connected(); }

// One-time pairing nudge on a genuinely fresh install (NVS wiped by a
// full-erase flash). The phone companion app can only discover the watch once a
// Notify mode is enabled (that is what advertises the BLE service), so a new
// user who just flashed sees nothing in the app until they enable it. On a plain
// reboot the saved Notify state is auto-restored (device_mode_restore_boot), so
// there is nothing to nudge and this stays silent. Shown at most once: the
// marker survives reboots and is only wiped by the same full erase that would
// warrant showing it again. Never forces a mode - it only tells the user where
// the switch is, leaving the WiFi/BLE arbitration untouched.
static void maybe_show_pairing_hint()
{
    // "argusnotify" gains the "en" key the first time the user ever toggles
    // Notify. Its absence means the watch has never been configured for phone
    // notifications - i.e. a fresh install, not a returning user who left it off.
    bool ever_configured = false;
    {
        Preferences np;
        if (np.begin("argusnotify", true)) {
            ever_configured = np.isKey("en");
            np.end();
        }
    }
    if (ever_configured) return;

    Preferences p;
    if (!p.begin("arguspair", false)) return;
    if (!p.getBool("hinted", false)) {
        sys_notify(SYS_UID_PAIR, "Connect your phone",
                   "Open Tools > Notify and tap Enable, then Scan in the phone app to pair.");
        p.putBool("hinted", true);
    }
    p.end();
}

static void display_power_tick()
{
    sun_location_note_fix();
    if (s_auto_bright && millis() - s_sun_last_ms >= AUTO_TICK_MS) {
        s_sun_last_ms = millis();
        float before = s_sun_factor;
        sun_factor_update();
        if (s_sun_factor != before && !s_is_dimmed && !s_display_off && !notify_popup_is_showing())
            instance.setBrightness(active_brightness());
    }
    // Auto-engage the saver under 20% (released with hysteresis, or on charge).
    // Kept separate from the user's toggle so the cell recovering doesn't clear
    // a saver they turned on themselves.
    int  batt_pct  = instance.pmu.getBatteryPercent();
    bool charging  = instance.pmu.isVbusIn();
    if (!s_low_batt_saver) {
        if (!charging && batt_pct >= 0 && batt_pct <= LOW_BATT_ENTER_PCT)
            s_low_batt_saver = true;
    } else if (charging || batt_pct >= LOW_BATT_EXIT_PCT) {
        s_low_batt_saver = false;
        // If the user never asked for the saver, undo the forced screen-off now.
        if (!s_batt_saver && s_display_off) display_on();
    }

    // System battery notifications, edge-triggered so each fires once.
    static bool s_saver_was  = false;
    static bool s_notif_crit = false;
    static bool s_notif_full = false;
    if (s_low_batt_saver && !s_saver_was)
        sys_notify(SYS_UID_SAVER, "Battery saver on",
                   "Low battery - saving power to extend runtime.");
    s_saver_was = s_low_batt_saver;

    if (charging) {
        s_notif_crit = false;                      // re-arm the critical warning
        if (!s_notif_full && batt_pct >= 100) {    // fully charged while plugged
            s_notif_full = true;
            sys_notify(SYS_UID_FULL, "Battery full", "Charged - you can unplug the watch.");
        }
    } else {
        s_notif_full = false;                      // re-arm "full" for the next charge
        if (!s_notif_crit && batt_pct >= 0 && batt_pct <= 5) {
            s_notif_crit = true;
            sys_notify(SYS_UID_CRIT, "Battery critical", "About to shut down - charge now.");
        }
    }

    if ((s_batt_saver || s_low_batt_saver) && s_is_dimmed && !s_display_off &&
        !notify_popup_is_showing() &&
        millis() - s_dimmed_at_ms >= SAVER_OFF_AFTER_MS)
        display_off();
}

// Public hook so full-screen utility screens (e.g. the Flashlight) can keep the
// display awake at active brightness for as long as they are shown. Also used
// by the notification banner to wake a dimmed or switched-off screen.
void ui_reset_dim_activity() { dim_reset_activity(); }

// ---- Charge-state feed + wake ----------------------------------------------
//
// Advance the shared bat_charge debouncer once per 1 Hz tick, on EVERY face
// (the Dot USB indicator and the classic charge bolt both read bat_charge, but
// only the classic update runs update_charge_bolt(), so the debouncer must be
// fed here or the Dot face never sees the charger). When the charger is first
// recognised, wake the screen out of dim/off for at least CHARGE_WAKE_MS so it
// is obvious the watch is charging.
static constexpr uint32_t CHARGE_WAKE_MS = 5000;
static uint32_t s_charge_wake_until_ms = 0;

static void charge_state_tick()
{
    static bool was_charging = false;
    bat_charge.update(instance.pmu.isVbusIn(), instance.pmu.isCharging());
    bool charging = bat_charge.state() != ChargeState::Discharging;
    if (charging && !was_charging) {
        s_charge_wake_until_ms = millis() + CHARGE_WAKE_MS;
        ui_reset_dim_activity();   // wake from dim / battery-saver-off
    }
    was_charging = charging;
}

// ---- Motion-wake ----------------------------------------------------------
//
// When enabled, the BHI260AP accelerometer is streamed at a low rate and any
// jump in magnitude greater than s_motion_delta_g is treated like a tap: the
// dim timer is reset and (if currently dimmed) the screen is brought back to
// full brightness. Default ON to match smartwatch wrist-raise behaviour;
// settings can switch it off if the user wants the dim timer to run even
// while the watch is being worn.
static SensorXYZ s_motion_accel(SensorBHI260AP::ACCEL_PASSTHROUGH, instance.sensor);
static bool      s_motion_wake_enabled    = true;
static bool      s_motion_accel_started   = false;
static float     s_motion_last_mag        = 0.0f;
// 10 Hz is enough to catch a wrist tilt without burning power; the BHI260
// fuses its own samples internally and only fires its interrupt when a
// sample is ready.
#define MOTION_SAMPLE_RATE_HZ   10.0f
// Threshold in g for "this counts as motion", set by Settings > Motion
// sensitivity (1 = needs a big, deliberate movement ... 5 = reacts to small
// ones). Stationary sample-to-sample noise is well under 0.05 g. The old fixed
// 0.20 g (now level 5) woke the screen on almost any wrist twitch, so the
// default is level 2.
static const float kMotionDeltaG[5] = { 0.70f, 0.55f, 0.40f, 0.30f, 0.20f };
static float s_motion_delta_g = kMotionDeltaG[1];   // level 2

void clock_screen_set_motion_sensitivity(int level)
{
    if (level < 1) level = 1;
    if (level > 5) level = 5;
    s_motion_delta_g = kMotionDeltaG[level - 1];
}

void clock_screen_set_motion_wake(bool enabled)
{
    s_motion_wake_enabled = enabled;
    if (enabled && !s_motion_accel_started) {
        // The BHI260AP firmware needs to be up before configuring virtual
        // sensors; this setter is called from the settings load + the UI
        // toggle, both of which run after instance.begin().
        s_motion_accel.enable(MOTION_SAMPLE_RATE_HZ, 0);
        s_motion_accel_started = true;
        // Drop the cached previous-magnitude so the first sample after a
        // restart isn't compared against a stale baseline.
        s_motion_last_mag = 0.0f;
    } else if (!enabled) {
        if (s_motion_accel_started) {
            s_motion_accel.disable();
            s_motion_accel_started = false;
        }
    }
}

// Pumped from the main loop after instance.loop() has drained the BHI260's
// sample queue. No-op when motion-wake is off or no new sample is in.
static void motion_wake_poll()
{
    if (!s_motion_wake_enabled || !s_motion_accel_started) return;
    if (!s_motion_accel.hasUpdated()) return;

    float x = s_motion_accel.getX();
    float y = s_motion_accel.getY();
    float z = s_motion_accel.getZ();
    float mag = sqrtf(x * x + y * y + z * z);

    // First sample after enable — no prior baseline, just seed and return.
    if (s_motion_last_mag == 0.0f) {
        s_motion_last_mag = mag;
        return;
    }

    float delta = fabsf(mag - s_motion_last_mag);
    s_motion_last_mag = mag;
    if (delta >= s_motion_delta_g) {
        dim_reset_activity();
    }
}

// Called by gps_screen after a quality fix to set the longitude-derived UTC offset
void clock_screen_set_utc_offset(int offset_hours)
{
    clock_utc_offset = offset_hours;
}

int clock_screen_get_utc_offset()
{
    return clock_utc_offset;
}

// Public repaint hook for the timezone module after it restores/refreshes the
// UTC offset (or re-syncs the RTC over WiFi).
void clock_screen_refresh()
{
    update_clock();
}

// True while the user has set the clock by hand — GPS time sync defers to it.
bool clock_screen_manual_time_active()
{
    return manual_time_override;
}

// Settings "Manual Time" switch: enable/disable the override without changing
// the clock. Turning it off lets the next GPS lock re-sync the RTC.
void clock_screen_set_manual_override(bool on)
{
    manual_time_override = on;
}

// Fill *out with the wall-clock local time currently shown on the face
// (the RTC holds UTC; clock_utc_offset shifts it to local).
void clock_screen_get_local_time(struct tm *out)
{
    instance.rtc.getDateTime(out);
    clocktime::tm_utc_to_local(out, clock_utc_offset);
}

// Apply a user-entered LOCAL date/time. The RTC always holds UTC (the world
// clock, Meshtastic and the SD log stamps all read it as UTC), so convert with
// the offset currently in force and keep that offset rather than zeroing it.
// Persisting the offset is what makes the setting survive a reboot: it used to
// be dropped to 0 in RAM only, so the next boot re-applied the last
// GPS/WiFi-detected offset to an RTC holding local time and the face came up
// that many hours off. Also enables the manual override so GPS/NTP sync stops
// touching the clock.
void clock_screen_apply_manual_time(int year, int mon, int day, int hour, int min)
{
    clocktime::DateTime local = { year, mon, day, hour, min, 0 };
    const clocktime::DateTime utc = clocktime::local_to_utc(local, clock_utc_offset);

    instance.rtc.setDateTime(utc.year, utc.mon, utc.day, utc.hour, utc.min, utc.sec);
    instance.rtc.hwClockRead();
    manual_time_override = true;
    rtc_build_seeded     = false;               // the seed has been superseded
    // Manual Time is a sync in the sense that matters here: the RTC was just
    // set deliberately. Stamped as Manual rather than Gps/Ntp because it is
    // only as correct as the person typing, which is precisely the case that
    // rode home from DEF CON seven hours wrong.
    timezone_note_synced(clock_utc_offset, clocksync::Source::Manual);
    update_clock();
}

// Called by gps_screen each second with the current satellite count
void clock_screen_set_sat_count(uint32_t count)
{
    if (count > 0)
        lv_label_set_text_fmt(gps_indicator, LV_SYMBOL_GPS " %lu", (unsigned long)count);
    else
        lv_label_set_text(gps_indicator, LV_SYMBOL_GPS);
    realign_status_icons();
}

// Skip bg_ticks for this many loop iterations after a screen transition
// is requested. Set by *_show() helpers; main loop drains this on each
// iteration. Gives LVGL guaranteed render budget when the wardriver +
// detector pipeline would otherwise stall the redraw.
static int s_lvgl_priority_cycles = 0;
void main_loop_request_lvgl_priority(int cycles) {
    if (cycles > s_lvgl_priority_cycles) s_lvgl_priority_cycles = cycles;
}

void clock_screen_show()
{
    // Pause matrix-rain so the synchronous refresh below doesn't have to
    // re-render 22 recolored labels every refresh tick. The main loop
    // also pauses during the priority window, but pausing here covers
    // the window in between (load + refresh + return).
    bool paused_matrix = matrix_bg_is_enabled();
    if (paused_matrix) matrix_bg_set_paused(true);

    lv_scr_load(clock_screen);
    lv_obj_invalidate(clock_screen);
    // Synchronous full refresh. lv_scr_load defers rendering to the next
    // lv_task_handler, and in partial mode with a full-screen buffer the
    // *buffer* only gets the dirty pixels written - non-dirty areas keep
    // whatever was rendered last, which is the previous screen's content.
    // Calling lv_refr_now after the invalidate guarantees the entire
    // screen's background + widgets land in the buffer in one shot
    // before we return to the busy main loop.
    lv_refr_now(NULL);

    if (paused_matrix) matrix_bg_set_paused(false);
    main_loop_request_lvgl_priority(12);
}

// Pad each side by this many px so the clock has breathing room from the
// screen edge. Kept small so the 116 px digits fill nearly the full width.
// NOTE: do NOT add negative letter-spacing here - the clock is a FIXED-mode
// spangroup and letter-spacing on it corrupts glyph layout (clipped/missing
// digits). Size the clock purely by picking the largest font that fits.
#define CLOCK_TEXT_PAD_X     4
// Cap the up-scale so the rendered glyphs don't get too soft. The base
// font is now the custom 96 px Montserrat subset (digits / colon / AM /
// PM / space), so 1.5× = ~144 px tall is the practical visual ceiling
// before the time overlaps with the date label below.
#define CLOCK_TEXT_MAX_SCALE (LV_SCALE_NONE * 3 / 2)
// Allow the scaler to shrink down to 50 % so long formats ("00:00:00 PM"
// in 12 h mode) still fit on a 410-wide screen. Without this floor the
// "never shrink" guard would clip the seconds off the right edge.
#define CLOCK_TEXT_MIN_SCALE (LV_SCALE_NONE / 2)

// Defined in lv_font_montserrat_clock_96.c — bigger base font generated
// from the project's bundled Montserrat-Medium.ttf via tools/gen_clock_font.py.
// Only contains 15 glyphs (0-9, ':', ' ', 'A', 'M', 'P') so the flash
// cost is ~24 KB of glyph data instead of the ~120 KB a full-character
// 96 px font would cost.
extern "C" const lv_font_t lv_font_montserrat_clock_96;
// Two smaller subset sizes for the adaptive-font clock (same 15 glyphs), so the
// widest formats fit WITHOUT a runtime transform. See tools/gen_clock_font.py.
extern "C" const lv_font_t lv_font_montserrat_clock_72;
extern "C" const lv_font_t lv_font_montserrat_clock_56;
// Larger 110 px Montserrat size so the "00:00" formats fill more of the 378 px
// usable width. resize_clock_text() only picks it when its worst-case string fits,
// so it is a pure "go bigger when there is room" option with no overflow risk.
extern "C" const lv_font_t lv_font_montserrat_clock_110;
// 116 px - the largest Montserrat size whose "00:00" fits the ~402 px usable
// width with no letter-spacing tricks. Selected only when it actually fits.
extern "C" const lv_font_t lv_font_montserrat_clock_116;

// Size the home-screen digital clock to fill the width, by SELECTING the largest
// pre-generated font whose worst-case string fits - NOT by transform-scaling one
// font. A runtime transform_scale fragmented/jumped the clock under LVGL
// partial-refresh whenever an overlapping layer (Matrix rain, SD wallpaper) or a
// busy radio invalidated part of it; a plain font has no transform, so partial
// redraws are always pixel-correct and it is glitch-free in every display mode.
// Memoised on (12h, ampm, secs, width) so the per-second tick is a no-op.
static void resize_clock_text()
{
    if (!time_label || !clock_screen) return;

    int screen_w = lv_obj_get_width(clock_screen);
    int usable_w = screen_w - 2 * CLOCK_TEXT_PAD_X;
    if (usable_w <= 0) return;

    uint32_t key = ((uint32_t)usable_w << 3)
                 | (clock_12h        ? 0x4 : 0)
                 | (clock_show_ampm  ? 0x2 : 0)
                 | (clock_show_secs  ? 0x1 : 0);
    static uint32_t s_cached_key = 0xFFFFFFFFu;   // force a pick on first call
    if (key == s_cached_key) return;              // same format+width -> no-op
    s_cached_key = key;

    // Worst-case digit string for the current format ("00…" wide glyphs + the
    // longest suffix). Selecting off the format, not the live text, keeps the
    // chosen size stable as the seconds tick.
    const char *ref;
    if (clock_12h) {
        if      (clock_show_secs && clock_show_ampm) ref = "00:00:00 PM";
        else if (clock_show_secs)                    ref = "00:00:00";
        else if (clock_show_ampm)                    ref = "00:00 PM";
        else                                         ref = "00:00";
    } else {
        ref = clock_show_secs ? "00:00:00" : "00:00";
    }

    // Largest font (widest -> narrowest) whose worst-case string fits usable_w.
    // The 56 px fallback fits every supported format, so a font is always chosen.
    const lv_font_t *fonts[] = {
        &lv_font_montserrat_clock_116,
        &lv_font_montserrat_clock_110,
        &lv_font_montserrat_clock_96,
        &lv_font_montserrat_clock_72,
        &lv_font_montserrat_clock_56,
    };
    const lv_font_t *chosen = fonts[4];
    for (unsigned i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
        lv_point_t sz;
        lv_text_get_size(&sz, ref, fonts[i], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x <= usable_w) { chosen = fonts[i]; break; }
    }
    lv_obj_set_style_text_font(time_label, chosen, LV_PART_MAIN);
    // No transform: guarantee any leftover scale from a prior build is cleared.
    lv_obj_set_style_transform_scale(time_label, LV_SCALE_NONE, LV_PART_MAIN);
    // Group font just changed; recompute FIXED-mode line geometry so the digit
    // spans and the smaller AM/PM span stay consistently laid out.
    lv_spangroup_refr_mode(time_label);
}

static void update_clock()
{
    struct tm t;
    instance.rtc.getDateTime(&t); // RTC stores UTC

    // Apply the persisted offset to get local time. tm_utc_to_local() also
    // refills tm_wday / tm_yday, which the day-name label and the calendar read.
    clocktime::tm_utc_to_local(&t, clock_utc_offset);

    if (clock_face == FACE_DOT) {
        update_dot_face(&t);
    } else {
        char hours_buf[4];
        char rest_buf[12];
        char ampm_buf[6];
        ampm_buf[0] = '\0';   // AM/PM span stays empty (invisible) unless 12h + show_ampm
        if (clock_12h) {
            int h = t.tm_hour % 12;
            if (h == 0) h = 12;
            snprintf(hours_buf, sizeof(hours_buf), "%d", h);
            if (clock_show_secs)
                snprintf(rest_buf, sizeof(rest_buf), "%02d:%02d", t.tm_min, t.tm_sec);
            else
                snprintf(rest_buf, sizeof(rest_buf), "%02d", t.tm_min);
            if (clock_show_ampm)
                snprintf(ampm_buf, sizeof(ampm_buf), " %s", t.tm_hour < 12 ? "AM" : "PM");
        } else {
            snprintf(hours_buf, sizeof(hours_buf), "%02d", t.tm_hour);
            if (clock_show_secs)
                snprintf(rest_buf, sizeof(rest_buf), "%02d:%02d", t.tm_min, t.tm_sec);
            else
                snprintf(rest_buf, sizeof(rest_buf), "%02d", t.tm_min);
        }
        lv_span_set_text(s_span_hours, hours_buf);
        lv_span_set_text(s_span_rest,  rest_buf);
        lv_span_set_text(s_span_ampm,  ampm_buf);   // smaller font; "" when unused
        // Blink by toggling the colon span's color — the character stays in
        // the string so the total width never changes and the digits stay put.
        static bool s_colon_on = true;
        if (!clock_show_secs) s_colon_on = !s_colon_on;
        lv_style_set_text_color(&s_span_colon->style,
                                (clock_show_secs || s_colon_on) ? lv_color_white() : lv_color_black());
        resize_clock_text();
    }

    char date_buf[32];
    if (clock_show_day && clock_show_date)
        strftime(date_buf, sizeof(date_buf), "%A\n%B %d, %Y", &t);
    else if (clock_show_day)
        strftime(date_buf, sizeof(date_buf), "%A", &t);
    else if (clock_show_date)
        strftime(date_buf, sizeof(date_buf), "%B %d, %Y", &t);
    else
        date_buf[0] = '\0';
    lv_label_set_text(date_label, date_buf);

    // Invalidate ONLY the clock's own area, not the whole screen. The earlier
    // full-screen-per-tick invalidate was a workaround for the transform_scale
    // (a scaled label drew past a label-only clear); that transform is gone now
    // (adaptive font selection), so the fixed-width time_label's own bounds cover
    // the whole time line and clear it cleanly. Just as important: a full-screen
    // flush every second maximised the window for WiFi to stall the PSRAM
    // framebuffer mid-flush -> the intermittent ghost. A tiny clock-area flush
    // (what base 13-37 does) barely exposes PSRAM, so the WiFi ghost goes away.
    lv_obj_invalidate(time_label);
}

// Firmware name + version surfaced in the boot banner so support tickets carry
// a fixed anchor. Bump FW_VERSION on each cut.
#define FW_NAME    "ARGUS"
#define FW_VERSION "0.1.3"   // ARGUS fork of r3dfish/13-37 (base 1.0.0)

// Saira Condensed boot-splash fonts generated via lv_font_conv; see
// src/font_argus_argus.c.
LV_FONT_DECLARE(font_argus_argus);

void setup()
{
    Serial.begin(115200);
    delay(50);   // let the USB-CDC link settle so the banner isn't truncated
    Serial.printf("\n%s firmware v%s  (T-Watch Ultra)  build %s %s\n",
                  FW_NAME, FW_VERSION, __DATE__, __TIME__);

    instance.begin();
    coex_log_heap("after-instance-begin");
    instance.powerControl(POWER_NFC, false); // ensure NFC is off on boot
    beginLvglHelper(instance);
    // Raise the scroll threshold so a tap that drifts a few pixels still registers
    // as a click, not a scroll. Fixes finicky buttons inside scrollable pages (e.g.
    // Exit Offense at the bottom of Settings). LVGL default is ~10 px.
    for (lv_indev_t *id = lv_indev_get_next(NULL); id; id = lv_indev_get_next(id))
        if (lv_indev_get_type(id) == LV_INDEV_TYPE_POINTER)
            lv_indev_set_scroll_limit(id, 20);
    // NOTE: bringing the BLE controller up here (ble_scan_boot_keepalive) crashed
    // boot — too early / unsafe at this point in init. Reverted. The keep-alive
    // approach is still right, but it must be invoked LATER (after full init) and
    // verified. For now the BT toggle brings the controller up on demand.

    // Time fallback: with no GPS fix and no WiFi/NTP, the RTC can come up unset and
    // the clock reads wildly wrong. If the RTC year is implausible, seed it from the
    // firmware BUILD time (__DATE__/__TIME__), which is local wall-clock, CONVERTED
    // TO UTC so the "RTC holds UTC" invariant survives the fallback. The offset to
    // convert with is the one timezone_load_on_boot() will restore later in setup,
    // so peek at it now; if it turns out to differ, the seed is rebased right after
    // that call. Approximate (build time, not flash time); a later GPS fix / NTP
    // sync overrides it precisely. Placed after beginLvglHelper so USB/console are
    // up first, and before detect_log_sweep_all() so the retention sweep has a
    // plausible date to age against.
    {
        struct tm rn;
        instance.rtc.getDateTime(&rn);
        if (rn.tm_year + 1900 < 2025) {
            const char *D = __DATE__;   // "Mmm dd yyyy" (day may be space-padded)
            const char *T = __TIME__;   // "hh:mm:ss"
            const char *M = "JanFebMarAprMayJunJulAugSepOctNovDec";
            int mon = 1;
            for (int i = 0; i < 12; i++)
                if (D[0]==M[i*3] && D[1]==M[i*3+1] && D[2]==M[i*3+2]) { mon = i+1; break; }
            int day = (D[4]==' ' ? 0 : (D[4]-'0')*10) + (D[5]-'0');
            int yr  = (D[7]-'0')*1000 + (D[8]-'0')*100 + (D[9]-'0')*10 + (D[10]-'0');
            int hh  = (T[0]-'0')*10 + (T[1]-'0');
            int mm  = (T[3]-'0')*10 + (T[4]-'0');
            int ss  = (T[6]-'0')*10 + (T[7]-'0');

            clock_utc_offset   = timezone_peek_saved_offset(clock_utc_offset);
            rtc_build_seed_off = clock_utc_offset;
            rtc_build_seeded   = true;

            clocktime::DateTime bl = { yr, mon, day, hh, mm, ss };
            const clocktime::DateTime bu = clocktime::local_to_utc(bl, clock_utc_offset);

            instance.rtc.setDateTime(bu.year, bu.mon, bu.day, bu.hour, bu.min, bu.sec);
            instance.rtc.hwClockRead();
        }
    }

    // Boot splash — ARGUS lockup on the panel before the clock comes up.
    // Backlight on now so it's visible; the splash stays up through the rest of
    // setup (screen construction) and is swapped for the clock below, held to a
    // minimum visible time. Brand typeface is Saira Condensed (src/font_argus_*.c),
    // filled red (#E02020, the Dot-face accent) on black, with a "DotOS" subtitle.
    // Restore the wearer's Facewatch accent now (before the splash) so the boot
    // brand uses their chosen colour too. Safe this early: NVS is up from the
    // start of setup, and a failed read just falls back to the default accent.
    face_watch_boot_restore();

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);
    lv_obj_t *boot_splash = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(boot_splash, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(boot_splash, 0, LV_PART_MAIN);


    // ARGUS hero, tinted with the user's accent colour.
    lv_obj_t *boot_brand = lv_label_create(boot_splash);
    lv_label_set_text(boot_brand, FW_NAME);   // "ARGUS"
    lv_obj_set_style_text_color(boot_brand, lv_color_hex(face_accent_rgb()), LV_PART_MAIN);
    lv_obj_set_style_text_font(boot_brand, &font_argus_argus, LV_PART_MAIN);
    lv_obj_align(boot_brand, LV_ALIGN_CENTER, 0, 8);

    // "DotOS" subtitle, just under the hero.
    lv_obj_t *boot_sub = lv_label_create(boot_splash);
    lv_label_set_text(boot_sub, "DotOS");
    lv_obj_set_style_text_color(boot_sub, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(boot_sub, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(boot_sub, 4, LV_PART_MAIN);
    lv_obj_align_to(boot_sub, boot_brand, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    lv_scr_load(boot_splash);
    lv_refr_now(NULL);                       // paint now; no timer handler in setup yet
    uint32_t boot_splash_ms = millis();

    // Register the USB Mass Storage interface and start the USB stack. Must run
    // after instance.begin() mounts the SD card; the card stays hidden from the
    // host until the USB SD screen mounts it.
    usb_sd_init();

    // Load the saved APRS callsign from the SD card (if present).
    aprs_init();

    // Load the saved alarm-clock configuration from the SD card (if present).
    alarm_init();

    // Expire stale detection records. The per-detector writers also enforce
    // retention as they append, but that only fires when something is DETECTED.
    // A watch that sits unused for a month would otherwise keep last month's
    // records indefinitely, so the window is also swept once at boot.
    detect_log_sweep_all();

    // Build the clock screen
    clock_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(clock_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(clock_screen, 0, LV_PART_MAIN);

    // Lowest z-order: the SD-card wallpaper renders behind everything,
    // including the matrix rain. Created before matrix_bg_create so it is
    // the first child. Hidden until the "Wallpaper" setting turns it on.
    background_create(clock_screen);

    // First child → renders behind every other widget on the clock screen
    matrix_bg_create(clock_screen);

    // GPS indicator — anchored to top-right; others chain off it via realign_status_icons()
    gps_indicator = lv_label_create(clock_screen);
    lv_obj_set_style_text_font(gps_indicator, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(gps_indicator, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_label_set_text(gps_indicator, LV_SYMBOL_GPS);
    lv_obj_align(gps_indicator, LV_ALIGN_TOP_RIGHT, -70, 20);

    // Wardriver indicator — flex container with green WiFi count and blue BT count
    wardriver_container = lv_obj_create(clock_screen);
    lv_obj_set_size(wardriver_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wardriver_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(wardriver_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wardriver_container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(wardriver_container, 2, LV_PART_MAIN);
    lv_obj_clear_flag(wardriver_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(wardriver_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(wardriver_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wardriver_container,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    wardriver_wifi_label = lv_label_create(wardriver_container);
    lv_obj_set_style_text_font(wardriver_wifi_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(wardriver_wifi_label, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_label_set_text(wardriver_wifi_label, LV_SYMBOL_EYE_OPEN);

    wardriver_bt_label = lv_label_create(wardriver_container);
    lv_obj_set_style_text_font(wardriver_bt_label, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(wardriver_bt_label, lv_color_make(0x55, 0x99, 0xFF), LV_PART_MAIN);
    lv_label_set_text(wardriver_bt_label, "");
    lv_obj_add_flag(wardriver_bt_label, LV_OBJ_FLAG_HIDDEN);

    // WiFi indicator
    wifi_indicator = lv_label_create(clock_screen);
    lv_obj_set_style_text_font(wifi_indicator, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(wifi_indicator, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_label_set_text(wifi_indicator, LV_SYMBOL_WIFI);

    // Bluetooth indicator
    bt_indicator = lv_label_create(clock_screen);
    lv_obj_set_style_text_font(bt_indicator, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(bt_indicator, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_label_set_text(bt_indicator, LV_SYMBOL_BLUETOOTH);

    // SD card indicator
    sd_indicator = lv_label_create(clock_screen);
    lv_obj_set_style_text_font(sd_indicator, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(sd_indicator, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_label_set_text(sd_indicator, LV_SYMBOL_SD_CARD);

    // NFC indicator (between SD and LoRa) — arc logo image, recolored for on/off state
    nfc_indicator = lv_image_create(clock_screen);
    lv_image_set_src(nfc_indicator, &nfc_icon_dsc);
    lv_obj_set_style_image_recolor(nfc_indicator, lv_color_make(0x33, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(nfc_indicator, LV_OPA_COVER, LV_PART_MAIN);

    // LoRa antenna indicator (leftmost) — composite widget built separately
    build_lora_indicator(clock_screen);

    // Unread-Meshtastic-message badge, sits to the immediate left of the
    // LoRa icon. White count on a red pill, hidden when count == 0.
    mesh_top_count_label = lv_label_create(clock_screen);
    lv_obj_set_style_text_color(mesh_top_count_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(mesh_top_count_label, &font_argus_mono_16, LV_PART_MAIN);   // VT323 (brand readout)
    lv_obj_set_style_bg_color(mesh_top_count_label, lv_color_make(0xC0, 0x20, 0x20), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(mesh_top_count_label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(mesh_top_count_label, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(mesh_top_count_label, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(mesh_top_count_label, 1, LV_PART_MAIN);
    lv_label_set_text(mesh_top_count_label, "0");
    lv_obj_add_flag(mesh_top_count_label, LV_OBJ_FLAG_HIDDEN);

    // Spangroup so the blinking colon can change color without changing
    // the string width — keeping hours and minutes pixel-stable.
    time_label = lv_spangroup_create(clock_screen);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_clock_96, LV_PART_MAIN);
    lv_obj_set_style_text_color(time_label, lv_color_white(), LV_PART_MAIN);
    lv_spangroup_set_align(time_label, LV_TEXT_ALIGN_CENTER);
    lv_spangroup_set_mode(time_label, LV_SPAN_MODE_FIXED);
    lv_obj_set_width(time_label, lv_pct(100));
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -40);

    s_span_hours = lv_spangroup_new_span(time_label);
    lv_span_set_text(s_span_hours, "00");

    s_span_colon = lv_spangroup_new_span(time_label);
    lv_span_set_text(s_span_colon, ":");

    s_span_rest = lv_spangroup_new_span(time_label);
    lv_span_set_text(s_span_rest, "00");

    // AM/PM in its own span at a smaller fixed font, so it reads as a suffix
    // rather than a full-height part of the time. Stays empty (invisible) unless
    // the format is 12h with AM/PM shown. Montserrat 32 is roughly half the
    // adaptive digit font (56-96 px), so it's a clear, readable suffix.
    s_span_ampm = lv_spangroup_new_span(time_label);
    lv_span_set_text(s_span_ampm, "");
    lv_style_set_text_font(&s_span_ampm->style, &lv_font_montserrat_32);
    lv_style_set_text_color(&s_span_ampm->style, lv_color_white());
    // The digit spans get their font from the group (swapped adaptively by
    // resize_clock_text). This span carries its OWN smaller font, so the group's
    // cached FIXED-mode line geometry must be recomputed to account for the
    // mixed font metrics - without this refresh the group renders stale/invalid
    // geometry (the earlier crash). Recompute once here; the adaptive resize
    // re-triggers its own refresh whenever it swaps the group font.
    lv_spangroup_refr_mode(time_label);

    date_label = lv_label_create(clock_screen);
    lv_obj_set_style_text_color(date_label, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(date_label, theme_text_font(20), LV_PART_MAIN);   // Orbitron (brand label)
    lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(date_label, "");
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 60);

    build_battery_widget(clock_screen);

    // Alarm-enabled indicator — bell glyph positioned to the left of the
    // battery widget, vertically centred with it. Hidden by default; shown
    // when the alarm module reports the alarm enabled.
    alarm_indicator = lv_label_create(clock_screen);
    lv_obj_set_style_text_font(alarm_indicator, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(alarm_indicator, ARGUS_TEXT, LV_PART_MAIN);
    lv_label_set_text(alarm_indicator, LV_SYMBOL_BELL);
    lv_obj_align(alarm_indicator, LV_ALIGN_BOTTOM_MID, -95, -10);
    lv_obj_add_flag(alarm_indicator, LV_OBJ_FLAG_HIDDEN);

    // Timer + stopwatch indicators — sit further left of the alarm bell.
    // Each is a small custom ring icon; only shown while their respective
    // module reports actively running. The two icons differ in cap shape
    // (wide knob vs narrow stem) and hand angle so they're distinguishable
    // at a glance even at 20 px wide.
    stopwatch_indicator = build_clock_icon(clock_screen,
                                           /*wide_cap=*/false,
                                           /*hand_rotation_deci_deg=*/450);   // 1:30
    lv_obj_align(stopwatch_indicator, LV_ALIGN_BOTTOM_MID, -125, -10);

    timer_indicator     = build_clock_icon(clock_screen,
                                           /*wide_cap=*/true,
                                           /*hand_rotation_deci_deg=*/-450);  // 10:30
    lv_obj_align(timer_indicator,     LV_ALIGN_BOTTOM_MID, -158, -10);

    // AirTag scanner indicator — flex row of disc-icon + count. Hidden until
    // airtag_is_running(); update_airtag_indicator() positions it left of the
    // mesh icon when both are visible, or centered when only this one is.
    airtag_indicator = lv_obj_create(clock_screen);
    lv_obj_set_size(airtag_indicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(airtag_indicator, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(airtag_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(airtag_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(airtag_indicator, 4, LV_PART_MAIN);
    lv_obj_clear_flag(airtag_indicator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(airtag_indicator, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(airtag_indicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(airtag_indicator,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(airtag_indicator, LV_OBJ_FLAG_HIDDEN);

    // Disc body
    lv_obj_t *airtag_disc = lv_obj_create(airtag_indicator);
    lv_obj_set_size(airtag_disc, 22, 22);
    lv_obj_set_style_radius(airtag_disc, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(airtag_disc, lv_color_make(0xEE, 0xEE, 0xEE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(airtag_disc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(airtag_disc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(airtag_disc, 0, LV_PART_MAIN);
    lv_obj_clear_flag(airtag_disc, LV_OBJ_FLAG_SCROLLABLE);

    // Inner dot inside the disc (AirTag's Apple-logo placement)
    lv_obj_t *airtag_dot = lv_obj_create(airtag_disc);
    lv_obj_set_size(airtag_dot, 6, 6);
    lv_obj_set_style_radius(airtag_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(airtag_dot, lv_color_make(0x99, 0x99, 0x99), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(airtag_dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(airtag_dot, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(airtag_dot, 0, LV_PART_MAIN);
    lv_obj_clear_flag(airtag_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(airtag_dot);

    // Discovery count
    airtag_count_label = lv_label_create(airtag_indicator);
    lv_obj_set_style_text_font(airtag_count_label, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(airtag_count_label, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(airtag_count_label, "0");

    // Flipper Zero indicator — tiny cyan dolphin-pill + count. Same layout
    // pattern as the AirTag indicator next to it. Hidden until first
    // detection; update_scan_indicators() positions it left of AirTag.
    flipper_indicator = lv_obj_create(clock_screen);
    lv_obj_set_size(flipper_indicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(flipper_indicator, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(flipper_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(flipper_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(flipper_indicator, 4, LV_PART_MAIN);
    lv_obj_clear_flag(flipper_indicator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(flipper_indicator, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(flipper_indicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(flipper_indicator,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(flipper_indicator, LV_OBJ_FLAG_HIDDEN);

    // Orange dolphin-pill — at this size it reads as a small dolphin silhouette.
    lv_obj_t *flipper_body = lv_obj_create(flipper_indicator);
    lv_obj_set_size(flipper_body, 26, 14);
    lv_obj_set_style_radius(flipper_body, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(flipper_body, lv_color_make(0xFF, 0x88, 0x00), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(flipper_body, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(flipper_body, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(flipper_body, 0, LV_PART_MAIN);
    lv_obj_clear_flag(flipper_body, LV_OBJ_FLAG_SCROLLABLE);

    // Tiny dark eye, anchored inside the pill near the "front"
    lv_obj_t *flipper_eye = lv_obj_create(flipper_body);
    lv_obj_set_size(flipper_eye, 3, 3);
    lv_obj_set_style_radius(flipper_eye, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(flipper_eye, lv_color_make(0x11, 0x11, 0x11), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(flipper_eye, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(flipper_eye, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(flipper_eye, 0, LV_PART_MAIN);
    lv_obj_clear_flag(flipper_eye, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(flipper_eye, LV_ALIGN_LEFT_MID, 4, -1);

    flipper_count_label = lv_label_create(flipper_indicator);
    lv_obj_set_style_text_font(flipper_count_label, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(flipper_count_label, lv_color_make(0xFF, 0x88, 0x00), LV_PART_MAIN);
    lv_label_set_text(flipper_count_label, "0");

    // Skimmer indicator — red "SK" badge + count. Hidden until the
    // wardriver / standalone scanner flags an HC-0x device. Sits between
    // the Flipper indicator and the EvilTwin indicator.
    skimmer_indicator = lv_obj_create(clock_screen);
    lv_obj_set_size(skimmer_indicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(skimmer_indicator, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(skimmer_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(skimmer_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(skimmer_indicator, 4, LV_PART_MAIN);
    lv_obj_clear_flag(skimmer_indicator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(skimmer_indicator, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(skimmer_indicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(skimmer_indicator,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(skimmer_indicator, LV_OBJ_FLAG_HIDDEN);

    // Red badge with "SK" letters — reads as "skimmer alert" at indicator scale.
    lv_obj_t *sk_badge = lv_obj_create(skimmer_indicator);
    lv_obj_set_size(sk_badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(sk_badge, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sk_badge, lv_color_make(0xCC, 0x22, 0x22), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sk_badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sk_badge, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(sk_badge, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(sk_badge, 1, LV_PART_MAIN);
    lv_obj_clear_flag(sk_badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sk_lbl = lv_label_create(sk_badge);
    lv_obj_set_style_text_font(sk_lbl, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(sk_lbl, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(sk_lbl, "SK");

    skimmer_count_label = lv_label_create(skimmer_indicator);
    lv_obj_set_style_text_font(skimmer_count_label, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(skimmer_count_label, lv_color_make(0xFF, 0x66, 0x66), LV_PART_MAIN);
    lv_label_set_text(skimmer_count_label, "0");

    // Evil-twin indicator — "ET" tag in alert orange + count. Hidden until
    // the wardriver flags at least one same-SSID/different-auth conflict;
    // sits between the Skimmer indicator and the Flock indicator in the
    // home-screen status row.
    evil_twin_indicator = lv_obj_create(clock_screen);
    lv_obj_set_size(evil_twin_indicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(evil_twin_indicator, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(evil_twin_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(evil_twin_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(evil_twin_indicator, 4, LV_PART_MAIN);
    lv_obj_clear_flag(evil_twin_indicator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(evil_twin_indicator, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(evil_twin_indicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(evil_twin_indicator,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(evil_twin_indicator, LV_OBJ_FLAG_HIDDEN);

    // "ET" badge — small orange pill with white letters reads as
    // "evil twin alert" at the indicator's tiny size.
    lv_obj_t *et_badge = lv_obj_create(evil_twin_indicator);
    lv_obj_set_size(et_badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(et_badge, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(et_badge, lv_color_make(0xFF, 0x66, 0x00), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(et_badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(et_badge, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(et_badge, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(et_badge, 1, LV_PART_MAIN);
    lv_obj_clear_flag(et_badge, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *et_lbl = lv_label_create(et_badge);
    lv_obj_set_style_text_font(et_lbl, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(et_lbl, lv_color_white(), LV_PART_MAIN);
    lv_label_set_text(et_lbl, "ET");

    evil_twin_count_label = lv_label_create(evil_twin_indicator);
    lv_obj_set_style_text_font(evil_twin_count_label, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(evil_twin_count_label, lv_color_make(0xFF, 0x88, 0x00), LV_PART_MAIN);
    lv_label_set_text(evil_twin_count_label, "0");

    // Flock/OUI indicator — warning icon + count, hidden until first detection.
    // Shown to the LEFT of the AirTag indicator.
    flock_indicator = lv_obj_create(clock_screen);
    lv_obj_set_size(flock_indicator, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(flock_indicator, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(flock_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(flock_indicator, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(flock_indicator, 4, LV_PART_MAIN);
    lv_obj_clear_flag(flock_indicator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(flock_indicator, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(flock_indicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(flock_indicator,
        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(flock_indicator, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *flock_icon = lv_label_create(flock_indicator);
    lv_obj_set_style_text_font(flock_icon, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(flock_icon, lv_color_make(0xFF, 0x88, 0x00), LV_PART_MAIN);
    lv_label_set_text(flock_icon, LV_SYMBOL_WARNING);

    flock_count_label = lv_label_create(flock_indicator);
    lv_obj_set_style_text_font(flock_count_label, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(flock_count_label, lv_color_make(0xFF, 0x88, 0x00), LV_PART_MAIN);
    lv_label_set_text(flock_count_label, "0");

    // ARGUS mode state machine (Daily/Defense/Offense). Init BEFORE any screen is
    // created so gating/theming can read the mode as screens build. Forces Daily
    // at boot (or Defense only if the user enabled Defense-persistence).
    argus_mode_init();

    gps_screen_create();
    lora_screen_create();
    nfc_screen_create();
    nfc_write_screen_create();
    meshtastic_screen_create();
    nodes_screen_create();
    send_message_screen_create();
    map_screen_create();
    configuration_screen_create();
    channels_screen_create();
    settings_screen_create();
    tools_screen_create();
    apps_screen_create();     // unified launcher; tools/time _show() forward here
    notifications_screen_create();
    notify_popup_init();
    threat_radar_screen_create();
    pet_screen_create();
    tpms_screen_create();
    pager_screen_create();
    mouse_screen_create();
    usb_sd_screen_create();
    aprs_screen_create();
    tesla_cp_screen_create();
    wifi_screen_create();
    wifi_radio_screen_create();
    bluetooth_screen_create();
    portscan_screen_create();
    analyze_screen_create();
    bt_analyze_screen_create();
    lora_analyze_screen_create();
    stopwatch_screen_create();
    timer_screen_create();
    alarm_screen_create();
    calendar_screen_create();
    world_clock_screen_create();
    sun_moon_screen_create();
    spycam_screen_create();
    nfc_field_screen_create();
    pin_pad_screen_create();
    loot_screen_create();
    deauth_screen_create();
    tracker_timeline_screen_create();
    beacon_spam_screen_create();
    deauth_attack_screen_create();
    rogue_ap_screen_create();
    probe_sniffer_screen_create();
    offense_wipe_register();   // arm the duress-shred Tier-1 wipe hook
    time_screen_create();
    health_screen_create();
    flashlight_screen_create();
    wardriver_screen_create();
    // Dot face layer, created last so its opaque panel sits above every other
    // clock_screen child; hidden until the Dot face is selected.
    build_dot_face(clock_screen);
    clock_screen_set_face(FACE_DOT);   // default face; a saved choice overrides it in settings_screen_load()
    sun_location_load();               // last GPS fix, for Auto brightness (before settings load)
    lv_obj_add_event_cb(clock_screen, on_clock_gesture, LV_EVENT_GESTURE, NULL);
    watch_touch_pulls();   // top-edge pull-down -> notification shade, from any screen
    // Hold the boot splash to a minimum ~1.5 s, then reveal the clock.
    while (millis() - boot_splash_ms < 1500) delay(10);
    lv_scr_load(clock_screen);
    lv_obj_del(boot_splash);
    s_last_activity_ms = millis();
    coex_log_heap("before-ble");

#if ARGUS_RADIO_COEXIST || ARGUS_COEX_MEASURE
    // Bring the BLE (Bluedroid) controller up ONCE now - late in setup, after the
    // display/LVGL and clock are up, and while WiFi is still OFF. This is the
    // coexistence-safe order (BLE before WiFi); the controller then stays alive so
    // WiFi coming up later COEXISTS instead of hanging. Matches upstream r3dfish;
    // the mutual-exclusion guards are compiled out via radio_coexist.h. If this
    // ever hangs the watch, flip ARGUS_RADIO_COEXIST to 0 and reflash.
    ble_scan_boot_keepalive();
    coex_log_heap("after-ble");
#endif

    // Power button short press cycles forward through screens:
    //   - clock -> GPS -> LoRa -> WiFi -> Bluetooth -> NFC
    //   - meshtastic -> nodes -> send_message -> map (when tiles exist)
    //     -> configuration (the same chain swipe-RIGHT follows on the
    //     Meshtastic family of screens, so the user can navigate via
    //     buttons or gestures interchangeably)
    //   - settings -> clock
    instance.onEvent([](DeviceEvent_t event, void *params, void *user_data) {
        if (instance.getPMUEventType(params) == PMU_EVENT_KEY_CLICKED) {
            // A button press wakes the watch directly (buttons are sturdy - no
            // accidental press), so when the screen is dimmed or off the press
            // only wakes and does not also advance the screen chain.
            bool was_asleep = clock_screen_display_is_off() || s_is_dimmed;
            dim_reset_activity();
            if (was_asleep) return;
            if (clock_vibrate) instance.vibrator();
            if (lv_screen_active() == clock_screen)
                gps_screen_show();
            else if (gps_screen_is_active())
                lora_screen_show();
            else if (lora_screen_is_active())
                bluetooth_screen_show();      // BT before WiFi: bring BLE up before
            else if (bluetooth_screen_is_active())
                wifi_radio_screen_show();      // WiFi comes up second (coexistence order)
            else if (wifi_radio_screen_is_active())
                nfc_screen_show();
            else if (meshtastic_screen_is_active())
                nodes_screen_show();
            else if (nodes_screen_is_active())
                send_message_screen_show();
            else if (send_message_screen_is_active()) {
                if (map_screen_available()) map_screen_show();
                else                        configuration_screen_show();
            } else if (map_screen_is_active())
                configuration_screen_show();
            // configuration_screen: swipe-RIGHT is unbound, so power
            // button is intentionally a no-op there too.
            else if (settings_screen_is_active())
                clock_screen_show();
        }
    }, POWER_EVENT, NULL);

    // Back button (GPIO0): GPS → clock
    pinMode(0, INPUT_PULLUP);
    attachInterrupt(0, on_back_btn_isr, FALLING);

    sd_was_ready = instance.isCardReady(); // sync with whatever instance.begin() mounted
    // Pull persisted meshtastic channel state off the SD card now
    // that it's mounted. Safe before LoRa is enabled - channels are
    // just data; meshtastic_set_active() reads from s_channels when
    // it starts transmitting.
    meshtastic_load_channels_from_sd();
    // NOTE: the user's "Enable at boot" radios are brought up LOWER DOWN, AFTER
    // settings_screen_load() has enabled the wallpaper and we have forced it to
    // decode. Bringing a radio up here (before the wallpaper decodes) leaves too
    // little free internal SRAM for the PNG decode -> OOM boot-loop when both a
    // wallpaper AND a boot radio (esp. WiFi ~45 KB) are enabled. See below.
    realign_status_icons();
    layout_battery_indicators(); // seed packing so a boot-enabled alarm renders immediately
    update_clock();
    // Seed the charge readout from the live PMU so a watch booted on the
    // charger shows the bolt on the first frame rather than after the first
    // state change.
    bat_charge.prime(instance.pmu.isVbusIn(), instance.pmu.isCharging());
    update_battery();
    update_lora_indicator();
    update_bt_indicator();
    update_wifi_indicator();
    update_sd_indicator();
    update_nfc_indicator();
    update_wardriver_indicator();

    // Per-mode indicator overlay (Offense border frame only; the "DEF" / "OFF"
    // corner chip is disabled - see theme.cpp). Init now that LVGL + argus_mode
    // are up; refresh on every mode change. The 1 Hz loop tick also refreshes it
    // so the Offense border flips to threat-red live.
    argus_mode_indicator_init();
    argus_mode_on_change([](ArgusMode) { argus_mode_indicator_refresh(); });

    instance.setBrightness(DEVICE_MAX_BRIGHTNESS_LEVEL);

    // Bring the motion-wake accelerometer up to its default state before
    // loading settings — settings_screen_load() will flip it off again if
    // the user has it disabled in /Settings/settings.txt. Doing this here
    // (rather than at the static-init / declaration site) means it happens
    // after instance.begin() has finished bringing the BHI260 firmware up.
    clock_screen_set_motion_wake(true);

    // Restore persisted settings from the SD card (if mounted and file exists).
    // Called after the default setBrightness so a saved brightness wins.
    settings_screen_load();

    // Timezone: restore the saved UTC offset so the face shows correct local time
    // immediately. MUST run after settings_screen_load(): the Manual Time flag is
    // what tells the v1-file migration whether the offset on the card pairs with a
    // UTC RTC (Manual Time off) or a local one written by the old manual-time code
    // (Manual Time on). Then register the WiFi auto-sync hook + background worker.
    timezone_load_on_boot();

    // If the RTC was seeded from the build time above with a provisional offset
    // and timezone_load_on_boot() settled on a different one (a v1 file migrated
    // to 0 under Manual Time, say), shift the RTC by the delta so the face still
    // reads the build time instead of sliding by the difference.
    if (rtc_build_seeded && clock_utc_offset != rtc_build_seed_off) {
        struct tm t;
        instance.rtc.getDateTime(&t);
        clocktime::DateTime seeded = { t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                                       t.tm_hour, t.tm_min, t.tm_sec };
        // Read the seed back as the local time it was meant to show, then store
        // it again against the offset that was actually restored.
        const clocktime::DateTime shown = clocktime::utc_to_local(seeded, rtc_build_seed_off);
        const clocktime::DateTime utc   = clocktime::local_to_utc(shown, clock_utc_offset);
        instance.rtc.setDateTime(utc.year, utc.mon, utc.day, utc.hour, utc.min, utc.sec);
        instance.rtc.hwClockRead();
        rtc_build_seed_off = clock_utc_offset;
        clock_screen_refresh();
    }

    // One line of ground truth on the console. The RTC and the offset are a
    // pair, and "the clock is N hours off" is unanswerable without seeing both
    // halves plus the Manual Time flag - which is what made the September 2026
    // report take a code audit instead of a measurement.
    {
        struct tm rtcnow;
        instance.rtc.getDateTime(&rtcnow);
        Serial.printf("[clock] rtc(utc)=%04d-%02d-%02d %02d:%02d:%02d "
                      "utc_off=%+d manual=%d seeded=%d\n",
                      rtcnow.tm_year + 1900, rtcnow.tm_mon + 1, rtcnow.tm_mday,
                      rtcnow.tm_hour, rtcnow.tm_min, rtcnow.tm_sec,
                      clock_utc_offset, manual_time_override ? 1 : 0,
                      rtc_build_seeded ? 1 : 0);
    }

    timezone_init();

    // Decode the wallpaper NOW, while internal SRAM is still fully free, BEFORE
    // any boot radio takes its ~45 KB (WiFi) share. settings_screen_load() above
    // enabled the wallpaper; lv_refr_now() renders the clock screen, which forces
    // the PNG decode and caches the result in PSRAM. A later WiFi bring-up then
    // cannot collide with the decode. Without this ordering, wallpaper + WiFi-at-
    // boot together OOM the decode and boot-loop the watch.
    lv_refr_now(NULL);

    // Now bring up the user's "Enable at boot" radios (Settings > Enable at boot).
    // Everything is OFF at boot by default; a radio powers on here ONLY if opted in
    // via boot_prefs (/Settings/boot_radios.txt). WiFi and BLE are mutually
    // exclusive in the chooser, so at most one of the two is ever set. If BLE-at-
    // boot boot-loops, recover by setting ble=0 in boot_radios.txt (card reader).
    if (boot_prefs_get(BOOT_RADIO_GPS))  gps_screen_restore_power();
    if (boot_prefs_get(BOOT_RADIO_WIFI)) wifi_radio_screen_restore_power();
    if (boot_prefs_get(BOOT_RADIO_BLE))  bluetooth_screen_restore_power();
    if (boot_prefs_get(BOOT_RADIO_LORA)) lora_screen_restore_power();
    // Repaint the radio indicators now that the boot radios are up (the earlier
    // update_*_indicator() calls ran before this and would show them off).
    update_lora_indicator();
    update_bt_indicator();
    update_wifi_indicator();

    // Re-apply persisted phone-notification state (Daily-wear). Done LAST, after
    // the boot radios, so it correctly no-ops if WiFi-at-boot or a BLE scanner is
    // already holding the radio (and keeps the preference for next time).
    device_mode_restore_boot();
    // First-boot-after-flash nudge: if Notify has never been configured, tell the
    // user how to make the watch pairable (see maybe_show_pairing_hint). No-op on
    // a normal reboot, where the saved state is already being restored above.
    maybe_show_pairing_hint();
    ans::set_find_handler(find_handler);   // phone -> watch "find" ring

    // Re-start the detectors the user left on (Tools tiles / Dot face badges).
    // Deferred ~10 s and crash-guarded inside detector_toggle; after the boot
    // radios and notifications, so a detector whose radio is taken just stays
    // off this boot and keeps its saved choice.
    detector_restore_on_boot();

    // Restore the cached health snapshot + the fixed step goal (shown as stale
    // until the phone relay refreshes them).
    health_boot_restore();
    haptic_boot_restore();   // apply saved (or default ~50%) vibration intensity
    dot_tiles_boot_restore();   // restore the two Dot-face data slot choices
    face_watch_boot_restore();  // restore Dot-face fonts / accent / date order
    clock_screen_apply_face_custom();   // re-apply the restored look to the built face
    power_boot_config();     // PMU: VINDPM anti-brownout, input cap, deep-discharge
                             // guard, and the saved charge target (full / long-life)
    coex_log_heap("setup-done");
}

// ── Low-memory warning ────────────────────────────────────────────────────
// Internal RAM (not PSRAM) is the scarce resource: WiFi + BLE + the display and
// detection state all draw from it, and when it runs low a radio can fail to
// start or the display can glitch. Rather than fail silently, pop a dismissable
// HADES-red toast so the user knows to shed load (turn off wallpaper / matrix /
// a radio). Created LAZILY on the LVGL top layer only when memory is low - never
// at setup - so it cannot affect boot. Rate-limited so it never spams.
#define LOW_MEM_WARN_BYTES   18432u   // ~18 KB internal free (conservative: only
                                      // fire when genuinely critical, to avoid
                                      // false nags; tune once normal free is known)
#define LOW_MEM_WARN_GAP_MS  60000u   // at most once a minute

// The warning is a MODAL dialog the user must acknowledge (OK button), not a
// timed toast: the earlier auto-dismiss toast flashed for a fraction of a second
// during a radio-toggle failure and could not be read. One dialog at a time -
// s_low_mem_dialog is the live instance (nullptr when none is open).
static lv_obj_t *s_low_mem_dialog = nullptr;

void low_mem_show_dialog(const char *msg);   // defined below; called by the 1 Hz check

static void low_mem_dialog_ok(lv_event_t *e)
{
    (void)e;
    if (s_low_mem_dialog) {
        lv_obj_del(s_low_mem_dialog);   // deletes the modal + its children
        s_low_mem_dialog = nullptr;
    }
}

static void low_mem_check()
{
    static uint32_t s_last_warn_ms = 0;
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    if (free_internal >= LOW_MEM_WARN_BYTES) return;

    // Never stack a second dialog on top of an unacknowledged one.
    if (s_low_mem_dialog) return;

    uint32_t now = millis();
    if (s_last_warn_ms != 0 && (now - s_last_warn_ms) < LOW_MEM_WARN_GAP_MS) return;
    s_last_warn_ms = now;

    low_mem_show_dialog(
        "#ff5555 LOW MEMORY#\n\n"
        "Not enough free memory to\n"
        "start another radio.\n\n"
        "Turn off Bluetooth, Wallpaper,\n"
        "or Matrix, then try again.");
}

// Build the modal low-memory dialog: a full-screen dim scrim on the top layer so
// nothing behind it is interactive, a HADES-red-bordered card, the message, and
// an OK button that tears the whole modal down. Stays up until the user taps OK.
// Placed on lv_layer_top so it floats above every screen. Safe to call from the
// WiFi/BLE toggle-failure path as well as the 1 Hz background check.
void low_mem_show_dialog(const char *msg)
{
    if (s_low_mem_dialog) return;   // one at a time

    // Modal scrim: covers the whole display, eats touches, dims the background.
    lv_obj_t *scrim = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(scrim);
    lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scrim, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_60, LV_PART_MAIN);
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);   // swallow taps behind the card
    s_low_mem_dialog = scrim;

    lv_obj_t *card = lv_obj_create(scrim);
    lv_obj_set_size(card, 360, 260);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_make(0x18, 0x0A, 0x0A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, HADES_RED, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, msg);
    lv_label_set_recolor(lbl, true);
    lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_t *btn = lv_button_create(card);
    lv_obj_set_size(btn, 160, 60);                  // large, finger-friendly target
    lv_obj_set_ext_click_area(btn, 24);             // extra forgiving hit area
    lv_obj_set_style_bg_color(btn, HADES_RED, LV_PART_MAIN);
    lv_obj_set_style_margin_top(btn, 16, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, low_mem_dialog_ok, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "OK");
    lv_obj_set_style_text_font(btn_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(btn_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(btn_lbl);
}

#ifdef SCREENSHOT_AUTO
// Screen entry points used by the auto-capture (redundant forward declarations
// are harmless if a header already provides them).
void threat_radar_screen_show();
void pet_screen_show();
void alarm_screen_show();
void stopwatch_screen_show();
void timer_screen_show();
void calendar_screen_show();
void meshtastic_screen_show();
void configuration_screen_show();

// Recursively find the descendant with the largest vertical scroll range - the
// scroll container may be nested (screen > wrapper > list), not a direct child.
static lv_obj_t *s_best_sc;
static int32_t   s_best_range;
static void scan_scrollable(lv_obj_t *obj)
{
    uint32_t cnt = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *c = lv_obj_get_child(obj, i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_SCROLLABLE)) {
            int32_t range = lv_obj_get_scroll_top(c) + lv_obj_get_scroll_bottom(c);
            if (range > s_best_range) { s_best_range = range; s_best_sc = c; }
        }
        scan_scrollable(c);
    }
}

// Capture the active screen, scrolling its main container so long screens (Tools
// grid, Settings list) are captured in full across several frames named
// <name>-a, <name>-b, ... A screen that fits on one viewport gets a single
// <name>.bmp.
static void capture_screen_scrolled(const char *name)
{
    lv_obj_t *scr = lv_screen_active();

    s_best_sc = NULL;
    s_best_range = 0;
    // The screen ITSELF may be the scroll container (items are direct children,
    // as on Settings), so measure it too - not just its descendants.
    if (lv_obj_has_flag(scr, LV_OBJ_FLAG_SCROLLABLE)) {
        s_best_range = lv_obj_get_scroll_top(scr) + lv_obj_get_scroll_bottom(scr);
        if (s_best_range > 0) s_best_sc = scr;
    }
    scan_scrollable(scr);
    lv_obj_t *sc = s_best_sc;
    int32_t best = s_best_range;
    Serial.printf("[shots]   %s scroll range = %d\n", name, (int)best);

    if (!sc || best <= 8) {                 // fits on one screen
        screenshot_capture_named(name);
        return;
    }

    lv_obj_scroll_to_y(sc, 0, LV_ANIM_OFF); // start at the top
    char fname[48];
    for (int frame = 0; frame < 8; frame++) {
        lv_refr_now(NULL);
        delay(250);
        snprintf(fname, sizeof(fname), "%s-%c", name, (char)('a' + frame));
        bool ok = screenshot_capture_named(fname);
        Serial.printf("[shots]   %s -> %s\n", fname, ok ? "OK" : "FAIL");
        if (lv_obj_get_scroll_bottom(sc) <= 0) break;   // reached the end
        int32_t step = lv_obj_get_height(sc) - 40;      // ~one viewport, slight overlap
        lv_obj_scroll_by(sc, 0, -step, LV_ANIM_OFF);
    }
}

// Dev-only (the `screenshots` build env): walk a curated set of side-effect-free
// screens, render each, and snapshot it to /Screenshots/<name>.bmp on the SD so
// the README can be illustrated from real UI. Requires a microSD card inserted.
// Radio/data screens (WiFi/BT/GPS/NFC/map/wardriver) are intentionally omitted -
// showing them starts hardware; capture those by hand with the long-press tool.
static void screenshot_auto_run()
{
    struct Item { const char *name; void (*show)(); };
    static const Item items[] = {
        { "01-clock",         clock_screen_show         },
        { "02-tools",         tools_screen_show         },
        { "03-radar",         threat_radar_screen_show  },
        { "04-notify",        notifications_screen_show },
        { "05-hexhound",      pet_screen_show           },
        { "06-settings",      settings_screen_show      },
        { "07-meshtastic",    meshtastic_screen_show    },
        { "08-configuration", configuration_screen_show },
        { "09-alarm",         alarm_screen_show         },
        { "10-stopwatch",     stopwatch_screen_show     },
        { "11-timer",         timer_screen_show         },
        { "12-calendar",      calendar_screen_show      },
    };
    Serial.println("[shots] auto-capture starting (need microSD inserted)");
    for (auto &it : items) {
        it.show();
        lv_refr_now(NULL);
        delay(500);            // let the screen's own timers/animations settle
        lv_refr_now(NULL);
        Serial.printf("[shots] %s\n", it.name);
        capture_screen_scrolled(it.name);   // multi-frame for scrollable screens
        delay(150);
    }
    clock_screen_show();
    lv_refr_now(NULL);
    Serial.println("[shots] auto-capture DONE - files in /Screenshots on the SD");
}
#endif

// The BOOT-button SHORT-press "back" chain: step to the previous screen for
// whatever is active (and open Settings from the clock, which the user likes).
// Factored out of loop() so the short-press path calls it while the new
// long-press path jumps home. Behaviour here is UNCHANGED from before.
static void do_boot_back_action()
{
    if (settings_screen_is_active()) {
        clock_screen_show();
    } else if (nfc_write_screen_is_active()) {
        nfc_screen_show();
    } else if (nfc_screen_is_active()) {
        wifi_radio_screen_show();
    } else if (wifi_radio_screen_is_active()) {
        bluetooth_screen_show();      // BT before WiFi (see forward chain)
    } else if (bluetooth_screen_is_active()) {
        lora_screen_show();
    } else if (lora_screen_is_active()) {
        gps_screen_show();
    } else if (configuration_screen_is_active()) {
        // BOOT mirrors swipe-LEFT: commit edits then step back through the chain
        // Config -> Map (if tiles present) -> Send Message.
        configuration_screen_commit();
        if (map_screen_available()) map_screen_show();
        else                        send_message_screen_show();
    } else if (map_screen_is_active()) {
        send_message_screen_show();
    } else if (send_message_screen_is_active()) {
        nodes_screen_show();
    } else if (nodes_screen_is_active()) {
        meshtastic_screen_show();
    } else if (meshtastic_screen_is_active()) {
        clock_screen_show();
    } else if (loot_screen_is_active()) {
        tools_screen_show();          // back to the Offense grid
    } else if (deauth_screen_is_active()) {
        tools_screen_show();          // back to the Defense grid
    } else if (tracker_timeline_screen_is_active()) {
        tools_screen_show();          // back to the Defense grid
    } else if (beacon_spam_screen_is_active()) {
        tools_screen_show();          // back to the Offense grid (tool keeps running)
    } else if (deauth_attack_screen_is_active()) {
        tools_screen_show();          // back to the Offense grid (tool keeps running)
    } else if (rogue_ap_screen_is_active()) {
        tools_screen_show();          // back to the Offense grid (AP keeps running)
    } else if (probe_sniffer_screen_is_active()) {
        tools_screen_show();          // back to the Offense grid (sniffer keeps running)
    } else if (spycam_screen_is_active()) {
        tools_screen_show();          // back to the Defense grid
    } else if (nfc_field_screen_is_active()) {
        tools_screen_show();          // its own tick powers NFC back down on exit
    } else if (pin_pad_screen_is_active()) {
        clock_screen_show();          // cancel the unlock
    } else if (analyze_screen_is_active()
            || bt_analyze_screen_is_active()
            || lora_analyze_screen_is_active()) {
        // Each *_stop() is a no-op when its analyzer isn't running, so calling all
        // three keeps the exit path the same regardless of which one is active.
        analyze_screen_stop();
        bt_analyze_screen_stop();
        lora_analyze_screen_stop();
        if (argus_mode_current() != ArgusMode::Daily) tools_screen_show();
        else clock_screen_show();     // Daily gates Tools; fall back to the clock
    } else if (lv_screen_active() == clock_screen) {
        settings_screen_show();       // from the clock, BOOT opens Settings
    } else {
        clock_screen_show();
    }
}

// ---- Offense unlock "knock": a Long-Short-Long on the BOOT button -------------
//
// The offensive tools have no visible entry point. The only way in is a private
// gesture on the BOOT button (GPIO0): LONG, SHORT, LONG, each beat within
// KNOCK_GAP_MS of the last. A match opens the PIN pad; a correct PIN then enters
// Offense (-> enter_offense()).
//
// KEY LESSON (this failed 3x before): a silent multi-beat timing gesture is
// unperformable - the user cannot tell whether a beat registered, so they cannot
// find the rhythm. So every ACCEPTED beat now gives a short haptic buzz (feel the
// L, S, L land), and a completed knock gives a double buzz. Timing is generous.
//
// Because a knock starts with a LONG, the lone-LONG "home" can't fire on release
// (it might be beat one): a LONG is buffered and its "home" is deferred by the
// gap (boot_knock_poll fires it if no SHORT follows). A SHORT that isn't
// continuing a knock fires its normal "back" immediately.
enum BootPress { BP_SHORT, BP_LONG };
static const uint32_t KNOCK_GAP_MS = 1500;  // generous window to START the next beat
static BootPress s_knock_seq[3];
static int       s_knock_len             = 0;
static uint32_t  s_knock_last_release_ms = 0;

// Short haptic tick so the user can FEEL each accepted knock beat.
static void knock_buzz(int n)
{
    for (int i = 0; i < n; i++) { instance.vibrator(); delay(60); }
}

static void boot_run_action(BootPress p)
{
    if (p == BP_LONG) clock_screen_show();     // home
    else              do_boot_back_action();   // back / Settings-from-clock
}

static void boot_flush_knock()
{
    for (int i = 0; i < s_knock_len; i++) boot_run_action(s_knock_seq[i]);
    s_knock_len = 0;
}

// Fire a deferred beat once its gap goes quiet (measured from the last RELEASE).
static void boot_knock_poll(uint32_t now)
{
    if (s_knock_len > 0 && (now - s_knock_last_release_ms) > KNOCK_GAP_MS)
        boot_flush_knock();
}

// Feed one completed press (already classified). The inter-beat gap is measured
// from the previous beat's RELEASE to THIS beat's PRESS-DOWN (a long beat holds
// ~600ms, so release-to-release made L-S-L impossible). Every accepted knock beat
// buzzes so the gesture is actually performable.
static void boot_knock_feed(BootPress p, uint32_t press_down_ms, uint32_t release_ms)
{
    if (s_knock_len > 0 && (press_down_ms - s_knock_last_release_ms) > KNOCK_GAP_MS)
        boot_flush_knock();

    s_knock_last_release_ms = release_ms;

    if (s_knock_len == 0) {                     // fresh buffer
        if (p == BP_SHORT) { boot_run_action(BP_SHORT); return; }  // knock never starts short
        s_knock_seq[s_knock_len++] = BP_LONG;   // buffer the opening LONG (defer home)
        knock_buzz(1);                          // beat 1 (L) registered
        return;
    }

    if (s_knock_len == 1) {                     // have [L]
        if (p == BP_SHORT) { s_knock_seq[s_knock_len++] = BP_SHORT; knock_buzz(1); return; }  // beat 2 (S)
        boot_run_action(BP_LONG);               // [L,L]: first L was just "home"...
        s_knock_seq[0] = BP_LONG; s_knock_len = 1;  // ...restart the knock at this L
        knock_buzz(1);
        return;
    }

    // Buffer holds [L,S].
    if (p == BP_LONG) {                         // [L,S,L] -> KNOCK
        s_knock_len = 0;
        knock_buzz(2);                          // completed: double buzz
        pin_pad_screen_show();
        return;
    }
    boot_flush_knock();                         // [L,S,S]: not a knock; run [L,S]...
    boot_run_action(BP_SHORT);                  // ...then this trailing SHORT now
}

// Dispatch one classified BOOT press. The Offense knock (L-S-L) is armed ONLY on
// the clock screen - its natural, private entry point. Everywhere else a press
// runs its ordinary back/home action IMMEDIATELY, so an accidental long-ish tap
// can't buffer a stray [L] that then eats the next press (the "BOOT didn't go
// back from the Time screen / had to press twice" bug). down_ms/up_ms carry the
// real press timestamps for knock-gap timing (equal for a synthesized tap).
static void boot_dispatch(BootPress p, uint32_t down_ms, uint32_t up_ms)
{
    if (clock_screen_display_is_off() || s_is_dimmed) {   // dimmed/off: a press only wakes
        dim_reset_activity();
        return;
    }
    dim_reset_activity();
    main_loop_request_lvgl_priority(20);
    bool armed = (argus_mode_current() != ArgusMode::Offense)
              && (s_low_mem_dialog == nullptr)
              && (lv_screen_active() == clock_screen);
    if (armed) {
        boot_knock_feed(p, down_ms, up_ms);     // buzzes each accepted beat
    } else {
        if (s_knock_len > 0) boot_flush_knock();
        if (clock_vibrate) instance.vibrator();
        boot_run_action(p);
    }
}

// Global navigation convention for app screens: swipe UP -> the Apps menu,
// swipe LEFT -> the home clock. Attached once to each screen the first time it
// becomes active (below), so every app obeys it without editing each screen's
// own gesture handler. The clock and the Apps menu are skipped - they run their
// own gesture scheme.
static void nav_gesture_cb(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_event_get_indev(e));
    if (dir == LV_DIR_TOP)       apps_screen_show();
    else if (dir == LV_DIR_LEFT) clock_screen_show();
}

static lv_obj_t *s_nav_attached[80];
static int       s_nav_attached_n = 0;

static void nav_attach_active_screen()
{
    lv_obj_t *scr = lv_screen_active();
    if (!scr || scr == clock_screen) return;   // clock has its own gestures
    if (apps_screen_is_active())      return;   // Apps menu has its own gestures
    for (int i = 0; i < s_nav_attached_n; i++)
        if (s_nav_attached[i] == scr) return;   // already wired
    if (s_nav_attached_n < (int)(sizeof(s_nav_attached) / sizeof(s_nav_attached[0]))) {
        lv_obj_add_event_cb(scr, nav_gesture_cb, LV_EVENT_GESTURE, NULL);
        s_nav_attached[s_nav_attached_n++] = scr;
    }
}

void loop()
{
    instance.loop(); // required for power button and PMU event dispatch
    find_pump();      // act on a pending phone->watch "find" request (LVGL thread)
    nav_attach_active_screen();   // ensure the active app screen obeys UP/LEFT nav

#ifdef SCREENSHOT_AUTO
    // Fire once, ~6 s after boot, so the UI and SD mount have settled.
    static bool s_shots_done = false;
    if (!s_shots_done && millis() > 6000) {
        s_shots_done = true;
        screenshot_auto_run();
    }
#endif

    // NOTE: auto-bringing the BLE controller up here (ble_scan_boot_keepalive at
    // ~4 s) boot-looped/froze the watch - the BLE bring-up is unstable regardless
    // of timing on this setup. Reverted. The Bluetooth toggle stays on its
    // on-demand bring-up (works when BT is toggled BEFORE WiFi is up); a robust
    // proper fix needs an async/off-thread bring-up, tracked separately.

    motion_wake_poll();   // accel-driven wake; no-op when toggle is off
    timezone_bg_tick();   // apply background WiFi NTP/geolocation results
    // Cheap on every iteration (an indev_state read + a millis() compare);
    // only crosses into the heavy capture+SD-write path on the 3 s edge.
    screenshot_poll();

    // BOOT button (GPIO0, INPUT_PULLUP -> pressed = LOW). Polled duration state
    // machine: SHORT press = back / Settings-from-clock (do_boot_back_action);
    // LONG press (>= BOOT_LONG_MS held) = jump HOME to the clock from anywhere.
    // The Offense knock (L-S-L) layers onto these edges via boot_knock_feed and
    // buzzes each accepted beat so it is actually performable. The FALLING ISR flag
    // is not used for actions (we poll for press DURATION), but attachInterrupt is
    // kept as a possible wake source; clear the flag so it can't linger.
    // Read the FALLING-edge ISR latch BEFORE clearing it: it catches a press the
    // duration-poll below misses when a background tick stalls the loop long enough
    // that the button is pressed AND released between two digitalRead() samples.
    bool boot_isr = back_btn_pressed;
    back_btn_pressed = false;
    static const uint32_t BOOT_LONG_MS     = 600;   // >= this held = long press
    static const uint32_t BOOT_DEBOUNCE_MS = 40;    // shorter = contact bounce
    static const uint32_t BOOT_COOLDOWN_MS = 150;   // swallow release bounce
    static bool     s_boot_down     = false;
    static uint32_t s_boot_down_ms  = 0;
    static uint32_t s_boot_cooldown = 0;
    bool     boot_down = (digitalRead(0) == LOW);
    uint32_t boot_ms   = millis();
    if (boot_down && !s_boot_down && (boot_ms - s_boot_cooldown) > BOOT_COOLDOWN_MS) {
        s_boot_down    = true;
        s_boot_down_ms = boot_ms;
    } else if (!boot_down && s_boot_down) {
        s_boot_down     = false;
        s_boot_cooldown = boot_ms;
        uint32_t held   = boot_ms - s_boot_down_ms;
        if (held >= BOOT_DEBOUNCE_MS)                          // else: bounce, ignore
            boot_dispatch((held >= BOOT_LONG_MS) ? BP_LONG : BP_SHORT, s_boot_down_ms, boot_ms);
    } else if (boot_isr && !boot_down && !s_boot_down && (boot_ms - s_boot_cooldown) > BOOT_COOLDOWN_MS) {
        // ISR latched a falling edge the poll never registered (button already back
        // HIGH, no press in flight) -> a fast tap was dropped to loop latency.
        // Recover it as a SHORT press so quick taps aren't silently lost.
        s_boot_cooldown = boot_ms;
        boot_dispatch(BP_SHORT, boot_ms, boot_ms);
    }
    boot_knock_poll(boot_ms);   // fire a deferred beat once its gap goes quiet

    // Feed NMEA bytes to TinyGPSPlus AND the GSV accumulator while the GPS radio
    // is on. gps_screen_pump() replaces instance.gps.loop() so the byte stream
    // can be teed; it is still exactly one drain of the port per iteration.
    if (gps_screen_is_powered()) {
        gps_screen_pump();
    }

    // When a screen transition was just requested, skip all the heavy
    // bg_ticks for a few iterations. With wardriver running, a single
    // loop iteration can take hundreds of ms (SD writes per detector
    // hit, drain_queue, etc) - enough that the LVGL refresh can't keep
    // up with the screen change and the user sees the old screen
    // "frozen" until the next loop iteration finally renders the new
    // one. This window gives LVGL ~12 fast iterations to fully redraw.
    // Queues continue to fill in the callbacks; we just defer the
    // draining for a few ms while the UI catches up.
    // While a modal dialog is open, give LVGL full cadence so its OK button
    // responds instantly. Otherwise the per-iteration background work (bg ticks,
    // 1 Hz detector flushes) runs between lv_task_handler() calls and the touch
    // feels laggy / hard to press, especially on a radio screen that never
    // requested priority itself.
    bool lvgl_priority = s_lvgl_priority_cycles > 0 || s_low_mem_dialog != nullptr;
    {
        // Pause the matrix-rain animation while LVGL is trying to complete
        // a screen transition. Without this, the 120 ms rain timer keeps
        // invalidating 22 recolored labels, forcing a full clock-screen
        // re-render every refresh tick.
        static bool s_matrix_paused_by_us = false;
        if (lvgl_priority && !s_matrix_paused_by_us) {
            matrix_bg_set_paused(true);
            s_matrix_paused_by_us = true;
        } else if (!lvgl_priority && s_matrix_paused_by_us) {
            matrix_bg_set_paused(false);
            s_matrix_paused_by_us = false;
        }
    }
    if (lvgl_priority) s_lvgl_priority_cycles--;


    if (!lvgl_priority) {
        meshtastic_bg_tick();
        // While the SD card is mounted over USB, the host owns the filesystem —
        // suspend the background loggers so the two sides never write at once.
        if (!usb_sd_is_running()) {
            airtag_bg_tick();
            flipper_bg_tick();
            skimmer_bg_tick();
            evil_twin_bg_tick();
            flock_bg_tick();
            human_detector_bg_tick();
            threatradar_bg_tick();  // correlate detector hits into follow-scores
            // Bridge: a Likely+ tail flips the HexHound to its wary/HADES-red mood,
            // matching the status-bar/radar HADES flip. (Team-decoupled hook.)
            hexhound_set_threat_level(threatradar_top_level() >= TR_LVL_LIKELY ? 1 : 0,
                                      HEX_THREAT_RADAR);
            handshake_bg_tick();    // drain captured EAPOL frames to /pwn/*.pcap
        }
    }
    // Yield to LVGL between SD-heavy batches. The display uses partial
    // refresh with ~6 tiles per screen, one tile per lv_task_handler call;
    // without these extra passes, a single loop iteration with several
    // detector SD writes can stall screen transitions visibly (new screen
    // draws tile-by-tile on top of the old one for hundreds of ms).
    lv_task_handler();
    if (!lvgl_priority) {
        tpms_bg_tick();
        pager_bg_tick();
        aprs_bg_tick();   // RX drain + queued TX; SD logging self-gates on USB SD
        pingsweep_poll(); // writes /PingSweeps/ once a sweep finishes
        portscan_poll();  // writes /PingSweeps/portscan_* once a scan finishes
        nfc_screen_worker();
        nfc_write_screen_worker();
    }
    lv_task_handler();

    // Any touchscreen press resets the dim timer; rising edge also triggers vibration
    {
        static lv_indev_state_t prev_touch = LV_INDEV_STATE_RELEASED;
        lv_indev_t *indev = lv_indev_get_next(NULL);
        while (indev) {
            if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
                lv_indev_state_t state = lv_indev_get_state(indev);
                if (state == LV_INDEV_STATE_PRESSED) {
                    // While dimmed-but-on, the swipe-to-wake gate owns the touch:
                    // a stray tap must not wake or click through. The gate wakes
                    // on a swipe up (and buttons wake directly). The fully-off
                    // saver still wakes on any tap, so it is not gated here.
                    if (s_is_dimmed && !s_display_off) {
                        // gated - do nothing; on_dim_gate_* handles hint + wake
                    } else {
                        dim_reset_activity();
                        // Keep LVGL on its fast cadence while the user is interacting
                        // so the next taps (buttons, Exit Offense, etc.) aren't starved
                        // by the heavy per-iteration background work.
                        main_loop_request_lvgl_priority(20);
                        if (clock_vibrate && prev_touch == LV_INDEV_STATE_RELEASED)
                            instance.vibrator();
                    }
                }
                prev_touch = state;
                break;
            }
            indev = lv_indev_get_next(indev);
        }
    }

    // Auto-return to the home clock after inactivity, INDEPENDENT of the dim
    // timer (fixed 1 minute). Keep-awake screens (compass / presence radar) and
    // any modal/overlay are exempt so nothing is torn down under the user.
    {
        const uint32_t AUTO_HOME_MS = 60000;
        if (!s_keep_awake && !notify_popup_is_showing() &&
            lv_screen_active() != clock_screen &&
            !pin_pad_screen_is_active() && !notifications_screen_is_active() &&
            s_low_mem_dialog == nullptr && !alarm_is_ringing() &&
            millis() - s_last_activity_ms >= AUTO_HOME_MS) {
            clock_screen_show();
        }
    }

    // Dim timer: check every loop iteration for low latency
    // Never dim under a notification banner: it is boosted on purpose so the
    // message is readable, and dims back on its own once it is dismissed.
    // Also hold off dimming briefly after the charger is plugged in, so the
    // charge indicator is visible for at least CHARGE_WAKE_MS.
    if (s_dim_timeout_ms > 0 && !s_is_dimmed && !notify_popup_is_showing() &&
        !s_keep_awake && millis() >= s_charge_wake_until_ms) {
        if (millis() - s_last_activity_ms >= s_dim_timeout_ms) {
            s_is_dimmed    = true;
            s_dimmed_at_ms = millis();
            instance.setBrightness(s_dim_brightness);
            // Home-return is no longer coupled to dimming - it is handled by the
            // independent 1-minute auto-home timer above (AUTO_HOME_MS).
            // Raise the swipe-to-wake gate so a stray touch cannot wake it.
            // It starts unarmed: for the first 15 s a tap still wakes.
            show_dim_gate();
        }
    }

    // Arm swipe-up-to-wake 15 s after dimming (not at the same instant): until
    // then a tap wakes; once armed, a tap only reveals the frosted hint and a
    // deliberate swipe up is required.
    if (s_is_dimmed && !s_display_off && s_dim_gate && !s_dim_gate_armed &&
        millis() - s_dimmed_at_ms >= DIM_GATE_ARM_MS) {
        s_dim_gate_armed = true;
    }

    // 1Hz block also skipped during LVGL priority window - it contains
    // I2C-heavy status updates plus wardriver_bg_tick (drain_queue +
    // periodic flush_to_sd, the latter iterating all 32768 ap_table
    // buckets which can block for seconds with a populated session).
    // Missing one 1Hz tick during a screen transition is invisible.
    if (millis() - last_update_ms >= 1000 && !lvgl_priority) {
        last_update_ms = millis();
        update_clock();
        argus_mode_indicator_refresh();   // Offense border flips to threat-red live
        alarm_tick();              // fires the alarm at the set time
        charge_state_tick();       // feed bat_charge on every face + wake on plug-in
        // The classic analog/digital face's status icons and battery widget are
        // only on screen when that face is showing on the (awake) clock screen.
        // Under the Dot face they sit hidden beneath dot_container, and on any
        // other screen or with the panel off they aren't drawn at all — so
        // restyling them every second is pure waste. dot_face_tick() paints the
        // Dot's own equivalents; the classic ones repaint on return via
        // screen_return_to()/the face switch.
        bool classic_face_visible = (lv_screen_active() == clock_screen
                                     && clock_face != FACE_DOT && !s_display_off);
        if (classic_face_visible) {
            layout_battery_indicators(); // pack alarm/stopwatch/timer icons R→L
            update_battery();
            update_lora_indicator();
            update_bt_indicator();
            update_wifi_indicator();
            update_sd_indicator();
            update_nfc_indicator();
            update_scan_indicators();
        }
        if (!usb_sd_is_running())   // host owns the SD card while mounted
            wardriver_bg_tick();
        if (classic_face_visible)
            update_wardriver_indicator();
        dot_face_tick();   // refresh the Dot face's own status row when active
        display_power_tick();   // auto brightness from the sun + battery saver
        health_tick_1hz();      // drain BLE health packets + 2-min HR compile
        if (wardriver_screen_is_active())
            wardriver_screen_update();
        if (health_screen_is_active())
            health_screen_update();
        if (configuration_screen_is_active())
            configuration_screen_update();
        low_mem_check();   // warn (once/min) if internal RAM is running low
        // Fold the live WiFi beacon stream through the pure evil-twin +
        // beacon-flood detectors -> ThreatState -> forensic log + HADES accent /
        // HexHound. millis()/1000 is a monotonic seconds base (never rewinds),
        // which the detectors' sliding windows + decay require.
#if ARGUS_WIFI_THREAT_PIPELINE
        detect_pipeline_tick(millis() / 1000);
#endif
#if ARGUS_BLE_THREAT_PIPELINE
        ble_detect_pipeline_tick(millis() / 1000);
#endif
    }
    // Core-clock policy in one place: run 80 MHz whenever the panel is dim or
    // off (nothing is animating that needs 240 MHz), and full clock the instant
    // it is bright again. Idempotent; the wake path also restores 240 MHz right
    // away for a snappy first frame. Placed before the panel-off early return so
    // it still applies in the saver state.
    cpu_set_low(s_display_off || s_is_dimmed);

    // Panel off (battery saver): the LVGL refresh timer is paused, so the extra
    // render passes below would do nothing. Run one cheap handler pass to keep
    // any pending timers serviced, then idle ~40 ms. The core is already at
    // 80 MHz here; the long delay lets it sit mostly asleep between the cheap
    // per-loop polls (motion wake, BOOT button, touch) which still run at ~25 Hz
    // — fast enough that a wrist-raise or tap wakes the screen without lag.
    if (s_display_off) {
        lv_task_handler();
        delay(40);
        return;
    }

    // Multiple LVGL passes per loop iteration. Each lv_task_handler call
    // renders at most one partial-refresh tile, and the watch panel needs
    // ~6 tiles for a full screen. When wardriver is dumping detector hits
    // into SD writes between iterations, having only one pass per loop
    // means a single screen transition takes 6+ loop iterations to fully
    // redraw - long enough that the user sees the old screen "freeze".
    lv_task_handler();
    delay(2);
    lv_task_handler();
    delay(2);
    uint32_t idle_ms = lv_task_handler();

    // When the UI is static - no touch down, no running animation, and the next
    // LVGL timer is not imminent - let the core idle instead of spinning the loop
    // at 240 MHz. Capped at 30 ms so motion-wake / BOOT-button / touch polling
    // stays ~30 Hz (a wrist-raise or tap still wakes without lag; the BOOT ISR
    // latch recovers any tap shorter than the poll gap). During active use idle_ms
    // is small, so this is a no-op and responsiveness is unchanged.
    if (!touch_is_down() && lv_anim_count_running() == 0 && idle_ms > 8) {
        delay(idle_ms > 30 ? 30 : idle_ms);
    }
}
