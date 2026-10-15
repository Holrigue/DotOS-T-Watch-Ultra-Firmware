// music_lib.cpp - see music_lib.h.
#include "music_lib.h"
#include "music_path.h"
#include "music_tags.h"

#include <Arduino.h>
#include <SD.h>
#include <cstring>
#include <esp_heap_caps.h>

namespace {

constexpr const char *MUSIC_ROOT  = "/music";
constexpr int          MAX_TRACKS = 1024;   // generous; extra files are skipped

MusicTrack *s_tracks      = nullptr;   // PSRAM, s_track_count entries
int         s_track_count = 0;
bool        s_scanned     = false;

// One reordering view per browse mode: order[] (a permutation) + group_start[]
// (ngroups+1 boundaries). All PSRAM.
struct View { int *order = nullptr; int *group_start = nullptr; int ngroups = 0; };
View s_view[MUSIC_BROWSE_COUNT];

bool has_ext(const char *name, const char *ext)
{
    size_t nlen = strlen(name), elen = strlen(ext);
    if (nlen < elen) return false;
    return strcasecmp(name + (nlen - elen), ext) == 0;
}

const char *basename_of(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void free_index()
{
    if (s_tracks) { heap_caps_free(s_tracks); s_tracks = nullptr; }
    s_track_count = 0;
    for (int m = 0; m < MUSIC_BROWSE_COUNT; m++) {
        if (s_view[m].order)       heap_caps_free(s_view[m].order);
        if (s_view[m].group_start) heap_caps_free(s_view[m].group_start);
        s_view[m] = View{};
    }
}

// Tag-reader callback over the open SD file: absolute seek + read.
size_t file_read_at(void *ctx, uint32_t off, uint8_t *buf, size_t n)
{
    File *f = (File *)ctx;
    if (!f->seek(off)) return 0;
    return f->read(buf, n);
}

// Read tags for one file and fill the record; fall back to the path for anything
// the file left untagged.
void read_meta(const char *path, MusicTrack &t)
{
    MusicMeta meta; memset(&meta, 0, sizeof meta);
    File f = SD.open(path);
    if (f) {
        music_tags_read(file_read_at, &f, (uint32_t)f.size(), meta);
        f.close();
    }
    music_meta_fill_from_path(path, meta);
    snprintf(t.title,  sizeof t.title,  "%s", meta.title[0]  ? meta.title  : basename_of(path));
    snprintf(t.artist, sizeof t.artist, "%s", meta.artist);
    snprintf(t.album,  sizeof t.album,  "%s", meta.album);
    t.track = meta.track;
}

void add_file(const char *path)
{
    if (s_track_count >= MAX_TRACKS) return;
    MusicTrack &t = s_tracks[s_track_count];
    memset(&t, 0, sizeof t);
    snprintf(t.path, sizeof t.path, "%s", path);
    read_meta(path, t);
    s_track_count++;
}

// Index the supported files directly inside `dirpath`, and recurse ONE level into
// its subfolders (so /music/Artist/track and /music/Artist/Album/track are both
// found). `depth` stops the recursion at two levels below /music.
void scan_dir(const char *dirpath, int depth)
{
    File dir = SD.open(dirpath);
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
    for (File e = dir.openNextFile(); e && s_track_count < MAX_TRACKS; e = dir.openNextFile()) {
        char child[MUSIC_PATH_LEN];
        bool is_dir = e.isDirectory();
        bool ok = music_join_path(dirpath, e.name(), child, sizeof child);
        e.close();
        if (!ok) continue;
        if (is_dir) { if (depth > 0) scan_dir(child, depth - 1); }
        else if (music_lib_is_supported_file(child)) add_file(child);
    }
    dir.close();
}

void build_views()
{
    for (int m = 0; m < MUSIC_BROWSE_COUNT; m++) {
        View &v = s_view[m];
        if (s_track_count <= 0) continue;
        v.order       = (int *)heap_caps_malloc(sizeof(int) * s_track_count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        v.group_start = (int *)heap_caps_malloc(sizeof(int) * (s_track_count + 1), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!v.order || !v.group_start) { if (v.order) heap_caps_free(v.order); if (v.group_start) heap_caps_free(v.group_start); v = View{}; continue; }
        v.ngroups = music_build_order(s_tracks, s_track_count, (MusicBrowse)m, v.order, v.group_start, s_track_count);
    }
}

}  // namespace

bool music_lib_is_supported_file(const char *path)
{
    return has_ext(path, ".mp3") || has_ext(path, ".flac");
}

int music_lib_scan()
{
    free_index();
    s_scanned = true;

    File root = SD.open(MUSIC_ROOT);
    if (!root || !root.isDirectory()) { if (root) root.close(); return 0; }
    root.close();

    s_tracks = (MusicTrack *)heap_caps_malloc(sizeof(MusicTrack) * MAX_TRACKS, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_tracks) return 0;

    scan_dir(MUSIC_ROOT, 1);   // /music and one level of subfolders
    build_views();
    return s_track_count;
}

bool music_lib_is_scanned() { return s_scanned; }

int               music_lib_track_count()      { return s_track_count; }
const MusicTrack *music_lib_track(int index)
{
    if (index < 0 || index >= s_track_count) return nullptr;
    return &s_tracks[index];
}

int music_lib_group_count(MusicBrowse mode)
{
    if (mode >= MUSIC_BROWSE_COUNT) return 0;
    return s_view[mode].ngroups;
}

const char *music_lib_group_name(MusicBrowse mode, int group)
{
    if (mode >= MUSIC_BROWSE_COUNT) return "";
    const View &v = s_view[mode];
    if (group < 0 || group >= v.ngroups || !v.order) return "";
    const MusicTrack &t = s_tracks[v.order[v.group_start[group]]];
    return mode == MUSIC_BY_ALBUM ? t.album : t.artist;   // BY_TRACK groups has no name
}

int music_lib_group_track_count(MusicBrowse mode, int group)
{
    if (mode >= MUSIC_BROWSE_COUNT) return 0;
    const View &v = s_view[mode];
    if (group < 0 || group >= v.ngroups || !v.group_start) return 0;
    return v.group_start[group + 1] - v.group_start[group];
}

int music_lib_group_track(MusicBrowse mode, int group, int i)
{
    if (mode >= MUSIC_BROWSE_COUNT) return -1;
    const View &v = s_view[mode];
    if (group < 0 || group >= v.ngroups || !v.order) return -1;
    int start = v.group_start[group], cnt = v.group_start[group + 1] - start;
    if (i < 0 || i >= cnt) return -1;
    return v.order[start + i];
}
