#pragma once
//
// face_watch.h - customization state for the Dot watchface (Tools > Face).
//
// Holds the wearer's Dot-face look: the hour font, the date font, the accent
// colour, and the date order. Persisted in NVS. The 12h/24h and wallpaper
// toggles on the same screen reuse the existing clock_screen_* setters (which
// own their own persistence), so they are not stored here.
//
// Pure state only: main.cpp maps these choices onto fonts/colours and repaints
// the face; the Face screen drives the setters. Enums are small and stable so
// the stored bytes keep meaning across firmware updates - only append new values
// before each __COUNT.
#include <cstdint>

// Hour digits: the 5x7 dot-matrix raster (the Nothing-OS look, default) or a
// large regular Montserrat clock font.
enum FaceHourFont : uint8_t { FACE_HOUR_DOTS = 0, FACE_HOUR_MONT, FACE_HOUR__COUNT };

// Date line font: Orbitron (geometric, default) or a clean monospace.
enum FaceDateFont : uint8_t { FACE_DATE_ORBITRON = 0, FACE_DATE_MONO, FACE_DATE__COUNT };

// Accent colour presets. Drives the Dot face's accent rail. Cyan and a vivid
// Pip-Boy green join the set; plain white was dropped (it read flat against the
// white text and status glyphs) and so was the pale steel-blue (too close to
// Grey).
//
// The numeric values are what gets saved in NVS, so they are pinned: removing the
// steel-blue must not renumber Cyan / Green for watches that already saved them.
enum FaceAccent : uint8_t {
    FACE_ACC_RED = 0, FACE_ACC_GREY = 1, FACE_ACC_AMBER = 2,
    // 3 = the retired steel-blue. Kept reserved; a saved 3 is read back as Grey.
    FACE_ACC_CYAN = 4, FACE_ACC_GREEN = 5,
    FACE_ACC_ORANGE = 6,   // the DotOS design accent (appended: saved values stay valid)
    FACE_ACC__COUNT = 7
};
constexpr uint8_t FACE_ACC_RETIRED_STEEL_BLUE = 3;

// Date order: DD/MM (default), MM/DD, YYYY-MM-DD, or DD Mon.
enum FaceDateOrder : uint8_t {
    FACE_ORDER_DMY = 0, FACE_ORDER_MDY, FACE_ORDER_ISO, FACE_ORDER_DMON,
    FACE_ORDER__COUNT
};

// "Global text" family for UI titles + body labels + notifications. Default
// keeps the ARGUS brand fonts (Saira titles / Orbitron labels); Roboto and Inter
// are the Nothing-OS-style alternates. The clock hour and the date line are NOT
// affected. theme_text_font() / theme_title_font() (theme.h) read this.
enum FaceTextFont : uint8_t {
    FACE_TEXT_DEFAULT = 0, FACE_TEXT_ROBOTO, FACE_TEXT_INTER, FACE_TEXT__COUNT
};

// Load the saved choices from NVS. Call once at boot, after Preferences is up.
void face_watch_boot_restore();

FaceHourFont  face_hour_font();
FaceDateFont  face_date_font();
FaceAccent    face_accent();
FaceDateOrder face_date_order();
FaceTextFont  face_text_font();

// Set + persist. main.cpp re-applies the look via clock_screen_apply_face_custom().
void face_set_hour_font(FaceHourFont v);
void face_set_date_font(FaceDateFont v);
void face_set_accent(FaceAccent v);
void face_set_date_order(FaceDateOrder v);
void face_set_text_font(FaceTextFont v);

// The current accent colour as a 0xRRGGBB value.
uint32_t face_accent_rgb();
