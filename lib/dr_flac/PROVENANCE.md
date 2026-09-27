# dr_flac — vendored

Source: https://github.com/mackron/dr_libs (`master` branch)
Fetched: 2026-09-27, file `dr_flac.h` unmodified (v0.13.4 at fetch time, per
the header comment).
License: dual public domain (unlicense.org) / MIT-No-Attribution — see the
license text at the end of `dr_flac.h`. We use it under the public-domain
grant, same posture as minimp3.

Used by `src/music_decode.cpp` for FLAC decode. `DR_FLAC_NO_STDIO` and
`DR_FLAC_NO_OGG` are defined before including it (see that file): we stream
from the Arduino `SD` `File` API ourselves rather than dr_flac's stdio
wrapper, and never see FLAC-in-Ogg containers here.

Do not hand-edit this file; re-fetch from upstream if a fix is needed.
