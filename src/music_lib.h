#pragma once
//
// music_lib.h - indexes music files on the SD card for the Music tool.
//
// Convention: /music/<Artist>/<anything>.mp3|.flac - the folder name IS the
// artist. This is the simplest, most robust "browse by artist" scheme for an
// embedded player with no ID3/Vorbis-comment tag-parsing budget: it needs no
// tag library, survives files with missing/garbled tags, and matches how most
// people already organise a music folder by hand. The track title shown is
// the filename without its extension.
//
// Everything the scan builds lives in PSRAM (heap_caps_malloc, MALLOC_CAP_
// SPIRAM) - same posture as the Dot watchface's dot-matrix raster - since the
// index can hold hundreds of entries and the watch's internal RAM is scarce.
#include <cstdint>

#define MUSIC_TITLE_LEN  48   // filename without extension, truncated if longer
#define MUSIC_PATH_LEN   256  // full SD path, ready to hand to SD.open()
#define MUSIC_ARTIST_LEN 40   // folder name, truncated if longer

struct MusicTrack {
    char title[MUSIC_TITLE_LEN];
    char path[MUSIC_PATH_LEN];
};

struct MusicArtist {
    char        name[MUSIC_ARTIST_LEN];
    MusicTrack *tracks;        // PSRAM array, `track_count` entries
    int         track_count;
};

// Scans /music once (call after SD.begin(), on first Tools > Music open - not
// at boot, so a watch that never opens Music never pays the SD-scan cost).
// Safe to call again to rescan (frees the previous index first, e.g. after
// the user swaps SD cards). Returns the artist count (0 if /music is absent,
// empty, or holds no folders with a supported file in them).
int music_lib_scan();

// True once music_lib_scan() has run at least once and found something.
bool music_lib_is_scanned();

int                music_lib_artist_count();
const MusicArtist *music_lib_artist(int index);   // nullptr if out of range

// True if `path`'s extension is one this player can decode (.mp3 or .flac,
// case-insensitive). Shared with music_player so the "what counts as a music
// file" rule lives in exactly one place.
bool music_lib_is_supported_file(const char *path);
