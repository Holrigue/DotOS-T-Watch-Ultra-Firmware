#pragma once
//
// music_lib.h - indexes music files on the SD card for the Music tool.
//
// The scan walks /music one and two levels deep (/music/<Artist>/track and
// /music/<Artist>/<Album>/track) and builds ONE flat PSRAM array of tracks.
// Each track carries its title, artist, album and track number, read from the
// file's tags (ID3v2 / ID3v1 for MP3, Vorbis comments for FLAC; see
// music_tags.h) and completed from the folder/file names for anything a file
// left untagged. The browse screens then present that one list three ways -
// by artist, by album, or as one flat track list - via music_build_order()
// (music_group.h, host-tested), which only reorders indices, never copies a
// track. Everything lives in PSRAM since the index can hold hundreds of
// entries and the watch's internal RAM is scarce.
#include <cstdint>
#include "music_group.h"   // MusicTrack, MusicBrowse, music_build_order()

// Scans /music once (call after SD.begin(), on first Music open - not at boot,
// so a watch that never opens Music never pays the SD-scan cost). Safe to call
// again to rescan. Returns the track count (0 if /music is absent or empty).
int music_lib_scan();

// True once music_lib_scan() has run at least once.
bool music_lib_is_scanned();

// The one flat track list.
int                music_lib_track_count();
const MusicTrack  *music_lib_track(int index);   // nullptr if out of range

// ---- grouped browse views (computed once per scan from the flat list) -------
// For BY_TRACK there is a single group holding every track, title-sorted.
int         music_lib_group_count(MusicBrowse mode);
const char *music_lib_group_name(MusicBrowse mode, int group);        // "" -> caller shows a placeholder
int         music_lib_group_track_count(MusicBrowse mode, int group);
int         music_lib_group_track(MusicBrowse mode, int group, int i); // global track index, -1 if out of range

// True if `path`'s extension is one this player can decode (.mp3 or .flac).
bool music_lib_is_supported_file(const char *path);
