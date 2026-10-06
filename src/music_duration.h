#pragma once
// music_duration.h - track length for an MP3 that has no VBR/Xing header. Header-only and
// free of any hardware, so it is covered by the host tests (test/test_music_duration.cpp).
//
// Opening a stream with minimp3's default flags scans the WHOLE file to learn its
// length, which blocks the touch handler for seconds on an SPI SD card. The player opens
// with MP3D_DO_NOT_SCAN instead and, when the file has no length tag, estimates the
// length from the file size and the first frame's bitrate (exact for constant bitrate).
#include <cstdint>

// Length in PCM frames (one sample per channel). 0 when it cannot be estimated.
inline uint64_t mp3_estimate_frames(uint64_t audio_bytes, int bitrate_kbps, int sample_rate_hz)
{
    if (bitrate_kbps <= 0 || sample_rate_hz <= 0 || audio_bytes == 0) return 0;
    // bytes * 8 / (kbps * 1000) seconds, times the sample rate; integer math, no overflow
    // for any file that fits on a card (bytes * 8 * hz < 2^64 up to ~48 TB).
    return audio_bytes * 8ULL * (uint64_t)sample_rate_hz / ((uint64_t)bitrate_kbps * 1000ULL);
}
