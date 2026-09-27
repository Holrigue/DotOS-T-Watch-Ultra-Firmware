// music_player.cpp - see music_player.h.
//
// Decode + playback task, patterned directly on alarm.cpp's chime_task: one
// FreeRTOS task pinned to core 0 owns the shared I2S output (instance.player /
// I2S_NUM_1) for as long as a track is loaded, powering the MAX98357A amp up
// on entry and down on exit via instance.powerControl(POWER_SPEAK, ...).
//
// MP3 and FLAC decode both stream straight from an SD `File` through their
// respective vendored single-header decoders (lib/minimp3, lib/dr_flac) and
// come out as interleaved signed 16-bit PCM, matching what the I2S output
// already expects (no format conversion needed, unlike a float-PCM decoder
// would require).
#include "music_player.h"
#include "music_lib.h"
#include "alarm.h"   // alarm_chime_is_active() - shared-I2S interlock

#include <Arduino.h>
#include <SD.h>
#include <LilyGoLib.h>
#include <driver/i2s.h>
#include <cstring>

// ---- vendored decoders -------------------------------------------------------
// MINIMP3_IMPLEMENTATION/DR_FLAC_IMPLEMENTATION must be defined in exactly one
// translation unit (else duplicate-symbol link errors) - this is that unit;
// nothing else in the firmware includes these headers.
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3    // strip MP1/MP2 decode - we only ever feed .mp3
#define MINIMP3_NO_SIMD     // Xtensa has none of minimp3's x86/ARM SIMD paths
#include <minimp3.h>
#include <minimp3_ex.h>

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO   // we stream via the SD File API ourselves
#define DR_FLAC_NO_OGG     // never see FLAC-in-Ogg on this player
#include <dr_flac.h>

namespace {

enum class Kind { None, Mp3, Flac };

constexpr uint32_t PLAYER_I2S_PORT     = I2S_NUM_1;   // matches alarm.cpp
constexpr int       PCM_CHUNK_FRAMES   = 1152;         // one MP3 frame's worth
constexpr size_t    PCM_CHUNK_SAMPLES  = PCM_CHUNK_FRAMES * 2;   // stereo-worst-case

Kind          s_kind = Kind::None;
File          s_file;                  // backs whichever decoder is open
mp3dec_ex_t  *s_mp3  = nullptr;         // heap: large struct, keep off the task stack
drflac       *s_flac = nullptr;

int      s_channels    = 0;
uint32_t s_sample_rate = 0;
uint64_t s_total_frames = 0;
volatile uint64_t s_cur_frame = 0;      // written by the task, read by the UI poll

int  s_artist_idx = -1;
int  s_track_idx  = -1;
char s_title[MUSIC_TITLE_LEN]   = "";
char s_artist[MUSIC_ARTIST_LEN] = "";

TaskHandle_t      s_task = nullptr;
volatile bool     s_running = false;    // task should keep decoding/writing
volatile bool     s_paused  = false;
SemaphoreHandle_t s_pause_sem = nullptr;   // given to wake a paused task on resume/stop

// ---- minimp3 I/O callbacks: read/seek against the open SD File -------------
size_t mp3_read_cb(void *buf, size_t size, void *user_data)
{
    File *f = (File *)user_data;
    return f->read((uint8_t *)buf, size);
}
int mp3_seek_cb(uint64_t position, void *user_data)
{
    File *f = (File *)user_data;
    return f->seek(position) ? 0 : -1;
}

// Case-insensitive suffix match - local copy of music_lib.cpp's has_ext(),
// kept self-contained here rather than shared across a one-line helper.
bool has_ext(const char *name, const char *ext)
{
    size_t nlen = strlen(name), elen = strlen(ext);
    if (nlen < elen) return false;
    return strcasecmp(name + (nlen - elen), ext) == 0;
}

// ---- dr_flac I/O callbacks: same File, dr_flac's own calling convention ----
size_t flac_read_cb(void *user_data, void *buf, size_t bytes)
{
    File *f = (File *)user_data;
    return f->read((uint8_t *)buf, bytes);
}
drflac_bool32 flac_seek_cb(void *user_data, int offset, drflac_seek_origin origin)
{
    File *f = (File *)user_data;
    size_t pos = (origin == DRFLAC_SEEK_SET)
                     ? (size_t)offset
                     : (size_t)((int)f->position() + offset);
    return f->seek(pos) ? DRFLAC_TRUE : DRFLAC_FALSE;
}

void close_decoder()
{
    if (s_mp3)  { mp3dec_ex_close(s_mp3); free(s_mp3); s_mp3 = nullptr; }
    if (s_flac) { drflac_close(s_flac);   s_flac = nullptr; }
    if (s_file) s_file.close();
    s_kind = Kind::None;
    s_channels = 0; s_sample_rate = 0; s_total_frames = 0; s_cur_frame = 0;
}

// Opens `path` and detects MP3 vs FLAC by extension. On success fills in
// channels/sample_rate/total_frames and leaves the matching decoder live.
bool open_decoder(const char *path)
{
    close_decoder();
    s_file = SD.open(path);
    if (!s_file) return false;

    if (has_ext(path, ".flac")) {
        s_flac = drflac_open(flac_read_cb, flac_seek_cb, nullptr, &s_file, nullptr);
        if (!s_flac) { s_file.close(); return false; }
        s_kind          = Kind::Flac;
        s_channels      = s_flac->channels;
        s_sample_rate   = s_flac->sampleRate;
        s_total_frames  = s_flac->totalPCMFrameCount;
    } else {
        s_mp3 = (mp3dec_ex_t *)malloc(sizeof(mp3dec_ex_t));
        if (!s_mp3) { s_file.close(); return false; }
        mp3dec_io_t io;
        io.read = mp3_read_cb; io.read_data = &s_file;
        io.seek = mp3_seek_cb; io.seek_data = &s_file;
        if (mp3dec_ex_open_cb(s_mp3, &io, MP3D_SEEK_TO_SAMPLE) != 0) {
            free(s_mp3); s_mp3 = nullptr; s_file.close(); return false;
        }
        s_kind          = Kind::Mp3;
        s_channels      = s_mp3->info.channels;
        s_sample_rate   = s_mp3->info.hz;
        // mp3dec_ex reports total interleaved samples; divide out channels for
        // a frame count (one frame = one sample per channel), matching FLAC's
        // totalPCMFrameCount so the rest of this file need not care which
        // decoder is live.
        s_total_frames  = s_channels ? (s_mp3->samples / (uint64_t)s_channels) : 0;
    }
    if (s_channels <= 0 || s_channels > 2 || s_sample_rate == 0) {
        close_decoder();   // something we can't play (e.g. >2 channels)
        return false;
    }
    return true;
}

// Decodes up to PCM_CHUNK_FRAMES frames into `out` (interleaved s16). Returns
// frames actually produced; 0 at end of stream or on decode error.
size_t decode_chunk(int16_t *out)
{
    if (s_kind == Kind::Mp3) {
        size_t samples = mp3dec_ex_read(s_mp3, out, (size_t)PCM_CHUNK_FRAMES * s_channels);
        size_t frames  = samples / (size_t)s_channels;
        s_cur_frame += frames;
        return frames;
    }
    if (s_kind == Kind::Flac) {
        drflac_uint64 frames = drflac_read_pcm_frames_s16(s_flac, PCM_CHUNK_FRAMES, out);
        s_cur_frame += frames;
        return (size_t)frames;
    }
    return 0;
}

void player_task(void *)
{
    instance.powerControl(POWER_SPEAK, true);
    vTaskDelay(pdMS_TO_TICKS(20));   // let the amp settle, same as the chime task
    i2s_set_clk(PLAYER_I2S_PORT, s_sample_rate, I2S_BITS_PER_SAMPLE_16BIT,
                s_channels == 2 ? I2S_CHANNEL_STEREO : I2S_CHANNEL_MONO);

    int16_t *chunk = (int16_t *)malloc(PCM_CHUNK_SAMPLES * sizeof(int16_t));
    if (chunk) {
        while (s_running) {
            if (s_paused) {
                // Block until resumed or stopped; the amp stays powered (a
                // paused track resumes instantly) but nothing is written, so
                // I2S just idles - matches how the chime task's own while-loop
                // condition is the only thing gating writes.
                xSemaphoreTake(s_pause_sem, portMAX_DELAY);
                continue;
            }
            size_t frames = decode_chunk(chunk);
            if (frames == 0) { s_running = false; break; }   // EOF or decode error
            instance.player.write(chunk, frames * s_channels * sizeof(int16_t));
        }
        free(chunk);
    }

    instance.powerControl(POWER_SPEAK, false);
    close_decoder();
    s_artist_idx = -1; s_track_idx = -1;
    s_title[0] = '\0'; s_artist[0] = '\0';
    s_task = nullptr;
    vTaskDelete(NULL);
}

void stop_task_and_wait()
{
    if (!s_task) return;
    s_running = false;
    if (s_paused) { s_paused = false; xSemaphoreGive(s_pause_sem); }   // unblock it
    // The task frees s_task itself on exit; briefly yield so a fast
    // stop-then-play doesn't race a still-tearing-down previous task for the
    // I2S peripheral.
    while (s_task) vTaskDelay(pdMS_TO_TICKS(5));
}

}  // namespace

bool music_player_play(int artist_idx, int track_idx)
{
    const MusicArtist *a = music_lib_artist(artist_idx);
    if (!a || track_idx < 0 || track_idx >= a->track_count) return false;
    if (alarm_chime_is_active()) return false;   // I2S is busy; try again shortly

    stop_task_and_wait();

    if (!open_decoder(a->tracks[track_idx].path)) return false;

    s_artist_idx = artist_idx;
    s_track_idx  = track_idx;
    snprintf(s_title,  sizeof(s_title),  "%s", a->tracks[track_idx].title);
    snprintf(s_artist, sizeof(s_artist), "%s", a->name);

    if (!s_pause_sem) s_pause_sem = xSemaphoreCreateBinary();
    s_running = true;
    s_paused  = false;
    xTaskCreatePinnedToCore(player_task, "music_player", 8192, NULL, 1, &s_task, 0);
    return true;
}

void music_player_pause()
{
    if (s_task && s_running && !s_paused) s_paused = true;
}

void music_player_resume()
{
    if (s_task && s_running && s_paused) {
        s_paused = false;
        xSemaphoreGive(s_pause_sem);
    }
}

void music_player_stop()
{
    stop_task_and_wait();
}

bool music_player_next()
{
    if (s_artist_idx < 0) return false;
    const MusicArtist *a = music_lib_artist(s_artist_idx);
    if (!a || s_track_idx + 1 >= a->track_count) return false;
    return music_player_play(s_artist_idx, s_track_idx + 1);
}

bool music_player_prev()
{
    if (s_artist_idx < 0 || s_track_idx <= 0) return false;
    return music_player_play(s_artist_idx, s_track_idx - 1);
}

bool music_player_is_playing() { return s_task != nullptr && s_running && !s_paused; }
bool music_player_is_paused()  { return s_task != nullptr && s_running && s_paused; }

const char *music_player_current_title()  { return s_title; }
const char *music_player_current_artist() { return s_artist; }

float music_player_position_s()
{
    return s_sample_rate ? (float)s_cur_frame / (float)s_sample_rate : 0.0f;
}
float music_player_duration_s()
{
    return s_sample_rate ? (float)s_total_frames / (float)s_sample_rate : 0.0f;
}
