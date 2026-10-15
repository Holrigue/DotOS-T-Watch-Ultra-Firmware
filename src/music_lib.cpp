// music_lib.cpp - see music_lib.h.
#include "music_lib.h"
#include "music_path.h"

#include <Arduino.h>
#include <SD.h>
#include <cstring>
#include <esp_heap_caps.h>

namespace {

constexpr const char *MUSIC_ROOT   = "/music";
constexpr int          MAX_ARTISTS = 64;   // generous; extra folders are skipped

MusicArtist *s_artists      = nullptr;   // PSRAM array, s_artist_count entries
int          s_artist_count = 0;
bool         s_scanned      = false;

// Case-insensitive suffix match, e.g. has_ext("Song.MP3", ".mp3") -> true.
bool has_ext(const char *name, const char *ext)
{
    size_t nlen = strlen(name), elen = strlen(ext);
    if (nlen < elen) return false;
    return strcasecmp(name + (nlen - elen), ext) == 0;
}

void free_index()
{
    if (!s_artists) return;
    for (int i = 0; i < s_artist_count; i++) {
        if (s_artists[i].tracks) heap_caps_free(s_artists[i].tracks);
    }
    heap_caps_free(s_artists);
    s_artists      = nullptr;
    s_artist_count = 0;
}

// basename idiom shared with loot_screen.cpp / background.cpp: the display name is
// whatever follows the last '/' (File::name() is already a base name on this core, a
// full path on older ones). Also strips the extension for the track title.
const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void title_from_filename(const char *path, char *out, size_t out_len)
{
    const char *base = basename_of(path);
    const char *dot  = strrchr(base, '.');
    size_t      len  = dot ? (size_t)(dot - base) : strlen(base);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, base, len);
    out[len] = '\0';
}

// Counts supported audio files directly inside `dirpath` (one level, no
// recursion - everything under an artist folder is that artist's tracks,
// flat or not doesn't matter since we only look at direct children here by
// design: nested sub-albums would need recursion we deliberately skip for
// v1's simplicity).
int count_tracks(File &dir)
{
    int n = 0;
    dir.rewindDirectory();
    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
        if (!e.isDirectory() && music_lib_is_supported_file(e.name())) n++;
        e.close();
    }
    return n;
}

// Fills `artist` with up to its pre-sized track array's worth of entries.
void fill_tracks(File &dir, MusicArtist &artist, const char *artist_path)
{
    dir.rewindDirectory();
    int i = 0;
    for (File e = dir.openNextFile(); e && i < artist.track_count; e = dir.openNextFile()) {
        if (!e.isDirectory() && music_lib_is_supported_file(e.name())) {
            MusicTrack &t = artist.tracks[i];
            title_from_filename(e.name(), t.title, sizeof(t.title));
            if (!music_join_path(artist_path, e.name(), t.path, sizeof(t.path))) { e.close(); continue; }
            i++;
        }
        e.close();
    }
    artist.track_count = i;   // in case anything failed to open between passes
}

}  // namespace

bool music_lib_is_supported_file(const char *path)
{
    return has_ext(path, ".mp3") || has_ext(path, ".flac");
}

int music_lib_scan()
{
    free_index();
    s_scanned = true;   // scanned, even if /music turns out to be empty/absent

    File root = SD.open(MUSIC_ROOT);
    if (!root || !root.isDirectory()) { if (root) root.close(); return 0; }

    s_artists = (MusicArtist *)heap_caps_malloc(
        sizeof(MusicArtist) * MAX_ARTISTS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_artists) { root.close(); return 0; }   // PSRAM exhausted; bail cleanly

    for (File e = root.openNextFile(); e && s_artist_count < MAX_ARTISTS;
         e = root.openNextFile()) {
        if (!e.isDirectory()) { e.close(); continue; }   // stray file at /music root

        // File::name() is the BASE name on this core, so join it to /music (see music_path.h).
        char artist_path[MUSIC_PATH_LEN];
        if (!music_join_path(MUSIC_ROOT, e.name(), artist_path, sizeof(artist_path))) { e.close(); continue; }

        File adir = SD.open(artist_path);
        if (!adir) { e.close(); continue; }

        int n = count_tracks(adir);
        if (n > 0) {
            MusicArtist &a = s_artists[s_artist_count];
            snprintf(a.name, sizeof(a.name), "%s", basename_of(artist_path));
            a.tracks = (MusicTrack *)heap_caps_malloc(
                sizeof(MusicTrack) * n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (a.tracks) {
                a.track_count = n;
                fill_tracks(adir, a, artist_path);
                s_artist_count++;
            }
        }
        adir.close();
        e.close();
    }
    root.close();
    return s_artist_count;
}

bool music_lib_is_scanned() { return s_scanned; }

int music_lib_artist_count() { return s_artist_count; }

const MusicArtist *music_lib_artist(int index)
{
    if (index < 0 || index >= s_artist_count) return nullptr;
    return &s_artists[index];
}
