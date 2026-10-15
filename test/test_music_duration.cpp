// test_music_duration.cpp - host tests for the MP3 length estimate (src/music_duration.h).
#include "wl_test.h"
#include "music_duration.h"

WL_TEST(music_duration_cbr_128k_one_minute) {
  // 128 kbit/s = 16000 bytes/s: 60 s of audio at 44.1 kHz.
  WL_CHECK_EQ((long long)mp3_estimate_frames(16000ULL * 60, 128, 44100), 44100LL * 60);
}

WL_TEST(music_duration_cbr_320k_nine_minutes) {
  // The 12.7 MB file on the card is about 5 min at 320k: 40000 bytes/s.
  WL_CHECK_EQ((long long)mp3_estimate_frames(40000ULL * 300, 320, 48000), 48000LL * 300);
}

WL_TEST(music_duration_unknown_inputs_give_zero) {
  WL_CHECK_EQ((long long)mp3_estimate_frames(0, 128, 44100), 0);
  WL_CHECK_EQ((long long)mp3_estimate_frames(1000, 0, 44100), 0);
  WL_CHECK_EQ((long long)mp3_estimate_frames(1000, 128, 0), 0);
  WL_CHECK_EQ((long long)mp3_estimate_frames(1000, -1, 44100), 0);
}
