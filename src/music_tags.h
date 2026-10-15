#pragma once
//
// music_tags.h - read title / artist / album / track number for the Music tool.
//
// Pure, header-only and host-tested (test/test_music_tags.cpp). Reads through a
// callback so the same code runs on an SD File on the watch and on a byte buffer
// on the host. Supported, in this order:
//   * ID3v2.2 / 2.3 / 2.4 at the start of an MP3 (TIT2/TPE1/TPE2/TALB/TRCK),
//   * FLAC Vorbis comments (TITLE/ARTIST/ALBUMARTIST/ALBUM/TRACKNUMBER),
//   * ID3v1 in the last 128 bytes of an MP3,
// and anything still missing is filled from the path, see music_meta_fill_from_path():
//   /music/Artist/Album/NN Title.mp3, /music/Artist - Album/NN - Title.mp3,
//   /music/Artist/Artist - Album - NN Title.mp3, ...
// Frames are skipped by size, so a large cover image costs a seek, not a read.
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cctype>

#define MUSIC_META_TITLE_LEN  64
#define MUSIC_META_ARTIST_LEN 48
#define MUSIC_META_ALBUM_LEN  64

struct MusicMeta {
    char     title[MUSIC_META_TITLE_LEN];
    char     artist[MUSIC_META_ARTIST_LEN];
    char     album[MUSIC_META_ALBUM_LEN];
    uint16_t track;        // 0 = unknown
};

// Reads up to n bytes at absolute offset off; returns bytes read (short = EOF/error).
typedef size_t (*music_tag_read_fn)(void *ctx, uint32_t off, uint8_t *buf, size_t n);

namespace music_tags_detail {

// Append one code point as UTF-8, never past cap-1 and never splitting a sequence.
inline void put_cp(char *out, size_t cap, size_t &len, uint32_t cp)
{
    char tmp[4]; size_t n;
    if (cp < 0x80)        { tmp[0] = (char)cp; n = 1; }
    else if (cp < 0x800)  { tmp[0] = (char)(0xC0 | (cp >> 6)); tmp[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000){ tmp[0] = (char)(0xE0 | (cp >> 12)); tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            tmp[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
    else                  { tmp[0] = (char)(0xF0 | (cp >> 18)); tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                            tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); tmp[3] = (char)(0x80 | (cp & 0x3F)); n = 4; }
    if (len + n >= cap) return;
    memcpy(out + len, tmp, n); len += n; out[len] = '\0';
}

inline void trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = '\0';
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) memmove(s, s + i, n - i + 1);
}

// Decode an ID3 text payload (first byte = encoding) into UTF-8. Stops at the first NUL.
inline void id3_text(const uint8_t *p, size_t n, char *out, size_t cap)
{
    size_t len = 0; out[0] = '\0';
    if (n < 1 || cap < 2) return;
    uint8_t enc = p[0]; p++; n--;
    if (enc == 0) {                                  // ISO-8859-1
        for (size_t i = 0; i < n && p[i]; i++) put_cp(out, cap, len, p[i]);
    } else if (enc == 3) {                           // UTF-8
        for (size_t i = 0; i < n && p[i]; i++) {
            if (len + 1 >= cap) break;
            // Do not leave a cut multi-byte sequence at the end.
            if ((p[i] & 0xC0) == 0xC0) {
                size_t need = (p[i] & 0xE0) == 0xC0 ? 2 : (p[i] & 0xF0) == 0xE0 ? 3 : 4;
                if (len + need >= cap || i + need > n) break;
                memcpy(out + len, p + i, need); len += need; i += need - 1; out[len] = '\0';
                continue;
            }
            out[len++] = (char)p[i]; out[len] = '\0';
        }
    } else {                                         // 1 = UTF-16 with BOM, 2 = UTF-16BE
        bool be = (enc == 2);
        size_t i = 0;
        if (enc == 1 && n >= 2) {
            if (p[0] == 0xFF && p[1] == 0xFE) { be = false; i = 2; }
            else if (p[0] == 0xFE && p[1] == 0xFF) { be = true; i = 2; }
        }
        for (; i + 1 < n; i += 2) {
            uint32_t u = be ? (uint32_t)(p[i] << 8 | p[i + 1]) : (uint32_t)(p[i + 1] << 8 | p[i]);
            if (u == 0) break;
            if (u >= 0xD800 && u < 0xDC00 && i + 3 < n) {
                uint32_t lo = be ? (uint32_t)(p[i + 2] << 8 | p[i + 3]) : (uint32_t)(p[i + 3] << 8 | p[i + 2]);
                if (lo >= 0xDC00 && lo < 0xE000) { u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
            }
            put_cp(out, cap, len, u);
        }
    }
    trim(out);
}

inline uint16_t track_num(const char *s)
{
    while (*s == ' ') s++;
    int v = atoi(s);                                 // "3/12" -> 3
    return (v > 0 && v < 10000) ? (uint16_t)v : 0;
}

inline uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
inline uint32_t le32(const uint8_t *p) { return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]; }
inline uint32_t syncsafe(const uint8_t *p) { return (uint32_t)(p[0] & 0x7F) << 21 | (uint32_t)(p[1] & 0x7F) << 14 | (uint32_t)(p[2] & 0x7F) << 7 | (p[3] & 0x7F); }

constexpr size_t FRAME_MAX = 512;                    // longest text frame we read

inline bool parse_id3v2(music_tag_read_fn rd, void *ctx, MusicMeta &m)
{
    uint8_t h[10];
    if (rd(ctx, 0, h, 10) != 10 || memcmp(h, "ID3", 3) != 0) return false;
    const int ver = h[3];
    if (ver < 2 || ver > 4) return false;
    const uint32_t end = 10 + syncsafe(h + 6);
    uint32_t off = 10;
    if ((h[5] & 0x40) && ver >= 3) {                 // extended header
        uint8_t e[4];
        if (rd(ctx, off, e, 4) != 4) return false;
        off += (ver == 4) ? syncsafe(e) : be32(e) + 4;
    }
    const size_t idn = ver == 2 ? 3 : 4, hdr = ver == 2 ? 6 : 10;
    char artist2[MUSIC_META_ARTIST_LEN] = "";        // album artist, used if no artist
    uint8_t buf[FRAME_MAX];
    bool any = false;
    while (off + hdr <= end) {
        uint8_t fh[10];
        if (rd(ctx, off, fh, hdr) != hdr || fh[0] == 0) break;   // padding
        uint32_t size = ver == 2 ? ((uint32_t)fh[3] << 16 | (uint32_t)fh[4] << 8 | fh[5])
                      : ver == 4 ? syncsafe(fh + 4) : be32(fh + 4);
        uint32_t data = off + (uint32_t)hdr;
        off = data + size;
        if (size == 0 || off > end) break;
        const char *id = (const char *)fh;
        char *dst = nullptr; size_t cap = 0; bool is_trk = false, is_aa = false;
        if      (!memcmp(id, ver == 2 ? "TT2" : "TIT2", idn)) { dst = m.title;  cap = sizeof m.title; }
        else if (!memcmp(id, ver == 2 ? "TP1" : "TPE1", idn)) { dst = m.artist; cap = sizeof m.artist; }
        else if (!memcmp(id, ver == 2 ? "TP2" : "TPE2", idn)) { dst = artist2;  cap = sizeof artist2; is_aa = true; }
        else if (!memcmp(id, ver == 2 ? "TAL" : "TALB", idn)) { dst = m.album;  cap = sizeof m.album; }
        else if (!memcmp(id, ver == 2 ? "TRK" : "TRCK", idn)) { is_trk = true; }
        else continue;
        (void)is_aa;
        if (ver >= 3) {
            uint8_t f2 = fh[9];
            bool skip = ver == 4 ? (f2 & 0x0C) : (f2 & 0xC0);   // compressed / encrypted
            if (skip) continue;
            if (ver == 4 && (f2 & 0x01)) { data += 4; size = size >= 4 ? size - 4 : 0; }   // data length indicator
            if (ver == 3 && (f2 & 0x20)) { data += 1; size = size >= 1 ? size - 1 : 0; }   // grouping id
        }
        size_t n = size < FRAME_MAX ? size : FRAME_MAX;
        if (rd(ctx, data, buf, n) != n) break;
        if (is_trk) { char t[16]; id3_text(buf, n, t, sizeof t); m.track = track_num(t); any = true; continue; }
        id3_text(buf, n, dst, cap);
        any = true;
    }
    if (!m.artist[0] && artist2[0]) { strncpy(m.artist, artist2, sizeof m.artist - 1); m.artist[sizeof m.artist - 1] = '\0'; }
    return any;
}

inline void set_from(char *dst, size_t cap, const uint8_t *v, size_t n)
{
    size_t len = 0; dst[0] = '\0';
    for (size_t i = 0; i < n && len + 1 < cap; i++) {
        if ((v[i] & 0xC0) == 0xC0) {                  // keep multi-byte sequences whole
            size_t need = (v[i] & 0xE0) == 0xC0 ? 2 : (v[i] & 0xF0) == 0xE0 ? 3 : 4;
            if (len + need >= cap || i + need > n) break;
            memcpy(dst + len, v + i, need); len += need; i += need - 1;
        } else dst[len++] = (char)v[i];
    }
    dst[len] = '\0';
    trim(dst);
}

inline bool parse_flac(music_tag_read_fn rd, void *ctx, MusicMeta &m)
{
    uint8_t h[4];
    if (rd(ctx, 0, h, 4) != 4 || memcmp(h, "fLaC", 4) != 0) return false;
    uint32_t off = 4;
    for (int guard = 0; guard < 64; guard++) {
        uint8_t bh[4];
        if (rd(ctx, off, bh, 4) != 4) return false;
        const bool last = bh[0] & 0x80;
        const uint32_t len = (uint32_t)bh[1] << 16 | (uint32_t)bh[2] << 8 | bh[3];
        const uint32_t body = off + 4;
        if ((bh[0] & 0x7F) == 4) {                    // VORBIS_COMMENT (little-endian lengths)
            uint8_t w[4];
            if (rd(ctx, body, w, 4) != 4) return false;
            uint32_t p = body + 4 + le32(w);          // skip the vendor string
            if (rd(ctx, p, w, 4) != 4) return false;
            uint32_t count = le32(w); p += 4;
            char artist2[MUSIC_META_ARTIST_LEN] = "";
            uint8_t buf[FRAME_MAX];
            for (uint32_t i = 0; i < count && i < 256 && p + 4 <= body + len; i++) {
                if (rd(ctx, p, w, 4) != 4) break;
                uint32_t clen = le32(w); uint32_t c = p + 4; p = c + clen;
                size_t n = clen < FRAME_MAX ? clen : FRAME_MAX;
                if (rd(ctx, c, buf, n) != n) break;
                const uint8_t *eq = (const uint8_t *)memchr(buf, '=', n);
                if (!eq) continue;
                size_t klen = (size_t)(eq - buf), vlen = n - klen - 1;
                auto key = [&](const char *k) { return strlen(k) == klen && strncasecmp((const char *)buf, k, klen) == 0; };
                if      (key("TITLE"))       set_from(m.title,  sizeof m.title,  eq + 1, vlen);
                else if (key("ARTIST"))      set_from(m.artist, sizeof m.artist, eq + 1, vlen);
                else if (key("ALBUMARTIST")) set_from(artist2,  sizeof artist2,  eq + 1, vlen);
                else if (key("ALBUM"))       set_from(m.album,  sizeof m.album,  eq + 1, vlen);
                else if (key("TRACKNUMBER")) { char t[16]; set_from(t, sizeof t, eq + 1, vlen); m.track = track_num(t); }
            }
            if (!m.artist[0] && artist2[0]) { strncpy(m.artist, artist2, sizeof m.artist - 1); m.artist[sizeof m.artist - 1] = '\0'; }
            return true;
        }
        if (last) return false;
        off = body + len;
    }
    return false;
}

inline void parse_id3v1(music_tag_read_fn rd, void *ctx, uint32_t file_size, MusicMeta &m)
{
    if (file_size < 128) return;
    uint8_t t[128];
    if (rd(ctx, file_size - 128, t, 128) != 128 || memcmp(t, "TAG", 3) != 0) return;
    auto fill = [](char *dst, size_t cap, const uint8_t *src, size_t n) {
        if (dst[0]) return;                           // a v2 value wins
        size_t len = 0;
        for (size_t i = 0; i < n && src[i]; i++) put_cp(dst, cap, len, src[i]);   // Latin-1
        trim(dst);
    };
    fill(m.title,  sizeof m.title,  t + 3,  30);
    fill(m.artist, sizeof m.artist, t + 33, 30);
    fill(m.album,  sizeof m.album,  t + 63, 30);
    if (!m.track && t[125] == 0 && t[126]) m.track = t[126];   // ID3v1.1
}

inline void copy_n(char *dst, size_t cap, const char *src, size_t n)
{
    if (n >= cap) n = cap - 1;
    // Never end on half a UTF-8 sequence.
    while (n > 0 && (src[n] & 0xC0) == 0x80) n--;
    memcpy(dst, src, n); dst[n] = '\0';
    trim(dst);
}

// Position of " - " in [s, s+n), or -1.
inline int find_sep(const char *s, size_t n)
{
    for (size_t i = 0; i + 3 <= n; i++) if (s[i] == ' ' && s[i + 1] == '-' && s[i + 2] == ' ') return (int)i;
    return -1;
}

} // namespace music_tags_detail

// Read tags from a file through `rd`. Fields not found stay empty (track 0).
// Returns true if any tag field was found.
inline bool music_tags_read(music_tag_read_fn rd, void *ctx, uint32_t file_size, MusicMeta &m)
{
    using namespace music_tags_detail;
    memset(&m, 0, sizeof m);
    bool v2 = parse_id3v2(rd, ctx, m);
    bool fl = !v2 && parse_flac(rd, ctx, m);
    if (!fl) parse_id3v1(rd, ctx, file_size, m);
    return m.title[0] || m.artist[0] || m.album[0] || m.track;
}

// Fill whatever is still empty in `m` from the path below the music root, e.g.
// "Artist/Album/03 Title.mp3", "Artist - Album/03 - Title.flac",
// "Artist - Album - 03 Title.mp3" or just "Title.mp3".
inline void music_meta_fill_from_path(const char *rel_path, MusicMeta &m)
{
    using namespace music_tags_detail;
    // Split into up to two folder components and the file name. A leading
    // "music" segment (the SD root the scan prepends) is skipped so callers can
    // pass the full "/music/Artist/..." path as-is.
    const char *parts[8]; size_t lens[8]; int np = 0;
    const char *s = rel_path;
    while (*s == '/') s++;
    if (strncasecmp(s, "music", 5) == 0 && (s[5] == '/' || s[5] == '\0')) {
        s += 5;
        while (*s == '/') s++;
    }
    while (*s && np < 8) {
        const char *e = strchr(s, '/');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        if (n) { parts[np] = s; lens[np] = n; np++; }
        if (!e) break;
        s = e + 1;
    }
    if (np == 0) return;
    const char *file = parts[np - 1];
    size_t flen = lens[np - 1];
    const char *dot = nullptr;
    for (size_t i = 0; i < flen; i++) if (file[i] == '.') dot = file + i;
    if (dot) flen = (size_t)(dot - file);

    char f_artist[MUSIC_META_ARTIST_LEN] = "", f_album[MUSIC_META_ALBUM_LEN] = "";
    if (np >= 3) {                                    // Artist/Album/file
        copy_n(f_artist, sizeof f_artist, parts[np - 3], lens[np - 3]);
        copy_n(f_album,  sizeof f_album,  parts[np - 2], lens[np - 2]);
    } else if (np == 2) {                             // "Artist - Album"/file or Artist/file
        int sep = find_sep(parts[0], lens[0]);
        if (sep > 0) {
            copy_n(f_artist, sizeof f_artist, parts[0], (size_t)sep);
            copy_n(f_album,  sizeof f_album,  parts[0] + sep + 3, lens[0] - (size_t)sep - 3);
        } else {
            copy_n(f_artist, sizeof f_artist, parts[0], lens[0]);
        }
    }

    // File name: "Artist - Album - NN Title", "NN - Title", "NN. Title", "NN Title" or "Title".
    const char *t = file; size_t tlen = flen;
    int s1 = find_sep(t, tlen);
    if (s1 > 0) {
        const char *rest = t + s1 + 3; size_t rlen = tlen - (size_t)s1 - 3;
        int s2 = find_sep(rest, rlen);
        bool lead_digit = isdigit((unsigned char)t[0]);
        if (s2 > 0 && !lead_digit) {                  // Artist - Album - rest
            if (!f_artist[0]) copy_n(f_artist, sizeof f_artist, t, (size_t)s1);
            if (!f_album[0])  copy_n(f_album,  sizeof f_album,  rest, (size_t)s2);
            t = rest + s2 + 3; tlen = rlen - (size_t)s2 - 3;
        } else if (!lead_digit && s2 < 0) {
            // "Artist - Title" only when the folder gave no artist; otherwise keep it whole.
            if (!f_artist[0]) { copy_n(f_artist, sizeof f_artist, t, (size_t)s1); t = rest; tlen = rlen; }
        }
    }
    uint16_t num = 0; size_t i = 0;
    while (i < tlen && i < 4 && isdigit((unsigned char)t[i])) { num = (uint16_t)(num * 10 + (t[i] - '0')); i++; }
    if (i > 0 && i < tlen && (t[i] == ' ' || t[i] == '.' || t[i] == '-' || t[i] == '_')) {
        while (i < tlen && (t[i] == ' ' || t[i] == '.' || t[i] == '-' || t[i] == '_')) i++;
        if (i < tlen) { t += i; tlen -= i; } else num = 0;
    } else num = 0;

    if (!m.title[0])  copy_n(m.title, sizeof m.title, t, tlen);
    if (!m.artist[0]) { strncpy(m.artist, f_artist, sizeof m.artist - 1); m.artist[sizeof m.artist - 1] = '\0'; }
    if (!m.album[0])  { strncpy(m.album,  f_album,  sizeof m.album - 1);  m.album[sizeof m.album - 1] = '\0'; }
    if (!m.track) m.track = num;
}
