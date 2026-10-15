#pragma once
#include <stdint.h>

// ── HexHound 8-bit sprites — pure data + composition, no LVGL ──────────────
//
// Five 16x16 creatures (Egg, Packet Pup, Beacon Beast, Gremlin, Sentinel). Each
// cell holds a palette CODE, not a colour, so one sprite serves every accent:
// the renderer (pet_screen.cpp) turns codes into colours with
// hexhound_sprite_palette(), which derives the body shades from the accent the
// user picked in Face > Accent. The mood face (eyes, mouth, markers) is drawn
// over the stage's base art by hexhound_sprite_compose(), so 5 bases give all
// 30 stage x mood images. Host-tested (test/test_hexhound_sprite.cpp).
//
// Stage and mood values are those of hexhound.h (HEX_EGG=1.., HEX_IDLE=0..);
// they are taken as plain integers so this header stays free of the engine.

enum HexPix : uint8_t {
    HXP_NONE = 0,   // transparent
    HXP_BODY,       // B  accent
    HXP_LIGHT,      // A  accent mixed toward white (highlight, muzzle, paws)
    HXP_DARK,       // D  accent mixed toward black (ears, outline, shadow)
    HXP_TAIL,       // T  accent, slightly darker
    HXP_INK,        // K  near-black (eyes, nose, mouth, collar)
    HXP_WHITE,      // W  catch-lights, fangs, Z
    HXP_PINK,       // R  tongue, blush, alert
    HXP_YELLOW,     // Y  sparkles, beacon light
    HXP_CYAN,       // C  collar "packet" tag, visor eyes, drool
    HXP_SLATE,      // G  Sentinel's eyes when the visor is dimmed
    HXP_COUNT
};

static constexpr int HEX_SPRITE_N = 16;

namespace hexsprite_detail {

struct StageDef {
    const char *art[HEX_SPRITE_N];
    int8_t le, re, ey;      // left / right eye column, eye row (2x2 eyes)
    bool   nose;            // muzzle with a nose at (7,8)-(8,8)
    int8_t my;              // mouth row
    uint8_t ec;             // eye colour
    int8_t zx, zy;          // "Z" origin (sleepy)
    int8_t sx, sy;          // sparkle origin (excited)
    int8_t ax, ay;          // "!" origin (wary); ax < 0 = none
    bool droop, beacon, fangs, visor;
};

// Index 0 = HEX_EGG ... 4 = HEX_SENTINEL.
static const StageDef kStages[5] = {
    { {  // Egg: shell with a crack and cyan "packet" speckles
        "................",
        "......BBBB......",
        ".....BAABBB.....",
        "....BAABBCDB....",
        "....BABBBBBD....",
        "...BBBBBBBDBB...",
        "...BBBBBBBBDB...",
        "..BBBBBBBBDBBB..",
        "..BBBBBBBBBBBB..",
        "..BBBBBBBBBBBB..",
        "..BBBBBBBBBBBB..",
        "..BCBBBBBBBBBB..",
        "...BBBBBBBBBCB..",
        "...DBBBBBBBBD...",
        "....DDDDDDDD....",
        "................" },
      5, 9, 8, false, 11, HXP_INK, 12, 0, 12, 1, 3, 1, false, false, false, false },
    { {  // Packet Pup: round head, floppy ears, muzzle, collar with a packet tag
        "................",
        "................",
        "....BBBBBBBB....",
        "..DBBBBBBBBBBD..",
        ".DDBBABBBBBBBDD.",
        ".DDBBBBBBBBBBDD.",
        ".DDBBBBBBBBBBDD.",
        ".DDBBBBBBBBBBDD.",
        "..DBBBAAAABBBD..",
        "...BBAAAAAABB...",
        "....BBAAAABB....",
        "....KKKCCKKK...T",
        "...BBBBCCBBBB.TT",
        "...BBBBBBBBBBTT.",
        "...BBDBBBBDBB...",
        "...AAA.BB.AAA..." },
      4, 10, 6, true, 9, HXP_INK, 13, 0, 12, 0, 7, 0, true, false, false, false },
    { {  // Beacon Beast: bigger dog, upright ears, beacon + signal waves
        ".......YY.......",
        ".C.....DD.....C.",
        "C..BB..DD..BB..C",
        "C..BBBBBBBBBB..C",
        ".C.BBABBBBBBB.C.",
        "..BBBBBBBBBBBB..",
        "..BBBBBBBBBBBB..",
        "..BBBBBBBBBBBB..",
        "..BBBBAAAABBBB..",
        "...BBAAAAAABB...",
        "..BBBBAAAABBBB..",
        ".BBKKKKCCKKKKBB.",
        ".BBBBBBCCBBBBBB.",
        ".BBBBBBBBBBBBBB.",
        ".BBDBBBBBBBBDBB.",
        ".AAAA.BBBB.AAAA." },
      4, 10, 6, true, 9, HXP_INK, 12, 0, 10, 0, -1, 0, false, true, false, false },
    { {  // Gremlin: tall pointed ears, fangs, spiked tail
        "B..............B",
        "BB............BB",
        "BBB..........BBB",
        ".BBBB.BBBBB.BBB.",
        ".BBBBBBBBBBBBBB.",
        "..BBBABBBBBBBB..",
        "..BBBBBBBBBBBB..",
        "..BBBBBBBBBBBB..",
        "..BBBBBBBBBBBB..",
        "...BBBBBBBBBB...",
        "....BBBBBBBB....",
        "...DBBBBBBBBD...",
        "..DBBBBBBBBBBD..",
        ".D.BBBBBBBBBB.D.",
        "...BBD....DBB.TT",
        "...DD......DDT.." },
      4, 10, 6, false, 9, HXP_INK, 6, 0, 6, 0, 7, 0, false, false, true, false },
    { {  // Sentinel: armoured, visor with glowing eyes, pauldrons
        "................",
        "...D........D...",
        "...DD.DDDD.DD...",
        "...DDDBBBBDDD...",
        "..DBBBBABBBBBD..",
        "..DBBBBBBBBBBD..",
        "..DKKKKKKKKKKD..",
        "..DKKKKKKKKKKD..",
        "..DBBBAAAABBBD..",
        "...DBAAAAAABD...",
        "....DBAAAABD....",
        ".AAKKKKCCKKKKAA.",
        "AAABBBBCCBBBBAAA",
        ".DBBBBBBBBBBBBD.",
        "..BBDBBBBBBDBB..",
        "..DDD.DDDD.DDD.." },
      4, 10, 6, true, 9, HXP_CYAN, 13, 0, 13, 0, 7, 0, false, false, false, true },
};

inline uint8_t code_of(char c)
{
    switch (c) {
        case 'B': return HXP_BODY;   case 'A': return HXP_LIGHT;
        case 'D': return HXP_DARK;   case 'T': return HXP_TAIL;
        case 'K': return HXP_INK;    case 'W': return HXP_WHITE;
        case 'R': return HXP_PINK;   case 'Y': return HXP_YELLOW;
        case 'C': return HXP_CYAN;   case 'G': return HXP_SLATE;
        default:  return HXP_NONE;
    }
}

struct Canvas {
    uint8_t (*px)[HEX_SPRITE_N];
    void put(int x, int y, uint8_t c) const
    {
        if (x >= 0 && x < HEX_SPRITE_N && y >= 0 && y < HEX_SPRITE_N) px[y][x] = c;
    }
};

inline uint8_t mix8(uint8_t a, uint8_t b, int pct)
{
    return (uint8_t)((a * (100 - pct) + b * pct) / 100);
}

} // namespace hexsprite_detail

// Mood indices (mirror HexMood in hexhound.h).
enum : uint8_t { HXM_IDLE = 0, HXM_HAPPY, HXM_EXCITED, HXM_HUNGRY, HXM_SLEEPY, HXM_WARY };

// Compose the 16x16 code grid for a stage (1..5) and mood (0..5). Out-of-range
// values clamp to the nearest valid one, so a corrupt save never draws garbage.
inline void hexhound_sprite_compose(uint8_t stage, uint8_t mood,
                                    uint8_t out[HEX_SPRITE_N][HEX_SPRITE_N])
{
    using namespace hexsprite_detail;
    if (stage < 1) stage = 1;
    if (stage > 5) stage = 5;
    if (mood > HXM_WARY) mood = HXM_IDLE;
    const StageDef &s = kStages[stage - 1];
    for (int y = 0; y < HEX_SPRITE_N; y++)
        for (int x = 0; x < HEX_SPRITE_N; x++)
            out[y][x] = code_of(s.art[y][x]);

    Canvas g{out};
    const int LE = s.le, RE = s.re, EY = s.ey, MY = s.my;
    const uint8_t ec = s.ec;
    const bool hl = !s.visor;               // catch-lights (not through a visor)
    const int eyes_x[2] = { LE, RE };

    if (s.nose) { g.put(7, 8, HXP_INK); g.put(8, 8, HXP_INK); }
    if (s.beacon) {
        uint8_t b = mood == HXM_WARY ? HXP_PINK : (mood == HXM_SLEEPY ? HXP_LIGHT : HXP_YELLOW);
        g.put(7, 0, b); g.put(8, 0, b);
    }

    auto eyes_open = [&]() {
        for (int x : eyes_x) { g.put(x, EY, ec); g.put(x + 1, EY, ec); g.put(x, EY + 1, ec); g.put(x + 1, EY + 1, ec); }
    };
    auto eyes_lid = [&](uint8_t c) {
        for (int x : eyes_x) { g.put(x, EY + 1, c); g.put(x + 1, EY + 1, c); }
    };

    switch (mood) {
    case HXM_IDLE:
        eyes_open();
        if (hl) { g.put(LE, EY, HXP_WHITE); g.put(RE, EY, HXP_WHITE); }
        if (s.nose) { g.put(6, MY, HXP_INK); g.put(9, MY, HXP_INK); }
        else        { g.put(7, MY, HXP_INK); g.put(8, MY, HXP_INK); }
        break;
    case HXM_HAPPY:
        for (int x : eyes_x) { g.put(x - 1, EY + 1, ec); g.put(x, EY, ec); g.put(x + 1, EY, ec); g.put(x + 2, EY + 1, ec); }
        g.put(6, MY, HXP_INK); g.put(9, MY, HXP_INK);
        if (s.nose) { g.put(7, MY + 1, HXP_PINK); g.put(8, MY + 1, HXP_PINK); }
        else        { g.put(7, MY + 1, HXP_INK);  g.put(8, MY + 1, HXP_INK); }
        g.put(LE - 1, EY + 2, HXP_PINK); g.put(RE + 2, EY + 2, HXP_PINK);   // blush
        break;
    case HXM_EXCITED:
        for (int x : eyes_x) {
            if (s.visor) { for (int i = -1; i <= 2; i++) g.put(x + i, EY, ec); g.put(x, EY + 1, ec); g.put(x + 1, EY + 1, ec); }
            else for (int j = -1; j <= 1; j++) { g.put(x, EY + j, ec); g.put(x + 1, EY + j, ec); }
        }
        if (hl) { g.put(LE, EY - 1, HXP_WHITE); g.put(RE, EY - 1, HXP_WHITE); }
        for (int x = 6; x <= 9; x++) g.put(x, MY, HXP_INK);
        g.put(7, MY + 1, HXP_PINK); g.put(8, MY + 1, HXP_PINK);
        g.put(s.sx + 1, s.sy, HXP_YELLOW); g.put(s.sx, s.sy + 1, HXP_YELLOW);
        g.put(s.sx + 2, s.sy + 1, HXP_YELLOW); g.put(s.sx + 1, s.sy + 2, HXP_YELLOW);
        break;
    case HXM_HUNGRY:
        eyes_open();
        if (hl) { g.put(LE + 1, EY, HXP_WHITE); g.put(RE + 1, EY, HXP_WHITE); }   // looking up
        if (s.nose) { g.put(6, MY, HXP_INK); g.put(9, MY, HXP_INK); }
        else        { g.put(7, MY, HXP_INK); g.put(8, MY, HXP_INK); }
        g.put(8, MY + 1, HXP_PINK); g.put(8, MY + 2, HXP_PINK);                 // tongue
        g.put(9, MY + 2, HXP_CYAN); g.put(9, MY + 3, HXP_CYAN);                 // drool
        break;
    case HXM_SLEEPY: {
        eyes_lid(s.visor ? (uint8_t)HXP_SLATE : ec);
        g.put(7, MY, HXP_INK); g.put(8, MY, HXP_INK);
        if (s.droop) { g.put(2, 3, HXP_NONE); g.put(13, 3, HXP_NONE); }
        static const int8_t Z[8][2] = { {0,0},{1,0},{2,0},{2,1},{1,2},{0,3},{1,3},{2,3} };
        for (const auto &p : Z) g.put(s.zx + p[0], s.zy + p[1], HXP_WHITE);
        break;
    }
    case HXM_WARY:
        eyes_lid(s.visor ? (uint8_t)HXP_SLATE : ec);
        g.put(LE + 1, EY + 1, HXP_PINK); g.put(RE, EY + 1, HXP_PINK);           // red glint
        if (!s.visor) {                                                          // frown
            g.put(LE - 1, EY - 1, HXP_INK); g.put(LE, EY, HXP_INK); g.put(LE + 1, EY, HXP_INK);
            g.put(RE, EY, HXP_INK); g.put(RE + 1, EY, HXP_INK); g.put(RE + 2, EY - 1, HXP_INK);
        }
        for (int x = 6; x <= 9; x++) g.put(x, MY + 1, HXP_INK);
        g.put(7, MY + 1, HXP_WHITE);                                             // fang
        if (s.ax >= 0) for (int j = 0; j < 3; j++) g.put(s.ax, s.ay + j, HXP_PINK);
        break;
    }
    if (s.fangs && (mood == HXM_IDLE || mood == HXM_HUNGRY || mood == HXM_WARY)) {
        for (int x = 5; x <= 10; x++) g.put(x, MY, HXP_INK);
        g.put(6, MY + 1, HXP_WHITE); g.put(9, MY + 1, HXP_WHITE);
    }
}

// Palette for an accent (0xRRGGBB) as 0xAARRGGBB. Only the body shades follow
// the accent; the face and marker colours are fixed so every mood stays readable
// on every accent. Entry HXP_NONE is fully transparent.
inline void hexhound_sprite_palette(uint32_t accent_rgb, uint32_t out[HXP_COUNT])
{
    using hexsprite_detail::mix8;
    const uint8_t r = (accent_rgb >> 16) & 0xFF, g = (accent_rgb >> 8) & 0xFF, b = accent_rgb & 0xFF;
    auto argb = [](uint8_t R, uint8_t G, uint8_t B) {
        return 0xFF000000u | ((uint32_t)R << 16) | ((uint32_t)G << 8) | B;
    };
    out[HXP_NONE]   = 0x00000000u;
    out[HXP_BODY]   = argb(r, g, b);
    out[HXP_LIGHT]  = argb(mix8(r, 255, 55), mix8(g, 255, 55), mix8(b, 255, 55));
    out[HXP_DARK]   = argb(mix8(r, 0, 45), mix8(g, 0, 45), mix8(b, 0, 45));
    out[HXP_TAIL]   = argb(mix8(r, 0, 25), mix8(g, 0, 25), mix8(b, 0, 25));
    out[HXP_INK]    = 0xFF0E0E12u;
    out[HXP_WHITE]  = 0xFFFAF6EEu;
    out[HXP_PINK]   = 0xFFFF6078u;
    out[HXP_YELLOW] = 0xFFFFD640u;
    out[HXP_CYAN]   = 0xFF50DCFFu;
    out[HXP_SLATE]  = 0xFF465A6Eu;
}
