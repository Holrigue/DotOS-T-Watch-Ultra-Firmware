#pragma once
// music_path.h - builds "<dir>/<entry>" for the music scan. Header-only and free of
// any hardware, so it is covered by the host tests (test/test_music_path.cpp).
//
// Why this exists: File::name() is not the same on every Arduino core. Old cores
// returned the FULL path, the ESP32 core used here (arduino-esp32 2.x) returns only
// the BASE name. The scan used to treat it as a full path, so on this core the artist
// folder was opened as "King Gizzard..." at the SD root instead of "/music/King
// Gizzard...", failed, and every folder was skipped: "No music found". Taking the
// part after the last '/' makes the result right for both behaviours.
#include <cstddef>
#include <cstdio>
#include <cstring>

// Writes "<dir>/<last component of entry>" into out. Returns false (and leaves out
// empty) if it does not fit, so a caller skips the entry instead of opening a
// silently truncated path.
inline bool music_join_path(const char *dir, const char *entry, char *out, size_t out_len)
{
    if (!out || out_len == 0) return false;
    out[0] = '\0';
    if (!dir || !entry) return false;
    const char *slash = strrchr(entry, '/');
    const char *base  = slash ? slash + 1 : entry;
    if (!*base) return false;
    int n = snprintf(out, out_len, "%s/%s", dir, base);
    if (n < 0 || (size_t)n >= out_len) { out[0] = '\0'; return false; }
    return true;
}
