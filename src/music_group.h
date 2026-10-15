#pragma once
//
// music_group.h - the flat track record and the browse ordering, both pure and
// host-tested (test/test_music_group.cpp). music_build_order() reorders indices
// into the flat track list for a browse mode; it never copies a track, so the
// three views cost only their index arrays.
#include <cstdint>
#include <cstring>
#include <strings.h>   // strcasecmp

#define MUSIC_TITLE_LEN  64   // track title (from tags, else the file name)
#define MUSIC_ARTIST_LEN 48
#define MUSIC_ALBUM_LEN  64
#define MUSIC_PATH_LEN   256  // full SD path, ready to hand to SD.open()

struct MusicTrack {
    char     title[MUSIC_TITLE_LEN];
    char     artist[MUSIC_ARTIST_LEN];
    char     album[MUSIC_ALBUM_LEN];
    char     path[MUSIC_PATH_LEN];
    uint16_t track;        // tag track number, 0 if unknown (for in-album ordering)
};

enum MusicBrowse : uint8_t { MUSIC_BY_ARTIST = 0, MUSIC_BY_ALBUM, MUSIC_BY_TRACK, MUSIC_BROWSE_COUNT };

namespace music_group_detail {

// Case-insensitive string compare that orders "" (unknown) last, so an untagged
// pile does not sit above named artists/albums.
inline int cmp_name(const char *a, const char *b)
{
    bool ea = !a || !*a, eb = !b || !*b;
    if (ea || eb) return ea == eb ? 0 : (ea ? 1 : -1);
    return strcasecmp(a, b);
}

// The field a mode groups and primary-sorts on.
inline const char *group_key(const MusicTrack &t, MusicBrowse mode)
{
    return mode == MUSIC_BY_ALBUM ? t.album : t.artist;   // BY_TRACK: one group (handled by caller)
}

// Order comparison between two tracks for a mode. Returns <0 / 0 / >0.
inline int cmp_track(const MusicTrack &x, const MusicTrack &y, MusicBrowse mode)
{
    int c;
    if (mode == MUSIC_BY_TRACK) {
        if ((c = cmp_name(x.title, y.title))) return c;
        return cmp_name(x.artist, y.artist);
    }
    if (mode == MUSIC_BY_ARTIST) {
        if ((c = cmp_name(x.artist, y.artist))) return c;
        if ((c = cmp_name(x.album, y.album)))   return c;
    } else {                                   // MUSIC_BY_ALBUM
        if ((c = cmp_name(x.album, y.album)))   return c;
    }
    if (x.track != y.track) return (int)x.track - (int)y.track;   // disc order within an album
    return cmp_name(x.title, y.title);
}

} // namespace music_group_detail

// Builds `order` (a permutation of [0,n)) grouped and sorted for `mode`, and
// `group_start` with the boundary index of each group plus a final entry == n
// (so group g spans [group_start[g], group_start[g+1])). Returns the group
// count, clamped so it never writes past group_start[max_groups]. For
// MUSIC_BY_TRACK there is one group holding everything. Stable insertion sort -
// n is in the hundreds and this runs once per scan.
inline int music_build_order(const MusicTrack *tracks, int n, MusicBrowse mode,
                             int *order, int *group_start, int max_groups)
{
    using namespace music_group_detail;
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 1; i < n; i++) {               // stable insertion sort on `order`
        int v = order[i], j = i - 1;
        while (j >= 0 && cmp_track(tracks[order[j]], tracks[v], mode) > 0) { order[j + 1] = order[j]; j--; }
        order[j + 1] = v;
    }

    int ng = 0;
    if (n <= 0) { if (max_groups > 0) group_start[0] = 0; return 0; }
    if (mode == MUSIC_BY_TRACK) {
        group_start[0] = 0;
        if (max_groups > 1) group_start[1] = n;
        return 1;
    }
    group_start[0] = 0; ng = 1;
    for (int i = 1; i < n && ng < max_groups; i++) {
        if (cmp_name(group_key(tracks[order[i]], mode), group_key(tracks[order[i - 1]], mode)) != 0)
            group_start[ng++] = i;
    }
    group_start[ng] = n;
    return ng;
}
