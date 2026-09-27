# minimp3 — vendored

Source: https://github.com/lieff/minimp3 (`master` branch)
Fetched: 2026-09-27, files `minimp3.h` and `minimp3_ex.h` unmodified.
License: CC0 1.0 Universal (public domain dedication) — see `LICENSE` in this
directory, copied verbatim from the upstream repo.

Used by `src/music_decode.cpp` for MP3 decode. Configured with
`MINIMP3_ONLY_MP3` and `MINIMP3_NO_SIMD` (see that file) to keep the compiled
size down — this firmware runs on Xtensa, which minimp3's x86/ARM SIMD paths
do not target anyway, and MP2/MP1 decode is dead weight we never use.

Do not hand-edit these files; re-fetch from upstream if a fix is needed.
