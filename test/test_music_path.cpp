// test_music_path.cpp - host tests for the music scan's path joining (src/music_path.h).
#include "wl_test.h"
#include "music_path.h"

#include <cstring>

WL_TEST(music_path_joins_a_base_name) {
  char p[160];
  WL_CHECK(music_join_path("/music", "King Gizzard & The Lizard Wizard - Nonagon Infinity", p, sizeof p));
  WL_CHECK(strcmp(p, "/music/King Gizzard & The Lizard Wizard - Nonagon Infinity") == 0);
}

WL_TEST(music_path_accepts_a_full_path_from_an_older_core) {
  char p[160];
  WL_CHECK(music_join_path("/music", "/music/Daft Punk", p, sizeof p));
  WL_CHECK(strcmp(p, "/music/Daft Punk") == 0);        // not "/music//music/Daft Punk"
}

WL_TEST(music_path_nested_track_path) {
  char p[160];
  WL_CHECK(music_join_path("/music/Artist", "01 Intro.mp3", p, sizeof p));
  WL_CHECK(strcmp(p, "/music/Artist/01 Intro.mp3") == 0);
}

WL_TEST(music_path_refuses_what_does_not_fit) {
  char p[16];
  WL_CHECK(!music_join_path("/music", "a very long folder name", p, sizeof p));
  WL_CHECK_EQ((int)strlen(p), 0);                      // never a truncated path
  WL_CHECK(music_join_path("/music", "short", p, sizeof p));
}

WL_TEST(music_path_rejects_empty_input) {
  char p[32];
  WL_CHECK(!music_join_path("/music", "", p, sizeof p));
  WL_CHECK(!music_join_path("/music", "/music/", p, sizeof p));
  WL_CHECK(!music_join_path(nullptr, "x", p, sizeof p));
  WL_CHECK(!music_join_path("/music", nullptr, p, sizeof p));
  WL_CHECK(!music_join_path("/music", "x", p, 0));
}
