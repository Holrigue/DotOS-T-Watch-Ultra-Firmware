#pragma once
#include <stdint.h>
//
// dot_font_5x7.h - custom 5x7 dot-matrix digit glyphs for the ARGUS-Design-OS
// "Dot" watch face. This is NOT Ndot or any TTF: it is the exact hand-authored
// grid the design was frozen against (docs/dotface/dotface_final.svg), so the
// on-watch time matches the mockup dot for dot.
//
// Each glyph is 5 columns x 7 rows. One row is a 5-bit value: column 0 is the
// most-significant bit (0b10000), column 4 the least (0b00001). A cell is lit
// when ((row >> (4 - col)) & 1).
//
// Source grid (1 = lit):
//   0: 01110/10001/10011/10101/11001/10001/01110
//   1: 00100/01100/00100/00100/00100/00100/01110
//   2: 01110/10001/00001/00010/00100/01000/11111
//   3: 11111/00010/00100/00010/00001/10001/01110
//   4: 00010/00110/01010/10010/11111/00010/00010
//   5: 11111/10000/11110/00001/00001/10001/01110
//   6: 00110/01000/10000/11110/10001/10001/01110
//   7: 11111/00001/00010/00100/01000/01000/01000
//   8: 01110/10001/10001/01110/10001/10001/01110
//   9: 01110/10001/10001/01111/00001/00010/01100

#define DOT_GLYPH_COLS 5
#define DOT_GLYPH_ROWS 7

// dot_font_5x7[digit][row] -> 5-bit column mask (col 0 = bit 4).
static const uint8_t dot_font_5x7[10][DOT_GLYPH_ROWS] = {
    { 14, 17, 19, 21, 25, 17, 14 },   // 0
    {  4, 12,  4,  4,  4,  4, 14 },   // 1
    { 14, 17,  1,  2,  4,  8, 31 },   // 2
    { 31,  2,  4,  2,  1, 17, 14 },   // 3
    {  2,  6, 10, 18, 31,  2,  2 },   // 4
    { 31, 16, 30,  1,  1, 17, 14 },   // 5
    {  6,  8, 16, 30, 17, 17, 14 },   // 6
    { 31,  1,  2,  4,  8,  8,  8 },   // 7
    { 14, 17, 17, 14, 17, 17, 14 },   // 8
    { 14, 17, 17, 15,  1,  2, 12 },   // 9
};

// True when column `col` (0..4) of `digit` (0..9) row `row` (0..6) is lit.
static inline bool dot_glyph_lit(int digit, int col, int row)
{
    if (digit < 0 || digit > 9) return false;
    if (col < 0 || col >= DOT_GLYPH_COLS) return false;
    if (row < 0 || row >= DOT_GLYPH_ROWS) return false;
    return (dot_font_5x7[digit][row] >> (4 - col)) & 1;
}
