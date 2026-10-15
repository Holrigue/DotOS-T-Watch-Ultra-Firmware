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
#include "music_duration.h"
#include "alarm.h"   // alarm_chime_is_active() - shared-I2S interlock
#include "usb_sd.h"  // usb_sd_is_running() - don't write the SD while it's a USB drive

#include <Arduino.h>
#include <SD.h>
#include <LilyGoLib.h>
#include <driver/i2s.h>
#include <cstring>
#include <esp_heap_caps.h>

// ---- vendored decoders -------------------------------------------------------
// MINIMP3_IMPLEMENTATION/DR_FLAC_IMPLEMENTATION must be defined in exactly one
// translation unit (else duplicate-symbol link errors) - this is that unit;
// nothing else in the firmware includes these headers.
#define MINIMP3_IMPLEMENTATION
// The decode scratch (~16 KB) comes from music_mp3_scratch(), not the task stack.
static void *music_mp3_scratch(void);
#define MINIMP3_EXTERNAL_SCRATCH music_mp3_scratch
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

constexpr i2s_port_t PLAYER_I2S_PORT   = I2S_NUM_1;   // matches alarm.cpp
constexpr int       PCM_CHUNK_FRAMES   = 1152;         // one MP3 frame's worth
constexpr size_t    PCM_CHUNK_SAMPLES  = PCM_CHUNK_FRAMES * 2;   // stereo-worst-case

Kind          s_kind = Kind::None;
File          s_file;                  // backs whichever decoder is open
mp3dec_ex_t  *s_mp3  = nullptr;         // heap: large struct, keep off the task stack
// minimp3 keeps a POINTER to this and calls through it on every read, so it must outlive
// open_decoder(). It used to be a local there: the first decode then called through a
// dangling stack pointer and the watch rebooted as soon as a track started.
mp3dec_io_t   s_mp3_io;
void         *s_scratch = nullptr;      // minimp3's decode scratch (internal RAM if it fits, else PSRAM)
const char   *s_last_error = "";        // why the last play() failed, for the screen / log
drflac       *s_flac = nullptr;

int      s_channels    = 0;
uint32_t s_sample_rate = 0;
uint64_t s_total_frames = 0;
volatile uint64_t s_cur_frame = 0;      // written by the task, read by the UI poll

// The browse list the user tapped becomes the play queue; next/prev walk it.
MusicBrowse s_mode  = MUSIC_BY_ARTIST;
int  s_group = -1;
int  s_pos   = -1;
char s_title[MUSIC_TITLE_LEN]   = "";
char s_artist[MUSIC_ARTIST_LEN] = "";

// Playback volume 0..100 (MAX98357A has no HW volume - we scale PCM). -1 until loaded.
volatile int s_volume = -1;
constexpr const char *MUSIC_CFG = "/Settings/music.txt";

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

char s_err_buf[96];

// Record why playback did not start. The numbers are on purpose: they show on the track
// list, so a photo of the screen tells us how much internal RAM was left.
void fail(const char *why)
{
    unsigned fr = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    unsigned bl = (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    snprintf(s_err_buf, sizeof(s_err_buf), "%s\nfree %uK, block %uK", why, fr / 1024, bl / 1024);
    s_last_error = s_err_buf;
    Serial.printf("[music] %s (internal free %u, largest block %u)\n", why, fr, bl);
}

// Internal RAM is what the watch is short of (the display's DMA buffers take 82 KB of it),
// so the player puts its big buffers there only when plenty is left over, and in PSRAM
// otherwise. The reserve keeps room for the task stack and for BLE/WiFi/SD.
constexpr size_t INTERNAL_RESERVE = 20 * 1024;

void *alloc_flex(size_t n)
{
    void *p = nullptr;
    if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= n + INTERNAL_RESERVE)
        p = heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p;
}

void close_decoder()
{
    if (s_scratch) { heap_caps_free(s_scratch); s_scratch = nullptr; }
    if (s_mp3)  { mp3dec_ex_close(s_mp3); heap_caps_free(s_mp3); s_mp3 = nullptr; }
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
    if (!s_file) { fail("Can't open the file"); return false; }

    if (has_ext(path, ".flac")) {
        s_flac = drflac_open(flac_read_cb, flac_seek_cb, nullptr, &s_file, nullptr);
        if (!s_flac) { s_file.close(); fail("Can't read this FLAC"); return false; }
        s_kind          = Kind::Flac;
        s_channels      = s_flac->channels;
        s_sample_rate   = s_flac->sampleRate;
        s_total_frames  = s_flac->totalPCMFrameCount;
    } else {
        s_mp3     = (mp3dec_ex_t *)alloc_flex(sizeof(mp3dec_ex_t));
        s_scratch = alloc_flex(sizeof(mp3dec_scratch_t));
        if (!s_mp3 || !s_scratch) { close_decoder(); fail("No memory: MP3 decoder"); return false; }
        s_mp3_io.read = mp3_read_cb; s_mp3_io.read_data = &s_file;
        s_mp3_io.seek = mp3_seek_cb; s_mp3_io.seek_data = &s_file;
        // DO_NOT_SCAN: no whole-file pass to learn the length (seconds of SD reads inside
        // the touch handler). We never seek, so no frame index is needed either.
        if (mp3dec_ex_open_cb(s_mp3, &s_mp3_io, MP3D_DO_NOT_SCAN) != 0) {
            close_decoder(); fail("Can't read this MP3 (or no memory for its buffer)"); return false;
        }
        s_kind          = Kind::Mp3;
        s_channels      = s_mp3->info.channels;
        s_sample_rate   = s_mp3->info.hz;
        if (s_channels > 0 && s_mp3->samples > 0) {
            // A length tag was found: samples are interleaved, so divide out the channels.
            s_total_frames = s_mp3->samples / (uint64_t)s_channels;
        } else {
            uint64_t audio = (uint64_t)s_file.size();
            if (audio > s_mp3->start_offset) audio -= s_mp3->start_offset;
            s_total_frames = mp3_estimate_frames(audio, s_mp3->info.bitrate_kbps, (int)s_sample_rate);
        }
    }
    if (s_channels <= 0 || s_channels > 2 || s_sample_rate == 0) {
        close_decoder();   // something we can't play (e.g. >2 channels)
        fail("Unsupported audio format");
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

    int16_t *chunk = (int16_t *)alloc_flex(PCM_CHUNK_SAMPLES * sizeof(int16_t));
    if (!chunk) fail("No memory: audio buffer");
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
            if (frames == 0) {                                 // EOF or decode error
                if (s_cur_frame == 0) s_last_error = "Decode failed";   // never produced a sample
                s_running = false; break;
            }
            // Software volume: scale each sample by s_volume/100 (no HW volume on
            // the MAX98357A). A plain multiply/shift per sample; cheap at 44.1 kHz.
            int vol = s_volume;
            if (vol < 100) {
                if (vol < 0) vol = 100;
                size_t ns = frames * (size_t)s_channels;
                for (size_t i = 0; i < ns; i++) chunk[i] = (int16_t)(((int)chunk[i] * vol) / 100);
            }
            instance.player.write(chunk, frames * s_channels * sizeof(int16_t));
        }
        heap_caps_free(chunk);
    }

    instance.powerControl(POWER_SPEAK, false);
    close_decoder();
    s_group = -1; s_pos = -1;
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

static void *music_mp3_scratch(void) { return s_scratch; }

// Load the saved volume the first time it is needed (default 70%).
static void volume_load()
{
    if (s_volume >= 0) return;
    s_volume = 70;
    File f = SD.open(MUSIC_CFG, FILE_READ);
    if (f) {
        char line[32]; size_t n = f.readBytesUntil('\n', line, sizeof line - 1); line[n] = '\0';
        if (!strncmp(line, "volume=", 7)) { int v = atoi(line + 7); if (v >= 0 && v <= 100) s_volume = v; }
        f.close();
    }
}

bool music_player_play(MusicBrowse mode, int group, int pos)
{
    int gidx = music_lib_group_track(mode, group, pos);   // global track index, -1 if out of range
    const MusicTrack *t = music_lib_track(gidx);
    if (!t) return false;
    s_last_error = "";
    volume_load();
    if (alarm_chime_is_active()) { s_last_error = "Speaker busy (alarm)"; return false; }   // try again shortly

    stop_task_and_wait();

    if (!open_decoder(t->path)) return false;

    s_mode  = mode;
    s_group = group;
    s_pos   = pos;
    snprintf(s_title,  sizeof(s_title),  "%s", t->title);
    snprintf(s_artist, sizeof(s_artist), "%s", t->artist[0] ? t->artist : "Unknown");

    if (!s_pause_sem) s_pause_sem = xSemaphoreCreateBinary();
    s_running = true;
    s_paused  = false;
    // The decode scratch is heap memory now (see s_scratch), so the task only needs a
    // normal stack: the decoder's own frames add up to ~2 KB. 8 KB is what this watch
    // already allocated for this task on the first test; 12 KB failed for lack of a block.
    if (xTaskCreatePinnedToCore(player_task, "music_player", 8192, NULL, 1, &s_task, 0) != pdPASS) {
        s_running = false; s_task = nullptr;
        close_decoder();
        s_group = -1; s_pos = -1;
        fail("No memory: playback task");
        return false;
    }
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
    if (s_group < 0) return false;
    if (s_pos + 1 >= music_lib_group_track_count(s_mode, s_group)) return false;
    return music_player_play(s_mode, s_group, s_pos + 1);
}

bool music_player_prev()
{
    if (s_group < 0 || s_pos <= 0) return false;
    return music_player_play(s_mode, s_group, s_pos - 1);
}

bool music_player_is_playing() { return s_task != nullptr && s_running && !s_paused; }
bool music_player_is_paused()  { return s_task != nullptr && s_running && s_paused; }

const char *music_player_last_error()     { return s_last_error; }
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

int music_player_get_volume()
{
    volume_load();
    return s_volume;
}

void music_player_set_volume(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_volume = pct;    // the running task reads s_volume each chunk, so this is live
    // Persist (best-effort; skipped if the SD is busy as a USB drive).
    if (!usb_sd_is_running() && instance.isCardReady()) {
        if (!SD.exists("/Settings")) SD.mkdir("/Settings");
        File f = SD.open(MUSIC_CFG, FILE_WRITE);
        if (f) { f.printf("volume=%d\n", pct); f.close(); }
    }
}
