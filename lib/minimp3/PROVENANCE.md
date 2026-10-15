# minimp3 — vendored

Source: https://github.com/lieff/minimp3 (`master` branch)
Fetched: 2026-09-27, files `minimp3.h` and `minimp3_ex.h`. `minimp3_ex.h` is unmodified;
`minimp3.h` carries ONE local patch (see below).
License: CC0 1.0 Universal (public domain dedication) — see `LICENSE` in this
directory, copied verbatim from the upstream repo.

Used by `src/music_decode.cpp` for MP3 decode. Configured with
`MINIMP3_ONLY_MP3` and `MINIMP3_NO_SIMD` (see that file) to keep the compiled
size down — this firmware runs on Xtensa, which minimp3's x86/ARM SIMD paths
do not target anyway, and MP2/MP1 decode is dead weight we never use.

## Local patch (2026-10-06)

`mp3dec_decode_frame()` keeps a ~16 KB `mp3dec_scratch_t` on the stack. On this watch that
forced a 24 KB task stack out of internal RAM in one piece, which fragmented RAM could not
give, so playback silently never started. The patch adds an opt-in hook: when a build defines
`MINIMP3_EXTERNAL_SCRATCH` (a function returning a `mp3dec_scratch_t *`), the scratch comes
from there instead of the stack. Without the define the code is byte-for-byte upstream. The
change is the `#ifdef MINIMP3_EXTERNAL_SCRATCH` block at the top of `mp3dec_decode_frame()`
and the matching `#undef scratch` after it; `src/music_player.cpp` defines the hook.

Apart from that patch, do not hand-edit these files; re-fetch from upstream if a fix is needed
and re-apply the patch.
