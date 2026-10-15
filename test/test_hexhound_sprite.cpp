// test_hexhound_sprite.cpp - host tests for the HexHound 8-bit sprites (src/hexhound_sprite.h).
#include "wl_test.h"
#include "hexhound_sprite.h"

#include <cstdint>
#include <cstring>

static int count(const uint8_t g[HEX_SPRITE_N][HEX_SPRITE_N], uint8_t code) {
  int n = 0;
  for (int y = 0; y < HEX_SPRITE_N; y++)
    for (int x = 0; x < HEX_SPRITE_N; x++) n += g[y][x] == code;
  return n;
}

WL_TEST(hexhound_sprite_base_art_is_16x16_and_uses_known_codes) {
  for (const auto &s : hexsprite_detail::kStages)
    for (int y = 0; y < HEX_SPRITE_N; y++) {
      WL_CHECK_EQ((int)strlen(s.art[y]), HEX_SPRITE_N);
      for (int x = 0; x < HEX_SPRITE_N; x++) {
        char c = s.art[y][x];
        WL_CHECK(c == '.' || hexsprite_detail::code_of(c) != HXP_NONE);   // no typo'd letters
      }
    }
}

WL_TEST(hexhound_sprite_every_stage_and_mood_has_a_body_and_a_face) {
  uint8_t g[HEX_SPRITE_N][HEX_SPRITE_N];
  for (int st = 1; st <= 5; st++)
    for (int m = HXM_IDLE; m <= HXM_WARY; m++) {
      hexhound_sprite_compose((uint8_t)st, (uint8_t)m, g);
      WL_CHECK(count(g, HXP_BODY) > 60);                       // a creature, not a speck
      WL_CHECK(count(g, HXP_INK) + count(g, HXP_CYAN) > 0);    // eyes / mouth drawn
      for (int y = 0; y < HEX_SPRITE_N; y++)
        for (int x = 0; x < HEX_SPRITE_N; x++) WL_CHECK(g[y][x] < HXP_COUNT);
    }
}

WL_TEST(hexhound_sprite_moods_differ_from_each_other) {
  uint8_t a[HEX_SPRITE_N][HEX_SPRITE_N], b[HEX_SPRITE_N][HEX_SPRITE_N];
  for (int st = 1; st <= 5; st++)
    for (int m1 = HXM_IDLE; m1 <= HXM_WARY; m1++)
      for (int m2 = m1 + 1; m2 <= HXM_WARY; m2++) {
        hexhound_sprite_compose((uint8_t)st, (uint8_t)m1, a);
        hexhound_sprite_compose((uint8_t)st, (uint8_t)m2, b);
        WL_CHECK(memcmp(a, b, sizeof a) != 0);
      }
}

WL_TEST(hexhound_sprite_mood_markers) {
  uint8_t g[HEX_SPRITE_N][HEX_SPRITE_N];
  hexhound_sprite_compose(2, HXM_SLEEPY, g);
  WL_CHECK_EQ(count(g, HXP_WHITE), 8);                         // the Z, no open-eye catch-lights
  hexhound_sprite_compose(2, HXM_EXCITED, g);
  WL_CHECK(count(g, HXP_YELLOW) >= 4);                         // sparkle
  hexhound_sprite_compose(3, HXM_WARY, g);
  WL_CHECK_EQ((int)g[0][7], (int)HXP_PINK);                    // beacon turns red on a threat
  hexhound_sprite_compose(3, HXM_IDLE, g);
  WL_CHECK_EQ((int)g[0][7], (int)HXP_YELLOW);
}

WL_TEST(hexhound_sprite_out_of_range_inputs_clamp) {
  uint8_t a[HEX_SPRITE_N][HEX_SPRITE_N], b[HEX_SPRITE_N][HEX_SPRITE_N];
  hexhound_sprite_compose(0, HXM_IDLE, a);  hexhound_sprite_compose(1, HXM_IDLE, b);
  WL_CHECK(memcmp(a, b, sizeof a) == 0);
  hexhound_sprite_compose(9, HXM_IDLE, a);  hexhound_sprite_compose(5, HXM_IDLE, b);
  WL_CHECK(memcmp(a, b, sizeof a) == 0);
  hexhound_sprite_compose(2, 200, a);       hexhound_sprite_compose(2, HXM_IDLE, b);
  WL_CHECK(memcmp(a, b, sizeof a) == 0);
}

WL_TEST(hexhound_sprite_palette_follows_the_accent_only_for_the_body) {
  uint32_t o[HXP_COUNT], p[HXP_COUNT];
  hexhound_sprite_palette(0xFF5A1A, o);
  hexhound_sprite_palette(0xB05CFF, p);
  WL_CHECK_EQ(o[HXP_NONE], 0u);                                // transparent
  WL_CHECK_EQ(o[HXP_BODY], 0xFFFF5A1Au);
  WL_CHECK_EQ(p[HXP_BODY], 0xFFB05CFFu);
  WL_CHECK(o[HXP_LIGHT] != p[HXP_LIGHT] && o[HXP_DARK] != p[HXP_DARK]);
  for (int c = HXP_INK; c < HXP_COUNT; c++) WL_CHECK_EQ(o[c], p[c]);   // face colours fixed
  // Light is lighter and dark is darker than the body, channel by channel.
  for (int sh = 0; sh <= 16; sh += 8) {
    uint32_t body = (o[HXP_BODY] >> sh) & 0xFF;
    WL_CHECK(((o[HXP_LIGHT] >> sh) & 0xFF) >= body);
    WL_CHECK(((o[HXP_DARK]  >> sh) & 0xFF) <= body);
  }
}
