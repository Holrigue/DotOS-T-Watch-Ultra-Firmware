#include "theme.h"
#include "threat_radar.h"
#include "argus_mode.h"
#include "face_watch.h"   // user-chosen accent colour (Facewatch)

// Runtime, state-aware brand accent (ARGUS -> HADES).
//
// The compile-time ARGUS_ACCENT macro paints the DotOS orange resting brand
// at ~67 low-traffic sites. This function is its live, threat-aware sibling:
// callers that repaint frequently (the clock status bar, the Threat Radar
// screen) call argus_accent() instead of the macro so the accent tracks the
// threat state. When Threat Radar has a contact at TR_LVL_LIKELY or above — i.e.
// something is co-moving with the wearer — the accent flips to HADES_RED so the
// watch visibly "opens its red eyes"; otherwise it stays on the base accent. The flip is
// glanceable and returns to calm on its own once the tail clears the staleness
// window (threatradar_top_level() reads only live contacts).
// Pipeline-driven threat override. The WiFi detect_pipeline sets this true when
// its ThreatState posture is Alert+; it flips the accent to HADES_RED the same
// way a Threat Radar tail does, on top of (independent of) the radar path. Both
// sources OR together, so either one flips the brand to the alert state.
static bool s_pipeline_threat = false;

void argus_set_threat(bool active)
{
    s_pipeline_threat = active;
}

// System accent = the user's Facewatch colour choice, so every screen title and
// accent tracks it (the colour cohesion the wearer picks). Threat alerts still
// flip to HADES_RED on top of this via argus_accent(); Offense is still signalled
// by its wallpaper/toolset and the mode border, not by recolouring the accent.
lv_color_t argus_base_accent(void)
{
    return lv_color_hex(face_accent_rgb());
}

lv_color_t argus_accent(void)
{
    // Daily stays INNOCENT: it never flips to the threat-red alert state, so a
    // detector firing in the background can't give the game away at a glance.
    if (argus_mode_current() == ArgusMode::Daily) return argus_base_accent();

    bool threat = s_pipeline_threat || threatradar_top_level() >= TR_LVL_LIKELY;
    return threat ? HADES_RED : argus_base_accent();
}

// ---- Design-language helpers (see theme.h) ----------------------------------

lv_color_t argus_tile_text(ArgusTile kind)
{
    return kind == ArgusTile::Normal ? ARGUS_CREAM : lv_color_black();
}

void argus_style_tile(lv_obj_t *obj, ArgusTile kind, int radius)
{
    lv_color_t bg = ARGUS_TILE;
    if (kind == ArgusTile::Focus)  bg = ARGUS_CREAM;
    if (kind == ArgusTile::Accent) bg = ARGUS_ACCENT;
    lv_obj_set_style_bg_color(obj, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

// 8x8 alpha tile of a 45-degree stripe, 2 px wide every 8 px. (x + y) & 7 is periodic
// in both axes, so the tile repeats without a seam.
static const uint8_t kHatchTile[64] = {
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00
};
static lv_image_dsc_t s_hatch_dsc;

void argus_style_hatch(lv_obj_t *obj, lv_color_t color)
{
    if (!s_hatch_dsc.data) {
        s_hatch_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
        s_hatch_dsc.header.cf     = LV_COLOR_FORMAT_A8;
        s_hatch_dsc.header.flags  = 0;
        s_hatch_dsc.header.w      = 8;
        s_hatch_dsc.header.h      = 8;
        s_hatch_dsc.header.stride = 8;
        s_hatch_dsc.data_size     = sizeof(kHatchTile);
        s_hatch_dsc.data          = kHatchTile;
    }
    lv_obj_set_style_bg_image_src(obj, &s_hatch_dsc, LV_PART_MAIN);
    lv_obj_set_style_bg_image_tiled(obj, true, LV_PART_MAIN);
    lv_obj_set_style_bg_image_recolor(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_image_recolor_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
}

void argus_style_pill_slider(lv_obj_t *slider)
{
    lv_obj_set_style_bg_color(slider, ARGUS_RAISED, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, ARGUS_R_PILL, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, ARGUS_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, ARGUS_R_PILL, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, ARGUS_CREAM, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, ARGUS_R_PILL, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, 14);                    // finger-sized hit area
    lv_obj_clear_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);    // a drag is not a page swipe
}

// ---- Persistent per-mode indicator (lv_layer_top overlay) -------------------

static lv_obj_t *s_mode_frame    = nullptr;   // full-screen border (Offense only)

// The "DEF" / "OFF" corner chip is DISABLED (2026-07-28). Mode is already
// unmistakable without it: the wallpaper changes, the tool set changes, and the
// accent colour changes. The chip was redundant on top of that, and it sat on
// lv_layer_top so it rode along on every screen rather than just the clock.
// Kept commented rather than deleted so it can be restored in one edit.
// static lv_obj_t *s_mode_chip     = nullptr;   // "DEF" / "OFF" chip container
// static lv_obj_t *s_mode_chip_lbl = nullptr;   // the chip's text

void argus_mode_indicator_init(void)
{
    lv_obj_t *top = lv_layer_top();

    s_mode_frame = lv_obj_create(top);
    lv_obj_remove_style_all(s_mode_frame);
    lv_obj_set_size(s_mode_frame, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_mode_frame, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_mode_frame, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_mode_frame, ARGUS_OFFENSE_ACCENT, LV_PART_MAIN);
    lv_obj_add_flag(s_mode_frame, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(s_mode_frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_mode_frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_mode_frame, LV_OBJ_FLAG_HIDDEN);

    // --- "DEF" / "OFF" corner chip: DISABLED, see the note on the statics. ---
    // Chip = a small container (bg + radius) with a centred label child, the
    // reliable idiom here (a bare label-with-bg did not render as a chip).
    // s_mode_chip = lv_obj_create(top);
    // lv_obj_remove_style_all(s_mode_chip);
    // lv_obj_set_size(s_mode_chip, 48, 26);
    // lv_obj_set_style_bg_opa(s_mode_chip, LV_OPA_COVER, LV_PART_MAIN);
    // lv_obj_set_style_bg_color(s_mode_chip, ARGUS_ACCENT, LV_PART_MAIN);
    // lv_obj_set_style_radius(s_mode_chip, 6, LV_PART_MAIN);
    // lv_obj_clear_flag(s_mode_chip, LV_OBJ_FLAG_CLICKABLE);
    // lv_obj_clear_flag(s_mode_chip, LV_OBJ_FLAG_SCROLLABLE);
    // // Sit the badge UP IN THE STATUS HEADER ROW (same y as the WiFi/BT/SD/GPS
    // // icons at y~20), on the LEFT half of that row - the status icons live on the
    // // right, this side is empty. Referenced from TOP_MID (screen centre, x~205 on
    // // the 410-wide panel) so it lands well clear of BOTH rounded top corners; the
    // // leftward offset keeps it left of the right-side status chain. NOTE: this is
    // // an lv_layer_top overlay so it shows on every screen's header band - the
    // // redesign will make it a proper clock-only status-row element.
    // lv_obj_align(s_mode_chip, LV_ALIGN_TOP_MID, -120, 16);
    // lv_obj_add_flag(s_mode_chip, LV_OBJ_FLAG_HIDDEN);
    //
    // s_mode_chip_lbl = lv_label_create(s_mode_chip);
    // lv_obj_set_style_text_font(s_mode_chip_lbl, &font_argus_label_14, LV_PART_MAIN);
    // lv_obj_set_style_text_color(s_mode_chip_lbl, lv_color_black(), LV_PART_MAIN);
    // lv_label_set_text(s_mode_chip_lbl, "DEF");
    // lv_obj_center(s_mode_chip_lbl);

    argus_mode_indicator_refresh();
}

void argus_mode_indicator_refresh(void)
{
    // Guard covers the frame only. The chip pointers are gone with the chip; if
    // it is ever restored, add them back here or the frame stops refreshing.
    if (!s_mode_frame) return;

    // Border frame: DISABLED. The red Offense border was removed — the double
    // opt-in to enter Offense (consent card + PIN) already makes the mode
    // unmistakable, so a persistent glowing frame on every screen was just
    // noise. The (hidden) object and this refresh are kept so the frame can be
    // restored in a single edit if it is ever wanted again.
    lv_obj_add_flag(s_mode_frame, LV_OBJ_FLAG_HIDDEN);

    // --- "DEF" / "OFF" corner chip: DISABLED, see the note on the statics. ---
    // Chip: hidden in Daily (innocent), "DEF" (accent) in Defense, "OFF" in Offense.
    // if (m == ArgusMode::Daily) {
    //     lv_obj_add_flag(s_mode_chip, LV_OBJ_FLAG_HIDDEN);
    // } else {
    //     bool off = (m == ArgusMode::Offense);
    //     lv_label_set_text(s_mode_chip_lbl, off ? "OFF" : "DEF");
    //     lv_obj_set_style_bg_color(s_mode_chip, off ? argus_accent() : ARGUS_ACCENT, LV_PART_MAIN);
    //     lv_obj_clear_flag(s_mode_chip, LV_OBJ_FLAG_HIDDEN);
    //     lv_obj_move_foreground(s_mode_chip);   // above any other top-layer content
    // }
}

// ---- On-screen keyboard placement ------------------------------------------
// See the rationale in theme.h. One geometry for every keyboard so the rounded
// corners cannot clip a bottom row, and so the numbers cannot drift apart across
// the eight screens that create one.
void argus_keyboard_fit(lv_obj_t *kb, int height)
{
    if (!kb) return;
    lv_obj_set_size(kb, ARGUS_KB_SAFE_W, height);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -ARGUS_KB_BOTTOM_INSET);
}

// ---- "Global text" font selection (Facewatch) -------------------------------
// Body labels and titles resolve their font through these instead of naming the
// Orbitron/Saira brand fonts directly, so the wearer's Facewatch choice
// (Default / Roboto / Inter) applies system-wide. Default returns the exact
// brand fonts, so it is a no-op; a screen picks up a change the next time it is
// (re)built. The clock hour and the Dot date line are intentionally NOT routed
// through here — they keep their own fonts.
const lv_font_t *theme_text_font(int px)
{
    FaceTextFont fam = face_text_font();
    switch (px) {
    case 14: return fam == FACE_TEXT_ROBOTO ? &font_roboto_14
                  : fam == FACE_TEXT_INTER  ? &font_inter_14  : &font_argus_label_14;
    case 16: return fam == FACE_TEXT_ROBOTO ? &font_roboto_16
                  : fam == FACE_TEXT_INTER  ? &font_inter_16  : &font_argus_label_16;
    case 28: return fam == FACE_TEXT_ROBOTO ? &font_roboto_28
                  : fam == FACE_TEXT_INTER  ? &font_inter_28  : &font_argus_label_28;
    case 20:
    default: return fam == FACE_TEXT_ROBOTO ? &font_roboto_20
                  : fam == FACE_TEXT_INTER  ? &font_inter_20  : &font_argus_label_20;
    }
}

const lv_font_t *theme_title_font()
{
    switch (face_text_font()) {
    case FACE_TEXT_ROBOTO: return &font_roboto_32;
    case FACE_TEXT_INTER:  return &font_inter_32;
    default:               return &font_argus_ui;
    }
}
