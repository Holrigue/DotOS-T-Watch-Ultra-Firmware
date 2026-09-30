#include "pet_screen.h"
#include "hexhound.h"
#include "theme.h"
// NOTE: threat_radar.h intentionally NOT included. Threat awareness reaches the
// pet only through hexhound_set_threat_level() (see hexhound.h) so this cluster
// stays decoupled from the Threat Radar bundle. main.cpp and detect_pipeline.cpp
// are the two wired sources, each passing its own HexThreatSource.
// NOT a confirmed tail: both sources trip a full rung below TR_LVL_CONFIRMED, so
// nothing rendered here may claim a tail is confirmed.
#include "tools_screen.h"
#include "wifi_beacon_manager.h"
#include <lvgl.h>
#include <LilyGoLib.h>
#include <Arduino.h>
#include <esp_heap_caps.h>   // heap_caps_malloc / MALLOC_CAP_SPIRAM for the mascot buffer
#include <math.h>

// ── ARGUS HexHound — LVGL renderer ─────────────────────────────────────
//
// The HexHound is an 8-bit pixel-art mascot (a pwnagotchi/Tamagotchi-style face)
// drawn on-device into a small ARGB buffer — no SD card, no PNG decode. It bobs
// inside a set of sonar rings that pulse as it sweeps the airwaves; both the face
// and the rings recolour by mood (calm steel-blue / HADES-red on a threat) as the
// at-a-glance cue. (The previous build loaded per-stage HD PNGs from the SD card;
// that decode froze the menu -> HexHound transition, so it was replaced by this
// self-contained renderer.) Everything below reads engine state through the
// hexhound_*() accessors; no game logic lives here.

static lv_obj_t  *s_screen    = nullptr;
static lv_obj_t  *s_sprite    = nullptr;   // 8-bit mascot face: lv_image over an ARGB buffer
static uint32_t     *s_masc_buf = nullptr; // mascot pixels in PSRAM (ARGB8888)
static lv_image_dsc_t s_masc_dsc;          // descriptor pointing at s_masc_buf
static uint8_t    s_masc_mood = 0xFF;      // last-drawn mood (redraw only on a change)
static lv_obj_t  *s_ring[3]   = { nullptr, nullptr, nullptr };  // sonar sweep
static lv_obj_t  *s_stage_lbl = nullptr;
static lv_obj_t  *s_ability   = nullptr;
static lv_obj_t  *s_speech    = nullptr;
static lv_obj_t  *s_lvl       = nullptr;
static lv_obj_t  *s_bar_fill  = nullptr;
static lv_obj_t  *s_stats     = nullptr;
static lv_obj_t  *s_banner    = nullptr;    // transient "EVOLVED" flash

static lv_timer_t *s_timer    = nullptr;    // 1 Hz engine tick + refresh
static lv_timer_t *s_anim     = nullptr;    // ~12 Hz idle bob + ring pulse
static bool        s_active   = false;
static uint32_t    s_phase    = 0;
static uint32_t    s_banner_until = 0;

// Mood -> accent colour for the head/eyes.
static lv_color_t mood_color(uint8_t mood)
{
    switch (mood) {
        case HEX_WARY:    return HADES_RED;               // threat: red eyes
        case HEX_EXCITED: return ARGUS_ACCENT_ACTIVE;     // bright steel
        case HEX_HUNGRY:  return lv_color_make(0xC8, 0x9B, 0x5A); // amber-ish
        case HEX_SLEEPY:  return ARGUS_ACCENT_DIM;
        default:          return ARGUS_ACCENT;            // calm steel-blue
    }
}

static void pet_wifi_cb(const WifiBeacon *b)
{
    if (b) hexhound_note_wifi(b->bssid);   // feed distinct-AP XP
}

// ── 8-bit mascot (pixel face, drawn on-device) ──────────────────────────────
// A pwnagotchi/Tamagotchi-style face rendered into a small ARGB buffer and shown
// as an lv_image. Drawn entirely on-device: opening HexHound never touches the
// SD card, which is what the old HD-PNG decode did — and that decode froze the
// menu -> HexHound transition. The expression tracks the pet's mood.
static constexpr int MASC_CELL = 16;                     // one chunky "8-bit" block
static constexpr int MASC_COLS = 10, MASC_ROWS = 8;
static constexpr int MASC_W = MASC_COLS * MASC_CELL;     // 160
static constexpr int MASC_H = MASC_ROWS * MASC_CELL;     // 128

// Mood -> face colour (ARGB8888). Mirrors mood_color()'s intent but as a raw
// pixel value for the buffer: threat red, bright/amber/dim steel by mood.
static uint32_t masc_color(uint8_t mood)
{
    switch (mood) {
        case HEX_WARY:    return 0xFFE02020;   // red eyes on a threat
        case HEX_EXCITED: return 0xFF9BBCD6;   // bright steel
        case HEX_HUNGRY:  return 0xFFC89B5A;   // amber-ish
        case HEX_SLEEPY:  return 0xFF5C6B7A;   // dim steel
        default:          return 0xFF7FA8C9;   // calm steel-blue
    }
}

// Fill one grid cell (a chunky pixel) in the mascot buffer.
static void masc_block(int col, int row, uint32_t argb)
{
    for (int y = 0; y < MASC_CELL; y++)
        for (int x = 0; x < MASC_CELL; x++) {
            int px = col * MASC_CELL + x, py = row * MASC_CELL + y;
            if (px >= 0 && px < MASC_W && py >= 0 && py < MASC_H)
                s_masc_buf[py * MASC_W + px] = argb;
        }
}

// Redraw the face for a mood (transparent background + accent-coloured blocks).
// Cheap; refresh() calls it only when the mood actually changes.
static void draw_mascot(uint8_t mood)
{
    if (!s_masc_buf) return;
    memset(s_masc_buf, 0, (size_t)MASC_W * (size_t)MASC_H * 4u);   // transparent
    const uint32_t c = masc_color(mood);

    // Eyes: open 2x2 blocks, or a single closed bar when sleepy.
    if (mood == HEX_SLEEPY) {
        masc_block(2, 3, c); masc_block(3, 3, c); masc_block(6, 3, c); masc_block(7, 3, c);
    } else {
        masc_block(2, 2, c); masc_block(3, 2, c); masc_block(2, 3, c); masc_block(3, 3, c);
        masc_block(6, 2, c); masc_block(7, 2, c); masc_block(6, 3, c); masc_block(7, 3, c);
    }
    // Angry brows when wary (threat).
    if (mood == HEX_WARY) { masc_block(2, 1, c); masc_block(7, 1, c); }

    // Mouth: smile (excited), open (hungry), or flat (calm/wary).
    if (mood == HEX_EXCITED) {
        masc_block(3, 5, c); masc_block(6, 5, c); masc_block(4, 6, c); masc_block(5, 6, c);
    } else if (mood == HEX_HUNGRY) {
        masc_block(4, 5, c); masc_block(5, 5, c); masc_block(4, 6, c); masc_block(5, 6, c);
    } else {
        masc_block(3, 5, c); masc_block(4, 5, c); masc_block(5, 5, c); masc_block(6, 5, c);
    }
}

// Redraw + repush the mascot image if the mood changed. Cheap on a no-op.
static void update_sprite(uint8_t mood)
{
    if (!s_sprite || !s_masc_buf || mood == s_masc_mood) return;
    s_masc_mood = mood;
    draw_mascot(mood);
    lv_image_set_src(s_sprite, NULL);
    lv_image_set_src(s_sprite, &s_masc_dsc);
    lv_obj_invalidate(s_sprite);
}

static void refresh()
{
    const HexHoundState &st = hexhound_state();
    lv_color_t accent = mood_color(st.mood);

    // Redraw the mascot face for the current mood (no-op unless it changed).
    update_sprite(st.mood);

    // Sonar rings track mood: calm steel-blue, or HADES-red on a confirmed
    // threat. With the creature now a fixed sprite, the rings are the pet's
    // at-a-glance mood/threat cue.
    for (int i = 0; i < 3; i++)
        lv_obj_set_style_border_color(s_ring[i], accent, LV_PART_MAIN);

    lv_label_set_text(s_stage_lbl, hexhound_stage_name());
    lv_obj_set_style_text_color(s_stage_lbl, accent, LV_PART_MAIN);
    lv_label_set_text(s_ability, hexhound_ability_name());
    lv_label_set_text(s_speech, hexhound_mood_speech());

    lv_label_set_text_fmt(s_lvl, "LVL %d", hexhound_level());

    int span = hexhound_xp_stage_span();
    int into = hexhound_xp_into_stage();
    int pct  = span > 0 ? (into * 100) / span : 100;
    if (pct > 100) pct = 100;
    lv_obj_set_width(s_bar_fill, 2 + (pct * 296) / 100);
    lv_obj_set_style_bg_color(s_bar_fill, accent, LV_PART_MAIN);

    lv_label_set_text_fmt(s_stats,
        "HUN %d   ENR %d   BND %d   PWND %d\nPEERS %d      XP %ld",
        st.hunger, st.energy, st.bond, st.pwnd, st.peers, st.xp);

    // Evolution banner (one-shot, 4 s).
    if (hexhound_take_evolved_flag()) {
        lv_label_set_text_fmt(s_banner, "EVOLVED  ->  %s", hexhound_stage_name());
        lv_obj_clear_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
        s_banner_until = millis() + 4000;
    } else if (s_banner_until && millis() > s_banner_until) {
        lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
        s_banner_until = 0;
    }
}

static void on_tick(lv_timer_t *)
{
    if (!s_active) return;
    hexhound_update();
    refresh();
}

// Idle bob + sonar ring pulse. Position/opacity only — cheap at ~12 Hz.
static void on_anim(lv_timer_t *)
{
    if (!s_active) return;
    s_phase++;

    int dy = (int)(6.0f * sinf(s_phase * 0.16f));
    lv_obj_align(s_sprite, LV_ALIGN_CENTER, 0, -74 + dy);

    // Rings expand/fade in sequence to read as an outward recon sweep.
    for (int i = 0; i < 3; i++) {
        float t = fmodf(s_phase * 0.03f + i * 0.33f, 1.0f);
        lv_opa_t opa = (lv_opa_t)(LV_OPA_COVER * (1.0f - t) * 0.6f);
        lv_obj_set_style_border_opa(s_ring[i], opa, LV_PART_MAIN);
    }
}

// Clean up on ANY exit (swipe to Apps, swipe to the clock, or the auto-return
// to the clock on dim) - not just one gesture. Detaches the WiFi beacon
// consumer (a no-op if HexHound never attached, and it never stops a scan that
// another tool owns), saves the pet, stops the timers, so nothing leaks.
static void on_unload(lv_event_t *)
{
    s_active = false;
    hexhound_save();
    wifi_beacon_remove(pet_wifi_cb);
    if (s_timer) { lv_timer_del(s_timer); s_timer = nullptr; }
    if (s_anim)  { lv_timer_del(s_anim);  s_anim  = nullptr; }
}

// ── little primitive helpers ───────────────────────────────────────────────

static lv_obj_t *box(lv_obj_t *parent, int w, int h, int radius, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(o, c, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *ring(lv_obj_t *parent, int d, lv_color_t c)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(o, c, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

void pet_screen_create()
{
    hexhound_init();

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_make(0x06, 0x0B, 0x11), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screen, on_unload, LV_EVENT_SCREEN_UNLOAD_START, NULL);

    // Title.
    lv_obj_t *name = lv_label_create(s_screen);
    lv_obj_set_style_text_font(name, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(name, ARGUS_TEXT, LV_PART_MAIN);
    lv_label_set_text(name, "HEXHOUND");
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 20);

    // Sonar rings (behind the hound).
    const int rd[3] = { 250, 190, 132 };
    for (int i = 0; i < 3; i++) {
        s_ring[i] = ring(s_screen, rd[i], ARGUS_ACCENT_DIM);
        lv_obj_align(s_ring[i], LV_ALIGN_CENTER, 0, -74);
    }

    // 8-bit mascot face: an ARGB buffer in PSRAM shown as an lv_image, floating
    // over the sonar rings and bobbed directly by on_anim(). Drawn on-device by
    // update_sprite()/draw_mascot() (no SD, no PNG decode — that decode was what
    // froze the menu -> HexHound transition). If the PSRAM alloc fails the image
    // simply stays empty rather than crashing.
    s_sprite = lv_image_create(s_screen);
    if (!s_masc_buf)
        s_masc_buf = (uint32_t *)heap_caps_malloc((size_t)MASC_W * (size_t)MASC_H * 4u, MALLOC_CAP_SPIRAM);
    if (s_masc_buf) {
        memset(s_masc_buf, 0, (size_t)MASC_W * (size_t)MASC_H * 4u);
        s_masc_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
        s_masc_dsc.header.cf     = LV_COLOR_FORMAT_ARGB8888;
        s_masc_dsc.header.flags  = 0;
        s_masc_dsc.header.w      = MASC_W;
        s_masc_dsc.header.h      = MASC_H;
        s_masc_dsc.header.stride = MASC_W * 4;
        s_masc_dsc.data_size     = (uint32_t)((size_t)MASC_W * (size_t)MASC_H * 4u);
        s_masc_dsc.data          = (const uint8_t *)s_masc_buf;
        lv_image_set_src(s_sprite, &s_masc_dsc);
    }
    lv_obj_align(s_sprite, LV_ALIGN_CENTER, 0, -74);

    // Speech line.
    s_speech = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_speech, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_speech, ARGUS_ACCENT_ACTIVE, LV_PART_MAIN);
    lv_label_set_text(s_speech, "...tick... incubating...");
    lv_obj_align(s_speech, LV_ALIGN_CENTER, 0, 66);

    // Stage name + ability.
    s_stage_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_stage_lbl, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_stage_lbl, ARGUS_TEXT, LV_PART_MAIN);
    lv_label_set_text(s_stage_lbl, "Egg");
    lv_obj_align(s_stage_lbl, LV_ALIGN_CENTER, 0, 92);

    s_ability = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_ability, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ability, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_label_set_text(s_ability, "INCUBATING");
    lv_obj_align(s_ability, LV_ALIGN_CENTER, 0, 116);

    // Level + XP-into-stage bar.
    s_lvl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_lvl, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lvl, ARGUS_TEXT, LV_PART_MAIN);
    lv_label_set_text(s_lvl, "LVL 1");
    lv_obj_align(s_lvl, LV_ALIGN_CENTER, -150, 138);

    lv_obj_t *bar_bg = box(s_screen, 300, 16, 8, lv_color_make(0x10, 0x18, 0x22));
    lv_obj_set_style_border_color(bar_bg, ARGUS_ACCENT_DIM, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar_bg, 1, LV_PART_MAIN);
    lv_obj_align(bar_bg, LV_ALIGN_CENTER, 0, 138);

    s_bar_fill = box(bar_bg, 2, 14, 7, ARGUS_ACCENT);
    lv_obj_align(s_bar_fill, LV_ALIGN_LEFT_MID, 0, 0);

    // Stats line.
    s_stats = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_stats, theme_text_font(16), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_stats, ARGUS_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_stats, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(s_stats, "HUN 0   ENR 0   BND 0   PWND 0\nPEERS 0      XP 0");
    lv_obj_align(s_stats, LV_ALIGN_CENTER, 0, 176);

    // Evolution banner (hidden until an evolution fires).
    s_banner = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_banner, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_banner, ARGUS_ACCENT_ACTIVE, LV_PART_MAIN);
    lv_label_set_text(s_banner, "");
    lv_obj_align(s_banner, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    // Timers are created in pet_screen_show() and deleted in on_unload() so they
    // only run while the screen is open (they used to run from boot forever).
}

void pet_screen_show()
{
    if (!s_screen) pet_screen_create();
    hexhound_init();
    s_active = true;
    // Feed on WiFi beacons only if a scan is ALREADY running (piggyback).
    // HexHound must NEVER bring WiFi up itself: with radio coexistence enabled
    // the BLE keepalive holds the internal SRAM, and a runtime WiFi.mode(STA)
    // can hang the whole watch — which is exactly what froze the
    // menu -> HexHound transition on open. The pet still evolves from BLE / NFC
    // / detections / peers / handshakes (all fed in the background); the live
    // WiFi survey is just a bonus whenever another WiFi tool is running.
    if (wifi_beacon_active()) wifi_beacon_add(pet_wifi_cb);
    // Start the engine tick + idle animation (deleted again on exit, on_unload).
    if (!s_timer) s_timer = lv_timer_create(on_tick, 1000, NULL);
    if (!s_anim)  s_anim  = lv_timer_create(on_anim, 80,   NULL);
    hexhound_update();
    refresh();
    lv_scr_load(s_screen);
}

bool pet_screen_is_active() { return s_active; }
