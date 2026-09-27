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

// Start playing artist_idx/track_idx from the current music_lib index
// (music_lib_scan() must have run first). Stops any track already playing.
// Returns false if the index is out of range, the file can't be opened/
// decoded, or the I2S output is busy with the alarm chime.
bool music_player_play(int artist_idx, int track_idx);

void music_player_pause();     // no-op if not playing
void music_player_resume();    // no-op if not paused
void music_player_stop();      // stop + release the I2S output

// Move to the next/previous track within the current artist's list. No-op
// (stays on the current track) past either end of the list - v1 doesn't
// loop or shuffle. Returns false if there is no current track to move from.
bool music_player_next();
bool music_player_prev();

bool music_player_is_playing();   // decoding + writing PCM right now
bool music_player_is_paused();    // a track is loaded but held

// Empty string when nothing is loaded.
const char *music_player_current_title();
const char *music_player_current_artist();

// Elapsed / total, in seconds. 0/0 when nothing is loaded. Polled by the Now
// Playing screen for its progress bar (no push callback - matches the 1 Hz
// tick-poll pattern used elsewhere, e.g. the Dot face badges).
float music_player_position_s();
float music_player_duration_s();
