// notify_popup.cpp - see notify_popup.h.
#include "notify_popup.h"
#include "notify/notify_center.h"
#include "notify/notify_log.h"
#include "notifications_screen.h"
#include "settings_screen.h"
#include "device_mode.h"   // device_mode_platform() - iOS vs Android
#include "face_watch.h"    // face_accent_rgb() - the wearer's chosen accent
#include "night_mode.h"    // night_mode_active() - quiet hours
#include "ancs.h"          // ancs::dismiss() - decline a call on iOS
#include "theme.h"

#include <LilyGoLib.h>

// Defined in main.cpp.
void clock_screen_restore_brightness();   // back to the dim level or the active brightness
int  clock_screen_active_brightness();    // Settings level, sun-scaled when Auto brightness is on
bool clock_screen_is_dimmed_or_off();     // the states in which a banner would wake the screen
void ui_reset_dim_activity();             // wake a dimmed / switched-off screen

// Nothing-OS palette (matches the Dot watchface): white / grey / black / red.
static const lv_color_t NOTHING_WHITE = lv_color_hex(0xFFFFFF);
static const lv_color_t NOTHING_GREY  = lv_color_hex(0x9A9A9A);
static const lv_color_t NOTHING_RED   = lv_color_hex(0xE02020);   // face red

static lv_obj_t   *s_banner        = nullptr;
static lv_timer_t *s_dismiss_timer = nullptr;
static bool        s_boosted       = false;   // brightness raised for the banner
static uint32_t    s_call_uid      = 0;        // uid of the call the banner is showing

static constexpr uint32_t POPUP_MS = 6000;   // auto-dismiss after 6s
// While a banner is up the panel runs 15% (of full scale) above the active
// brightness, so the message is readable at a glance; an arrival also wakes a
// dimmed or switched-off screen, and the boost drops back as the banner goes.
static constexpr int BOOST = DEVICE_MAX_BRIGHTNESS_LEVEL * 15 / 100;

static void boost_brightness()
{
    if (s_boosted) return;   // a newer arrival replacing a banner: already up
    int level = clock_screen_active_brightness() + BOOST;
    if (level > DEVICE_MAX_BRIGHTNESS_LEVEL) level = DEVICE_MAX_BRIGHTNESS_LEVEL;
    instance.setBrightness((uint8_t)level);
    s_boosted = true;
}

static void delete_banner()
{
    if (s_dismiss_timer) { lv_timer_del(s_dismiss_timer); s_dismiss_timer = nullptr; }
    if (s_banner)        { lv_obj_del(s_banner);          s_banner        = nullptr; }
}

// The banner is gone for good (timeout or tap): drop the brightness boost and
// repaint the WHOLE screen underneath. With the panel's partial refresh, only
// the banner's own rect used to be redrawn, which left its shadow and edges
// smeared over the face until another full redraw (e.g. going back home).
static void dismiss_banner(bool repaint)
{
    delete_banner();
    if (s_boosted) {
        clock_screen_restore_brightness();
        s_boosted = false;
    }
    if (repaint) {
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(NULL);
    }
}

static void on_dismiss_timer(lv_timer_t *) { dismiss_banner(true); }

static void on_banner_click(lv_event_t *)
{
    dismiss_banner(false);          // the screen load below repaints everything
    notifications_screen_show();   // tap the banner -> open the full list
}

// Mute an incoming call: silence the watch's buzz for this call (see
// notify::mute_call). Handy when the call is answered on another device (Teams
// on a PC) and the phone keeps re-ringing the watch.
static void on_mute_click(lv_event_t *)
{
    notify::mute_call();
    dismiss_banner(true);
}

// Hang up an incoming call. On iOS we can perform the ANCS "negative" action,
// which declines the call on the phone. Android (Gadgetbridge) exposes no
// call-reject channel to the watch, so there we at least silence the ring for
// this call (same as Mute); a true phone-side reject there needs the companion
// app's telephony path.
static void on_hangup_click(lv_event_t *)
{
    if (device_mode_platform() == NotifyPlatform::iOS && ancs::is_connected())
        ancs::dismiss(s_call_uid);
    notify::mute_call();
    dismiss_banner(true);
}

bool notify_popup_is_showing() { return s_banner != nullptr; }

static void show_banner(const notify::Notification &n)
{
    // Night time: a notification must not light up a dimmed or switched-off
    // screen. It is already in the list (publish() stored it), and the home
    // screen's notification square shows it when the wearer wakes the watch.
    if (night_mode_active() && clock_screen_is_dimmed_or_off()) {
        NLOG("[popup] night time: not waking the screen for \"%s\"\n", n.title);
        return;
    }
    NLOG("[popup] show_banner: \"%s\"\n", n.title);
    delete_banner();    // one banner at a time; a newer arrival replaces the old
    ui_reset_dim_activity();   // a notification wakes a dimmed or switched-off screen
    boost_brightness();

    // Parent on the TOP layer so it floats above the clock and every screen and
    // survives screen loads. We own its lifetime (auto-dismiss / tap).
    s_banner = lv_obj_create(lv_layer_top());
    lv_obj_set_width(s_banner, 390);
    lv_obj_set_height(s_banner, LV_SIZE_CONTENT);
    // Sit below the display's top curve / status area rather than jammed against
    // the top edge, so the whole card is readable.
    lv_obj_align(s_banner, LV_ALIGN_TOP_MID, 0, 72);
    lv_obj_set_style_bg_color(s_banner, lv_color_hex(0x141414), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_COVER, LV_PART_MAIN);
    // Border follows the colour the wearer picked in Facewatch (red by default).
    lv_obj_set_style_border_color(s_banner, lv_color_hex(face_accent_rgb()), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_banner, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_banner, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_banner, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_banner, 3, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_banner, 16, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_banner, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_banner, LV_OPA_50, LV_PART_MAIN);
    lv_obj_clear_flag(s_banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(s_banner, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_banner, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_banner, on_banner_click, LV_EVENT_CLICKED, NULL);
    // Guarantee we're above every other lv_layer_top overlay (the dim/wake gate,
    // the mode frame, the find scrim): those are created at other times, and a
    // banner arriving while one is up must not render behind it.
    lv_obj_move_foreground(s_banner);

    // Glyph-filtered scratch: strip codepoints the watch font can't render so the
    // banner never shows "[]" tofu boxes for emoji (body is the largest field).
    char safe[notify::kBodyLen];

    // Header: bell + app/source name in the accent colour.
    lv_obj_t *app = lv_label_create(s_banner);
    lv_obj_set_style_text_font(app, theme_text_font(14), LV_PART_MAIN);
    lv_obj_set_style_text_color(app, NOTHING_GREY, LV_PART_MAIN);
    lv_label_set_text_fmt(app, LV_SYMBOL_BELL "  %s",
        n.app[0] ? notify_glyph_filter(n.app, theme_text_font(14), safe, sizeof safe) : "Notification");

    if (n.title[0]) {
        lv_obj_t *title = lv_label_create(s_banner);
        lv_obj_set_style_text_font(title, theme_text_font(16), LV_PART_MAIN);
        lv_obj_set_style_text_color(title, NOTHING_WHITE, LV_PART_MAIN);
        lv_obj_set_width(title, LV_PCT(100));
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);   // one-line, ellipsized
        lv_label_set_text(title, notify_glyph_filter(n.title, theme_text_font(16), safe, sizeof safe));
    }
    if (n.body[0]) {
        lv_obj_t *body = lv_label_create(s_banner);
        lv_obj_set_style_text_font(body, theme_text_font(14), LV_PART_MAIN);
        lv_obj_set_style_text_color(body, NOTHING_GREY, LV_PART_MAIN);
        lv_obj_set_width(body, LV_PCT(100));
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_label_set_text(body, notify_glyph_filter(n.body, theme_text_font(14), safe, sizeof safe));
    }

    // Incoming call: two side-by-side actions — Hang up (decline on the phone,
    // iOS; silence on Android) and Mute (stop the watch buzzing for this call).
    // Each button consumes its own tap, so neither opens the list.
    if (n.category == notify::Category::IncomingCall) {
        s_call_uid = n.uid;

        lv_obj_t *row = lv_obj_create(s_banner);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_margin_top(row, 6, LV_PART_MAIN);
        lv_obj_set_layout(row, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 8, LV_PART_MAIN);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *hang = lv_button_create(row);
        lv_obj_set_flex_grow(hang, 1);
        lv_obj_set_style_bg_color(hang, NOTHING_RED, LV_PART_MAIN);
        lv_obj_set_style_radius(hang, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(hang, 8, LV_PART_MAIN);
        lv_obj_add_event_cb(hang, on_hangup_click, LV_EVENT_CLICKED, NULL);
        lv_obj_t *hl = lv_label_create(hang);
        lv_obj_set_style_text_font(hl, theme_text_font(16), LV_PART_MAIN);
        lv_obj_set_style_text_color(hl, NOTHING_WHITE, LV_PART_MAIN);
        lv_label_set_text(hl, "Hang up");
        lv_obj_center(hl);

        lv_obj_t *mute = lv_button_create(row);
        lv_obj_set_flex_grow(mute, 1);
        lv_obj_set_style_bg_color(mute, lv_color_hex(0x2A2A2A), LV_PART_MAIN);
        lv_obj_set_style_border_color(mute, NOTHING_GREY, LV_PART_MAIN);
        lv_obj_set_style_border_width(mute, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(mute, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(mute, 8, LV_PART_MAIN);
        lv_obj_add_event_cb(mute, on_mute_click, LV_EVENT_CLICKED, NULL);
        lv_obj_t *ml = lv_label_create(mute);
        lv_obj_set_style_text_font(ml, theme_text_font(16), LV_PART_MAIN);
        lv_obj_set_style_text_color(ml, NOTHING_WHITE, LV_PART_MAIN);
        lv_label_set_text(ml, LV_SYMBOL_MUTE "  Mute");
        lv_obj_center(ml);
    }

    s_dismiss_timer = lv_timer_create(on_dismiss_timer, POPUP_MS, NULL);
    lv_timer_set_repeat_count(s_dismiss_timer, 1);   // one-shot
}

// Runs on the UI thread (lv_timer_handler). Drains a pending arrival, if any.
static void poll(lv_timer_t *)
{
    notify::Notification n;
    if (notify::take_pending(n)) show_banner(n);
}

void notify_popup_init()
{
    lv_timer_create(poll, 250, NULL);
    NLOGLN("[popup] init: poll timer created");
}
