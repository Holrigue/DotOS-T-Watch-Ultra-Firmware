// test_music_group.cpp - host tests for the browse ordering (src/music_group.h).
#include "wl_test.h"
#include "music_group.h"
#include <cstring>

static MusicTrack mk(const char *title, const char *artist, const char *album, uint16_t tn) {
    MusicTrack t; memset(&t, 0, sizeof t);
    strncpy(t.title, title, sizeof t.title - 1);
    strncpy(t.artist, artist, sizeof t.artist - 1);
    strncpy(t.album, album, sizeof t.album - 1);
    t.track = tn;
    return t;
}

WL_TEST(music_group_by_artist_groups_and_orders) {
    MusicTrack T[] = {
        mk("Robot Stop",   "King Gizzard", "Nonagon", 1),
        mk("Gamma Knife",  "King Gizzard", "Nonagon", 3),   // deliberately out of order
        mk("Rattlesnake",  "King Gizzard", "Microtonal", 1),
        mk("Aisha",        "Death Grips", "Bottomless", 2),
    };
    int n = 4, order[4], gs[8];
    int ng = music_build_order(T, n, MUSIC_BY_ARTIST, order, gs, 8);
    WL_CHECK_EQ(ng, 2);                                 // Death Grips, King Gizzard
    WL_CHECK(strcmp(T[order[gs[0]]].artist, "Death Grips") == 0);
    // Within King Gizzard: album alpha (Microtonal before Nonagon), then track no.
    WL_CHECK(strcmp(T[order[gs[1] + 0]].album, "Microtonal") == 0);
    WL_CHECK(strcmp(T[order[gs[1] + 1]].title, "Robot Stop") == 0);   // Nonagon #1
    WL_CHECK(strcmp(T[order[gs[1] + 2]].title, "Gamma Knife") == 0);  // Nonagon #3
    WL_CHECK_EQ(gs[ng], n);
}

WL_TEST(music_group_by_album) {
    MusicTrack T[] = {
        mk("B", "X", "Alpha", 2),
        mk("A", "Y", "Alpha", 1),
        mk("C", "Z", "Beta",  1),
    };
    int order[3], gs[8];
    int ng = music_build_order(T, 3, MUSIC_BY_ALBUM, order, gs, 8);
    WL_CHECK_EQ(ng, 2);
    WL_CHECK(strcmp(T[order[gs[0] + 0]].title, "A") == 0);   // Alpha #1
    WL_CHECK(strcmp(T[order[gs[0] + 1]].title, "B") == 0);   // Alpha #2
    WL_CHECK(strcmp(T[order[gs[1]]].album, "Beta") == 0);
}

WL_TEST(music_group_by_track_is_one_group_title_sorted) {
    MusicTrack T[] = { mk("Zephyr","A","X",0), mk("alpha","B","Y",0), mk("Mango","C","Z",0) };
    int order[3], gs[8];
    int ng = music_build_order(T, 3, MUSIC_BY_TRACK, order, gs, 8);
    WL_CHECK_EQ(ng, 1);
    WL_CHECK(strcasecmp(T[order[0]].title, "alpha") == 0);
    WL_CHECK(strcasecmp(T[order[1]].title, "Mango") == 0);
    WL_CHECK(strcasecmp(T[order[2]].title, "Zephyr") == 0);
    WL_CHECK_EQ(gs[1], 3);
}

WL_TEST(music_group_unknown_sorts_last) {
    MusicTrack T[] = { mk("S1","", "", 0), mk("S2","Alpha","Al",0) };
    int order[2], gs[8];
    int ng = music_build_order(T, 2, MUSIC_BY_ARTIST, order, gs, 8);
    WL_CHECK_EQ(ng, 2);
    WL_CHECK(strcmp(T[order[gs[0]]].artist, "Alpha") == 0);   // named first
    WL_CHECK_EQ((int)T[order[gs[1]]].artist[0], 0);           // unknown last
}

WL_TEST(music_group_empty) {
    int order[1], gs[2];
    WL_CHECK_EQ(music_build_order(nullptr, 0, MUSIC_BY_ARTIST, order, gs, 2), 0);
}
