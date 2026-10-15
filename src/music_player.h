#pragma once
//
// music_player.h - streaming MP3/FLAC playback over the shared I2S speaker.
//
// One FreeRTOS task (same pattern as alarm.cpp's chime task) decodes the
// current track a chunk at a time via minimp3 (MP3) or dr_flac (FLAC) -
// chosen by extension - and writes interleaved 16-bit PCM to instance.player.
// Only one track plays at a time; starting a new one stops whatever was
// playing first. Since the alarm chime and this player share the one I2S
// peripheral, playback refuses to start while alarm_chime_is_active().
#include <cstdint>
#include "music_group.h"   // MusicBrowse

// Start playing the `pos`-th track of browse group `group` in `mode` (the list
// the user is looking at becomes the play queue; next/prev walk it). music_lib_
// scan() must have run first. Stops any track already playing. Returns false if
// the position is out of range, the file can't be opened/decoded, or the I2S
// output is busy with the alarm chime.
bool music_player_play(MusicBrowse mode, int group, int pos);

void music_player_pause();     // no-op if not playing
void music_player_resume();    // no-op if not paused
void music_player_stop();      // stop + release the I2S output

// Move to the next/previous track within the browsed group that started this
// queue. No-op past either end (v1 doesn't loop/shuffle). Returns false if
// there is no current track to move from.
bool music_player_next();
bool music_player_prev();

// ---- volume (0..100) --------------------------------------------------------
// The MAX98357A has no hardware volume, so the player scales each PCM sample by
// this before writing it (0 = silent, 100 = unchanged). Applied live. Saved to
// /Settings/music.txt and restored at first use.
void music_player_set_volume(int pct);
int  music_player_get_volume();

bool music_player_is_playing();   // decoding + writing PCM right now
bool music_player_is_paused();    // a track is loaded but held

// Why the last play() failed or a track died at once ("Not enough memory", "Can't open the
// file", ...); empty when the last attempt was fine. For the screen and the serial log.
const char *music_player_last_error();

// Empty string when nothing is loaded.
const char *music_player_current_title();
const char *music_player_current_artist();

// Elapsed / total, in seconds. 0/0 when nothing is loaded. Polled by the Now
// Playing screen for its progress bar (no push callback - matches the 1 Hz
// tick-poll pattern used elsewhere, e.g. the Dot face badges).
float music_player_position_s();
float music_player_duration_s();
