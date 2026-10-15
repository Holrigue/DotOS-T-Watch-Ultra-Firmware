// test_music_tags.cpp - host tests for the tag reader + path fallback (src/music_tags.h).
#include "wl_test.h"
#include "music_tags.h"

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

namespace {

// A byte buffer standing in for an SD File.
struct Buf { const uint8_t *p; size_t n; };
size_t buf_read(void *ctx, uint32_t off, uint8_t *out, size_t n) {
    Buf *b = (Buf *)ctx;
    if (off >= b->n) return 0;
    size_t avail = b->n - off;
    if (n > avail) n = avail;
    memcpy(out, b->p + off, n);
    return n;
}

// Build a minimal ID3v2.3 header + a few text frames, encoding 0 (Latin-1) or 3 (UTF-8).
std::vector<uint8_t> id3v2(const std::vector<std::pair<std::string,std::string>> &frames, uint8_t enc = 0) {
    std::vector<uint8_t> body;
    for (auto &f : frames) {
        body.insert(body.end(), f.first.begin(), f.first.end());
        uint32_t sz = (uint32_t)f.second.size() + 1;   // +1 for the encoding byte
        body.push_back((sz >> 24) & 0xFF); body.push_back((sz >> 16) & 0xFF);
        body.push_back((sz >> 8) & 0xFF);  body.push_back(sz & 0xFF);
        body.push_back(0); body.push_back(0);           // flags
        body.push_back(enc);
        body.insert(body.end(), f.second.begin(), f.second.end());
    }
    std::vector<uint8_t> out = {'I','D','3', 3, 0, 0};
    uint32_t ss = (uint32_t)body.size();
    out.push_back((ss >> 21) & 0x7F); out.push_back((ss >> 14) & 0x7F);
    out.push_back((ss >> 7) & 0x7F);  out.push_back(ss & 0x7F);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

}  // namespace

WL_TEST(music_tags_id3v23_basic) {
    auto data = id3v2({{"TIT2","Rattlesnake"},{"TPE1","King Gizzard"},{"TALB","Flying Microtonal Banana"},{"TRCK","1/9"}});
    Buf b{data.data(), data.size()};
    MusicMeta m;
    WL_CHECK(music_tags_read(buf_read, &b, (uint32_t)data.size(), m));
    WL_CHECK(strcmp(m.title, "Rattlesnake") == 0);
    WL_CHECK(strcmp(m.artist, "King Gizzard") == 0);
    WL_CHECK(strcmp(m.album, "Flying Microtonal Banana") == 0);
    WL_CHECK_EQ((int)m.track, 1);
}

WL_TEST(music_tags_albumartist_fallback) {
    auto data = id3v2({{"TIT2","Song"},{"TPE2","Various Artists"},{"TALB","Comp"}});
    Buf b{data.data(), data.size()};
    MusicMeta m;
    music_tags_read(buf_read, &b, (uint32_t)data.size(), m);
    WL_CHECK(strcmp(m.artist, "Various Artists") == 0);   // no TPE1 -> use TPE2
}

WL_TEST(music_tags_utf8_frame) {
    auto data = id3v2({{"TIT2","Caf\xC3\xA9 del Mar"},{"TPE1","Bj\xC3\xB6rk"}}, 3);
    Buf b{data.data(), data.size()};
    MusicMeta m;
    music_tags_read(buf_read, &b, (uint32_t)data.size(), m);
    WL_CHECK(strcmp(m.title, "Caf\xC3\xA9 del Mar") == 0);
    WL_CHECK(strcmp(m.artist, "Bj\xC3\xB6rk") == 0);
}

WL_TEST(music_tags_id3v1_when_no_v2) {
    std::vector<uint8_t> data(2000, 0);
    data[0] = 0xFF; data[1] = 0xFB;                       // looks like an MP3 frame, no ID3v2
    uint8_t tag[128] = {0};
    memcpy(tag, "TAG", 3);
    memcpy(tag + 3,  "Title One", 9);
    memcpy(tag + 33, "Artist Two", 10);
    memcpy(tag + 63, "Album Three", 11);
    tag[125] = 0; tag[126] = 7;                           // ID3v1.1 track 7
    data.insert(data.end(), tag, tag + 128);
    Buf b{data.data(), data.size()};
    MusicMeta m;
    WL_CHECK(music_tags_read(buf_read, &b, (uint32_t)data.size(), m));
    WL_CHECK(strcmp(m.title, "Title One") == 0);
    WL_CHECK(strcmp(m.artist, "Artist Two") == 0);
    WL_CHECK_EQ((int)m.track, 7);
}

WL_TEST(music_tags_none_found_is_false) {
    std::vector<uint8_t> data(1000, 0x55);
    Buf b{data.data(), data.size()};
    MusicMeta m;
    WL_CHECK(!music_tags_read(buf_read, &b, (uint32_t)data.size(), m));
    WL_CHECK_EQ((int)m.title[0], 0);
}

WL_TEST(music_meta_path_artist_album_track_title) {
    MusicMeta m; memset(&m, 0, sizeof m);
    music_meta_fill_from_path("King Gizzard/Nonagon Infinity/03 Gamma Knife.mp3", m);
    WL_CHECK(strcmp(m.artist, "King Gizzard") == 0);
    WL_CHECK(strcmp(m.album, "Nonagon Infinity") == 0);
    WL_CHECK(strcmp(m.title, "Gamma Knife") == 0);
    WL_CHECK_EQ((int)m.track, 3);
}

WL_TEST(music_meta_path_artist_dash_album_folder) {
    MusicMeta m; memset(&m, 0, sizeof m);
    music_meta_fill_from_path("/music/Boards of Canada - Geogaddi/04 - 1969.flac", m);
    WL_CHECK(strcmp(m.artist, "Boards of Canada") == 0);
    WL_CHECK(strcmp(m.album, "Geogaddi") == 0);
    WL_CHECK(strcmp(m.title, "1969") == 0);
    WL_CHECK_EQ((int)m.track, 4);
}

WL_TEST(music_meta_path_all_in_filename_at_root) {
    // No artist folder, so the "Artist - Album - NN Title" filename supplies them.
    MusicMeta m; memset(&m, 0, sizeof m);
    music_meta_fill_from_path("/music/Aphex Twin - Drukqs - 07 Vordhosbn.mp3", m);
    WL_CHECK(strcmp(m.artist, "Aphex Twin") == 0);
    WL_CHECK(strcmp(m.album, "Drukqs") == 0);
    WL_CHECK(strcmp(m.title, "Vordhosbn") == 0);
    WL_CHECK_EQ((int)m.track, 7);
}

WL_TEST(music_meta_path_artist_folder_dash_title_filename) {
    // Folder gives the artist; the filename is "NN Title" so the title is clean.
    MusicMeta m; memset(&m, 0, sizeof m);
    music_meta_fill_from_path("/music/King Gizzard/12 Robot Stop.mp3", m);
    WL_CHECK(strcmp(m.artist, "King Gizzard") == 0);
    WL_CHECK(strcmp(m.title, "Robot Stop") == 0);
    WL_CHECK_EQ((int)m.track, 12);
}

WL_TEST(music_meta_path_plain_title_no_folder) {
    MusicMeta m; memset(&m, 0, sizeof m);
    music_meta_fill_from_path("/music/Untitled Jam.mp3", m);
    WL_CHECK(strcmp(m.title, "Untitled Jam") == 0);
    WL_CHECK_EQ((int)m.artist[0], 0);
    WL_CHECK_EQ((int)m.track, 0);
}

WL_TEST(music_meta_path_does_not_override_tags) {
    MusicMeta m; memset(&m, 0, sizeof m);
    strcpy(m.title, "Tagged Title");
    strcpy(m.artist, "Tagged Artist");
    music_meta_fill_from_path("Folder Artist/Album/09 File Title.mp3", m);
    WL_CHECK(strcmp(m.title, "Tagged Title") == 0);       // kept
    WL_CHECK(strcmp(m.artist, "Tagged Artist") == 0);     // kept
    WL_CHECK(strcmp(m.album, "Album") == 0);              // filled
    WL_CHECK_EQ((int)m.track, 9);                         // filled
}
