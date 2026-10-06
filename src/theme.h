#pragma once
// ARGUS theme.
//
// The 13-37 base hardcoded its matrix-green accent as lv_color_make(0x00,0xCC,0x66)
// (and a brighter "active" lv_color_make(0x00,0xFF,0x80)) at ~67 sites across the
// screens. This header centralizes the ARGUS brand palette so the accent lives
// in one place: DotOS orange at rest, HADES threat-red for the alert state
// (brand-as-functional-state). Colors are lv_color_make(r,g,b); LVGL converts to
// the panel's native format.
#include <lvgl.h>

// ---- DotOS design language -------------------------------------------------
// Black base, ONE orange accent, a cream "inverted" tile for the focused item,
// diagonal-hatch fills, squircle tiles and bold type (see docs/design/). The
// accent family below used to be steel-blue; every screen that uses these macros
// follows the palette from this one place.
//
// Static design accent (#FF5A1A). The wearer's Facewatch choice still drives the
// title/chrome colour through argus_base_accent(); "Orange" there is this same colour.
#define ARGUS_ACCENT         lv_color_make(0xFF, 0x5A, 0x1A)

// Brighter "active / live" accent.
#define ARGUS_ACCENT_ACTIVE  lv_color_make(0xFF, 0x8A, 0x4D)

// Dim accent for idle / secondary structure.
#define ARGUS_ACCENT_DIM     lv_color_make(0x8F, 0x35, 0x12)

// Surfaces. Pure black base (the panel is AMOLED: black pixels are off), then two
// lifts for tiles and for raised controls inside a tile.
#define ARGUS_BG             lv_color_make(0x00, 0x00, 0x00)
#define ARGUS_TILE           lv_color_make(0x16, 0x16, 0x16)
#define ARGUS_RAISED         lv_color_make(0x20, 0x20, 0x20)

// Cream: the "inverted" tile of the focused / selected item, and its dark text.
#define ARGUS_CREAM          lv_color_make(0xF4, 0xF2, 0xEC)

// Quiet secondary text, readable on tiles without competing with the accent.
#define ARGUS_QUIET          lv_color_make(0x8A, 0x8A, 0x86)

// Corner radii. The panel itself is a rounded rectangle, so big radii nest with it.
#define ARGUS_R_TILE         46     // app / data tile
#define ARGUS_R_ROW          34     // settings row, list card
#define ARGUS_R_PILL         LV_RADIUS_CIRCLE   // buttons, toggles, bars

// The three looks a tile takes. Normal sits on the black base; Focus is the cream
// inverted tile; Accent is the orange one (the active or primary item).
enum class ArgusTile { Normal, Focus, Accent };

// Style a plain lv_obj as a tile: background, radius, no border/padding/scroll.
// Children place themselves; use argus_tile_text() for the readable text colour.
void argus_style_tile(lv_obj_t *obj, ArgusTile kind, int radius = ARGUS_R_TILE);

// Text colour that reads on a tile of this kind (black on cream/orange, white on normal).
lv_color_t argus_tile_text(ArgusTile kind);

// Diagonal hatch fill (the "Figma charts" stripe): paints a tiled 8x8 A8 stripe over
// the object's background in `color`. For flat strips and panels (radius 0 reads
// best); the object's own bg colour shows between the stripes. Costs no RAM: the
// tile is a 64-byte constant.
void argus_style_hatch(lv_obj_t *obj, lv_color_t color);

// Style an lv_slider as a fat pill (track + orange fill + a knob that is easy to grab).
void argus_style_pill_slider(lv_obj_t *slider);

// Secondary body/label TEXT colour. The maintainer preferred the secondary text in a
// warm white/cream over the accent, so these carry body text while the
// accent is reserved for structure/titles. ARGUS_TEXT = bright cream, _DIM = a
// softer warm grey for captions/hints.
#define ARGUS_TEXT           lv_color_make(0xED, 0xE8, 0xDA)
#define ARGUS_TEXT_DIM       lv_color_make(0xB2, 0xAB, 0x98)

// HADES threat-red (#DB615A) — the "red eyes" alert state.
#define HADES_RED            lv_color_make(0xDB, 0x61, 0x5A)

// Offense-mode base accent — aggressive red-team red (#F02E2E). Distinct from
// the calm accent (Daily/Defense). Drives the Offense border + "OFF" chip
// and the offense tool icons, so Offense reads unmistakably "red team". HADES_RED
// (a softer coral) still overlays as the live threat/alert state on top of this.
#define ARGUS_OFFENSE_ACCENT lv_color_make(0xF0, 0x2E, 0x2E)

// Runtime, state-aware accent. Returns ARGUS_ACCENT at rest and
// HADES_RED when Threat Radar is flagging a tail (top level >= TR_LVL_LIKELY).
//
// RESERVED FOR ALERT SURFACES ONLY. Red on the Defense side means "a threat is
// live right now", so only surfaces whose job is to raise that alarm may call
// this: the clock status icons (main.cpp status_accent_active(), same
// threshold) and the Threat Radar screen. Ordinary screen chrome must NOT;
// see argus_base_accent() below. Defined in theme.cpp.
lv_color_t argus_accent(void);

// Mode-aware BASE accent (no threat overlay): the base accent in Daily/Defense,
// red-team red in Offense. argus_accent() layers the threat-red flip on top of
// this (except in Daily, which stays innocent and never flips).
//
// THIS IS THE DEFAULT FOR SCREEN CHROME: every screen title/heading, list
// accent, card border and notification banner uses it, so colour tracks the
// MODE and red appears only on the Offense side. Using argus_accent() for
// chrome was the 2026-07-30 bug: a live WiFi/tracker threat turned every
// Defense-mode heading (TOOLS, TIME, WARDRIVER, MESHTASTIC, NODES, SEND
// MESSAGE, CONFIGURATION, SETTINGS, ...) HADES-red, which reads as "you are in
// Offense" rather than as an alert. Defined in theme.cpp.
lv_color_t argus_base_accent(void);

// Persistent per-mode indicator on lv_layer_top(): an amber/red border frame in
// Offense, nothing in Daily or Defense. init() once at boot (after LVGL/clock
// build); refresh() on every mode change and on the 1s status tick so the border
// flips live under threat. Defined in theme.cpp.
//
// The "DEF" / "OFF" corner chip this used to also draw is disabled (2026-07-28)
// and left commented in theme.cpp: mode is already obvious from the wallpaper,
// tool set and accent colour, and the chip rode on every screen, not just the
// clock.
void argus_mode_indicator_init(void);
void argus_mode_indicator_refresh(void);

// ---- On-screen keyboard placement ------------------------------------------
//
// The panel is a 410x502 ROUNDED rectangle. A keyboard sized to the full 410 and
// aligned flush to LV_ALIGN_BOTTOM_MID puts its bottom row exactly where the
// corner radius cuts in, so the outer keys of that row are clipped by the bezel
// and cannot be read or reliably tapped. Every keyboard in the tree had this.
//
// argus_keyboard_fit() applies one safe geometry to all of them: narrowed to
// ARGUS_KB_SAFE_W and lifted ARGUS_KB_BOTTOM_INSET px off the bottom edge, which
// keeps the bottom row inside the flat part of the panel. The LVGL keyboard is a
// button matrix and lays its keys out within the object, so narrowing the object
// narrows the keys rather than clipping them.
//
// Height stays per-caller: keyboards here range 180..240 px depending on how much
// of the screen the field above them needs.
//
// These two numbers are the whole tuning surface. If a bottom row still clips,
// raise ARGUS_KB_BOTTOM_INSET (and/or lower ARGUS_KB_SAFE_W) here, once, rather
// than editing call sites.
#define ARGUS_KB_SAFE_W         360
#define ARGUS_KB_BOTTOM_INSET   36

void argus_keyboard_fit(lv_obj_t *kb, int height);

// Pipeline-driven threat override for the brand accent. The detect_pipeline
// (WiFi evil-twin + beacon-flood aggregator, src/detect_pipeline.*) calls this
// to flip argus_accent() to HADES_RED when its ThreatState posture reaches
// Alert or above, independently of the GPS-co-movement Threat Radar. Passing
// false clears the override, so the accent falls back to the Threat Radar state.
// Default (never called) is false, so existing behavior is unchanged until the
// pipeline drives it. Defined in theme.cpp.
void argus_set_threat(bool active);

// Full-alphabet Saira Condensed brand font for screen titles (src/font_argus_ui.c).
LV_FONT_DECLARE(font_argus_ui);

// ARGUS UI text fonts (brand system): Orbitron for labels, VT323 for numeric/
// terminal readouts, Saira Condensed for the wordmark/titles, and Montserrat for
// the digital clock. All are generated from OFL-licensed font sources.
LV_FONT_DECLARE(font_argus_label_14);   // Orbitron 14 - small labels
LV_FONT_DECLARE(font_argus_label_16);   // Orbitron 16 - body labels
LV_FONT_DECLARE(font_argus_label_20);   // Orbitron 20 - tile labels, date
LV_FONT_DECLARE(font_argus_label_28);   // Orbitron 28 - larger labels
LV_FONT_DECLARE(font_argus_mono_16);    // VT323 16 - small numeric readouts
LV_FONT_DECLARE(font_argus_mono_48);    // VT323 48 subset (digits/colon) - big readouts (alarm/stopwatch/timer)
LV_FONT_DECLARE(font_argus_label_48);   // Orbitron 48 subset (START/STOP) - wardriver button

// Alternate "Global text" families (Nothing-OS inspired), selectable in
// Facewatch. Full printable-ASCII subsets at the same sizes as the Orbitron
// label set + the Saira title (32). Generated by tools/gen_clock_font.py from
// the OFL Roboto / Inter variable fonts (default upright instance).
LV_FONT_DECLARE(font_roboto_14);
LV_FONT_DECLARE(font_roboto_16);
LV_FONT_DECLARE(font_roboto_20);
LV_FONT_DECLARE(font_roboto_28);
LV_FONT_DECLARE(font_roboto_32);
LV_FONT_DECLARE(font_inter_14);
LV_FONT_DECLARE(font_inter_16);
LV_FONT_DECLARE(font_inter_20);
LV_FONT_DECLARE(font_inter_28);
LV_FONT_DECLARE(font_inter_32);

// Runtime UI-text font selection ("Global text" in Facewatch). theme_text_font()
// returns the selected family at the given px (14/16/20/28 — the label sizes);
// theme_title_font() returns it at the 32 px title size. With the default choice
// they return the Orbitron label fonts / Saira title, so callers are unchanged.
// The clock hour and the Dot date line are deliberately NOT routed through these
// (they keep their own fonts).
const lv_font_t *theme_text_font(int px);
const lv_font_t *theme_title_font();
