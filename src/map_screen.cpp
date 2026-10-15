#include "map_screen.h"
#include "psram_array.h"
#include "theme.h"
#include "gps_screen.h"
#include "meshtastic.h"
#include "gpx_track.h"
#include "gpx_recorder.h"
#include <LilyGoLib.h>
#include <SD.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// Defined by their respective screens. The map sits between send_message
// and configuration in the swipe chain (LEFT = back to send, RIGHT = on
// to configuration).
void send_message_screen_show();
void configuration_screen_show();
// Defined in main.cpp
void main_loop_request_lvgl_priority(int cycles);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAP_W   410
#define MAP_H   502
#define TILE_PX 256

// 3x3 grid of tiles covers any sub-tile centring on a 410x502 screen
#define TILES   9

static lv_obj_t *map_screen;
static lv_obj_t *tile_imgs[TILES];
static lv_obj_t *marker;
static lv_obj_t *info_badge;
static lv_obj_t *info_label;
static lv_obj_t *status_label;
static lv_obj_t *zoom_in_btn, *zoom_out_btn, *recentre_btn;
static lv_obj_t *rec_btn, *rec_lbl;   // GPX track-recording toggle

// Peer-node markers - one dot + short_name label per node slot. Pool
// is sized to MESH_MAX_NODES (20) and lives for the lifetime of the
// screen; refresh() hides/shows individual entries based on which
// nodes currently have a known position + fall within the view.
static lv_obj_t *node_dots[MESH_MAX_NODES];
static lv_obj_t *node_labels[MESH_MAX_NODES];

// Per-tile path buffers — kept alive while LVGL uses them as the image source.
static PsramArray<char[80], TILES> s_paths;   // tile file paths (PSRAM)

// GPX track overlay (hiking follow). One lv_line polyline, projected from the
// loaded track each refresh so it tracks pan/zoom like the peer dots. The point
// array is capped (the whole track is decimated to fit) and must stay alive
// while LVGL references it, so it is file-scope. A small badge shows distance
// remaining while a track is loaded.
#define TRACK_MAXPTS 400
static lv_obj_t          *track_line = nullptr;
static PsramArray<lv_point_precise_t, TRACK_MAXPTS> s_track_pts;   // PSRAM (3.2 KB)
static lv_obj_t          *dist_badge = nullptr;
static lv_obj_t          *dist_label = nullptr;
static bool               s_gpx_autoload_tried = false;

static int    s_zoom        = 6;
static int    s_zoom_min    = 1;
static int    s_zoom_max    = 6;
static double s_last_lat    = 1000;   // sentinel: forces first render
static double s_last_lon    = 1000;
static int    s_last_zoom   = -1;

// Drag-to-pan state. While s_manual_pan is true, refresh() centres the
// map on (s_view_lat, s_view_lon) instead of the GPS fix, and the red
// marker is offset from screen centre to reflect where the GPS actually
// is. Cleared by the recentre button OR by the swipe-nav handler (since
// a swipe shouldn't leave the map permanently pinned away from GPS).
static bool   s_manual_pan = false;
static double s_view_lat   = 0;
static double s_view_lon   = 0;

// Last-known GPS fix persisted to /Meshtastic/last_fix.txt so the map
// can open centred where we last had a position - even if GPS is off
// or hasn't relocked. Loaded once on first map_screen_show(); written
// from refresh() whenever a fresh live fix is observed, rate-limited
// to once a minute so the SD doesn't get hammered.
static bool     s_have_saved_fix = false;
static double   s_saved_lat      = 0;
static double   s_saved_lon      = 0;
static bool     s_saved_loaded   = false;
static uint32_t s_last_save_ms   = 0;
static const uint32_t kSaveIntervalMs = 60000;
static const char    *kLastFixPath    = "/Meshtastic/last_fix.txt";

static void save_last_fix(double lat, double lon)
{
    uint32_t now = millis();
    if (s_last_save_ms != 0 && now - s_last_save_ms < kSaveIntervalMs) return;
    if (!instance.isCardReady()) return;
    if (!SD.exists("/Meshtastic")) SD.mkdir("/Meshtastic");
    File f = SD.open(kLastFixPath, FILE_WRITE);
    if (!f) return;
    f.printf("%.7f,%.7f\n", lat, lon);
    f.close();
    s_last_save_ms   = now;
    s_have_saved_fix = true;
    s_saved_lat      = lat;
    s_saved_lon      = lon;
}

// Read the persisted fix once. File format is "lat,lon\n". Missing
// or malformed -> s_have_saved_fix stays false and the cold-start
// (0,0 world view) path takes over.
static void load_last_fix()
{
    if (s_saved_loaded) return;
    s_saved_loaded = true;
    if (!instance.isCardReady()) return;
    if (!SD.exists(kLastFixPath)) return;
    File f = SD.open(kLastFixPath, FILE_READ);
    if (!f) return;
    char buf[64];
    size_t n = 0;
    while (f.available() && n < sizeof(buf) - 1) {
        int c = f.read();
        if (c < 0 || c == '\n') break;
        if (c == '\r') continue;
        buf[n++] = (char)c;
    }
    buf[n] = '\0';
    f.close();
    double lat, lon;
    if (sscanf(buf, "%lf,%lf", &lat, &lon) == 2) {
        s_have_saved_fix = true;
        s_saved_lat      = lat;
        s_saved_lon      = lon;
    }
}

// Slippy-map degrees-per-pixel at the current zoom. Lon scale is
// uniform across the projection; lat scale collapses with cos(lat) due
// to Mercator. Linearisation around the view centre - fine for the
// small per-event finger deltas we get from lv_indev_get_vect().
static inline double lon_per_px(int z) {
    return 360.0 / (256.0 * pow(2.0, (double)z));
}
static inline double lat_per_px(int z, double lat_deg) {
    double lat_rad = lat_deg * M_PI / 180.0;
    return 360.0 * cos(lat_rad) / (256.0 * pow(2.0, (double)z));
}

// ---- tile maths (standard slippy-map) --------------------------------------

static void rebuild_tiles(double lat, double lon, int z)
{
    double lat_rad = lat * M_PI / 180.0;
    double n       = pow(2.0, (double)z);
    double xtile_f = (lon + 180.0) / 360.0 * n;
    double ytile_f = (1.0 - asinh(tan(lat_rad)) / M_PI) / 2.0 * n;

    int cx = (int)floor(xtile_f);
    int cy = (int)floor(ytile_f);
    int px = (int)((xtile_f - cx) * TILE_PX);   // pixel offset within centre tile
    int py = (int)((ytile_f - cy) * TILE_PX);

    int cx_screen = MAP_W / 2 - px;
    int cy_screen = MAP_H / 2 - py;
    int maxt      = (int)n - 1;

    int idx = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int tx = cx + dx;
            int ty = cy + dy;
            if (tx < 0 || tx > maxt || ty < 0 || ty > maxt) {
                lv_obj_add_flag(tile_imgs[idx], LV_OBJ_FLAG_HIDDEN);
                idx++;
                continue;
            }
            snprintf(s_paths[idx], sizeof(s_paths[idx]),
                     "A:/map/%d/%d/%d.png", z, tx, ty);
            // Force a reload even when the path-buffer pointer is unchanged.
            lv_image_set_src(tile_imgs[idx], NULL);
            lv_image_set_src(tile_imgs[idx], s_paths[idx]);
            lv_obj_clear_flag(tile_imgs[idx], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(tile_imgs[idx],
                cx_screen + dx * TILE_PX,
                cy_screen + dy * TILE_PX);
            idx++;
        }
    }
}

// Walks /map/0 … /map/18 on the SD card to learn which zoom levels are present.
static void detect_zooms()
{
    if (!instance.isCardReady()) {
        s_zoom_min = 1;
        s_zoom_max = 6;
        return;
    }
    int lo = 99, hi = -1;
    for (int z = 0; z <= 18; z++) {
        char p[24];
        snprintf(p, sizeof(p), "/map/%d", z);
        if (SD.exists(p)) {
            if (z < lo) lo = z;
            if (z > hi) hi = z;
        }
    }
    if (hi < 0) { s_zoom_min = 1; s_zoom_max = 6; }
    else        { s_zoom_min = lo; s_zoom_max = hi; }
}

static void hide_tiles()
{
    for (int i = 0; i < TILES; i++)
        lv_obj_add_flag(tile_imgs[i], LV_OBJ_FLAG_HIDDEN);
}

// Project each known peer node's lat/lon onto the screen and either
// position+show its dot+label or hide it (off-screen / no position /
// is-our-own-node). view_lat/view_lon is the current map centre.
static void render_peer_nodes(double view_lat, double view_lon)
{
    uint32_t self_id = meshtastic_get_node_id();
    int n = meshtastic_get_node_count();
    int slot = 0;
    double lon_dpp = lon_per_px(s_zoom);
    double lat_dpp = lat_per_px(s_zoom, view_lat);
    for (int i = 0; i < n && slot < MESH_MAX_NODES; i++) {
        const MeshNode *node = meshtastic_get_node(i);
        if (!node) continue;
        if (!node->has_position) continue;
        if (node->node_id == self_id) continue;   // our own GPS marker

        double node_lat = node->latitude_i / 1e7;
        double node_lon = node->longitude_i / 1e7;
        double dx_px =  (node_lon - view_lon) / lon_dpp;
        double dy_px = -(node_lat - view_lat) / lat_dpp;
        int sx = (int)(MAP_W / 2.0 + dx_px);
        int sy = (int)(MAP_H / 2.0 + dy_px);

        // Cull off-screen dots; leave a small margin so a half-on-edge
        // marker still appears clipped rather than vanishing entirely.
        if (sx < -10 || sx >= MAP_W + 10 || sy < -10 || sy >= MAP_H + 10) {
            lv_obj_add_flag(node_dots[slot],   LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(node_labels[slot], LV_OBJ_FLAG_HIDDEN);
            slot++;
            continue;
        }

        lv_obj_align(node_dots[slot], LV_ALIGN_TOP_LEFT, sx - 5, sy - 5);
        lv_obj_clear_flag(node_dots[slot], LV_OBJ_FLAG_HIDDEN);

        // Label: short_name if the node sent one, else first 4 hex
        // chars of the node ID so they're at least somewhat
        // identifiable. Positioned 8 px below the dot.
        char lbl[12];
        if (node->short_name[0]) {
            snprintf(lbl, sizeof(lbl), "%s", node->short_name);
        } else {
            snprintf(lbl, sizeof(lbl), "%04lx",
                     (unsigned long)(node->node_id & 0xFFFF));
        }
        lv_label_set_text(node_labels[slot], lbl);
        lv_obj_align(node_labels[slot], LV_ALIGN_TOP_LEFT, sx - 14, sy + 8);
        lv_obj_clear_flag(node_labels[slot], LV_OBJ_FLAG_HIDDEN);
        slot++;
    }
    // Hide any slots we didn't use (node went away, lost position, etc).
    for (int j = slot; j < MESH_MAX_NODES; j++) {
        lv_obj_add_flag(node_dots[j],   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(node_labels[j], LV_OBJ_FLAG_HIDDEN);
    }
}

// Hide every peer dot/label at once - used when we don't have a GPS
// fix and the tiles + markers are all hidden.
static void hide_peer_nodes()
{
    for (int i = 0; i < MESH_MAX_NODES; i++) {
        lv_obj_add_flag(node_dots[i],   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(node_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
}

// Project the loaded GPX track onto the screen and draw it as one polyline,
// using the same slippy projection as the peer dots so it tracks pan/zoom. The
// whole track is decimated to <= TRACK_MAXPTS vertices and each is clamped to a
// sane pixel range (a track far from the view must not make a giant line).
static void render_gpx_track(double view_lat, double view_lon)
{
    if (!track_line) return;
    int n = gpx::count();
    if (n < 2) { lv_obj_add_flag(track_line, LV_OBJ_FLAG_HIDDEN); return; }

    double lon_dpp = lon_per_px(s_zoom);
    double lat_dpp = lat_per_px(s_zoom, view_lat);
    int stride = (n + TRACK_MAXPTS - 1) / TRACK_MAXPTS;   // >= 1
    if (stride < 1) stride = 1;

    auto project = [&](int i, int slot) {
        double dx =  (gpx::lon(i) - view_lon) / lon_dpp;
        double dy = -(gpx::lat(i) - view_lat) / lat_dpp;
        long sx = (long)(MAP_W / 2.0 + dx);
        long sy = (long)(MAP_H / 2.0 + dy);
        if (sx < -2000) sx = -2000; else if (sx > 2000) sx = 2000;
        if (sy < -2000) sy = -2000; else if (sy > 2000) sy = 2000;
        s_track_pts[slot].x = (int32_t)sx;
        s_track_pts[slot].y = (int32_t)sy;
    };

    int m = 0;
    for (int i = 0; i < n && m < TRACK_MAXPTS - 1; i += stride) project(i, m++);
    project(n - 1, m++);   // always land exactly on the last point

    if (m < 2) { lv_obj_add_flag(track_line, LV_OBJ_FLAG_HIDDEN); return; }
    lv_line_set_points(track_line, s_track_pts.data(), m);
    lv_obj_clear_flag(track_line, LV_OBJ_FLAG_HIDDEN);
}

static void refresh(bool force)
{
    // Three-source fallback chain for the map centre:
    //   1) live GPS fix - preferred; also stamped to SD for next run
    //   2) last-known fix loaded from SD - we've been here before,
    //      we just don't have a current lock
    //   3) (0,0) world view at min zoom - cold start, no live fix
    //      and nothing on disk; user drags from here.
    // Manual-pan always wins regardless of which source seeded the
    // pre-pan centre.
    bool has_live = gps_screen_has_lock() && instance.gps.location.isValid();
    double gps_lat = 0, gps_lon = 0;
    enum class Source { LIVE, SAVED, NONE } src = Source::NONE;
    if (has_live) {
        gps_lat = instance.gps.location.lat();
        gps_lon = instance.gps.location.lng();
        src     = Source::LIVE;
        save_last_fix(gps_lat, gps_lon);   // rate-limited internally
    } else if (s_have_saved_fix) {
        gps_lat = s_saved_lat;
        gps_lon = s_saved_lon;
        src     = Source::SAVED;
    }

    double lat, lon;
    if (s_manual_pan) {
        lat = s_view_lat;
        lon = s_view_lon;
    } else if (src != Source::NONE) {
        lat = gps_lat;
        lon = gps_lon;
    } else {
        // Cold start. Drop to the lowest available zoom so the world
        // view actually renders something; user can drag from there.
        lat = 0.0;
        lon = 0.0;
        if (s_zoom != s_zoom_min) s_zoom = s_zoom_min;
    }

    if (force || s_zoom != s_last_zoom
        || fabs(lat - s_last_lat) > 1e-5
        || fabs(lon - s_last_lon) > 1e-5) {
        s_last_lat  = lat;
        s_last_lon  = lon;
        s_last_zoom = s_zoom;
        rebuild_tiles(lat, lon, s_zoom);
    }
    // Old "No GPS fix" overlay is gone - the map is always interactive
    // now. Status label widget is kept around in case other code
    // references it but stays hidden.
    lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);

    // GPS marker means "you are here right now". Only show it when
    // the source is LIVE - saved-fix mode gives us a view centre but
    // doesn't tell us where the device currently is, so rendering a
    // marker there would be a lie.
    if (src != Source::LIVE) {
        lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
    } else if (s_manual_pan) {
        double dx_px =  (gps_lon - lon) / lon_per_px(s_zoom);
        double dy_px = -(gps_lat - lat) / lat_per_px(s_zoom, lat);
        int    mx    = (int)(MAP_W / 2.0 + dx_px) - 7;   // marker is 14x14
        int    my    = (int)(MAP_H / 2.0 + dy_px) - 7;
        if (mx < -14 || mx >= MAP_W || my < -14 || my >= MAP_H) {
            lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_align(marker, LV_ALIGN_TOP_LEFT, mx, my);
            lv_obj_clear_flag(marker, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_center(marker);
        lv_obj_clear_flag(marker, LV_OBJ_FLAG_HIDDEN);
    }

    // Info badge with provenance suffix. Pre-format with snprintf and
    // pass to lv_label_set_text (rather than lv_label_set_text_fmt)
    // to dodge LVGL's builtin vsnprintf, which has trouble with %s
    // args trailing %.4f doubles on RV32 soft-float (same bug we hit
    // on the P4).
    const char *src_tag =
        (src == Source::LIVE)  ? "" :
        (src == Source::SAVED) ? "  (saved)" : "  (no fix)";
    const char *pan_tag = s_manual_pan ? "  *" : "";
    char info[64];
    snprintf(info, sizeof(info), "MAP  z%d  %.4f, %.4f%s%s",
             s_zoom, lat, lon, src_tag, pan_tag);
    lv_label_set_text(info_label, info);

    // Plot every other node we've heard a position from. Always uses
    // the current view centre (lat/lon, which is GPS or manual-pan
    // depending on s_manual_pan), so peer dots track pan and zoom
    // without any extra work.
    render_peer_nodes(lat, lon);

    // GPX track overlay + distance badge (hiking follow).
    render_gpx_track(lat, lon);
    if (gpx::loaded()) {
        char db[48];
        if (src == Source::LIVE)
            snprintf(db, sizeof(db), "%s  %.1f km left",
                     gpx::name(), gpx::remaining_km(gps_lat, gps_lon));
        else
            snprintf(db, sizeof(db), "%s  %.1f km", gpx::name(), gpx::total_km());
        lv_label_set_text(dist_label, db);
        lv_obj_clear_flag(dist_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(dist_badge, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---- events ----------------------------------------------------------------

static void on_zoom_in(lv_event_t *)
{
    if (s_zoom < s_zoom_max) { s_zoom++; refresh(true); }
}

static void on_zoom_out(lv_event_t *)
{
    if (s_zoom > s_zoom_min) { s_zoom--; refresh(true); }
}

static void on_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
        // The swipe also fed PRESSING events to on_pan, which may have
        // shifted the view a bit during the gesture. Clear that side
        // effect so coming back to the map snaps to GPS rather than
        // leaving it permanently nudged.
        s_manual_pan = false;
    }
    if      (dir == LV_DIR_LEFT)  send_message_screen_show();   // back
    else if (dir == LV_DIR_RIGHT) configuration_screen_show();  // forward
}

// Press-and-drag panning. Fires for every LVGL frame while the finger
// is moving on the map. lv_indev_get_vect returns the pixel delta since
// the previous event, which we convert to lat/lon shifts at the current
// zoom and apply to the view centre. First non-zero delta promotes us
// out of GPS-follow mode into s_manual_pan; the recentre button or a
// swipe-nav clears that flag.
static void on_pan(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) return;
    lv_point_t v;
    lv_indev_get_vect(indev, &v);
    if (v.x == 0 && v.y == 0) return;

    if (!s_manual_pan) {
        // Seed the view from the current GPS fix so the first drag
        // doesn't snap the map to the world origin.
        if (gps_screen_has_lock() && instance.gps.location.isValid()) {
            s_view_lat = instance.gps.location.lat();
            s_view_lon = instance.gps.location.lng();
        } else {
            // No fix - start from the last rendered centre, which is
            // sane (it was either GPS or our previous view value).
            s_view_lat = (s_last_lat < 360) ? s_last_lat : 0.0;
            s_view_lon = (s_last_lon < 360) ? s_last_lon : 0.0;
        }
        s_manual_pan = true;
    }
    // Drag finger right -> view shifts west (lon decreases).
    // Drag finger down  -> view shifts north (lat increases).
    s_view_lon -= (double)v.x * lon_per_px(s_zoom);
    s_view_lat += (double)v.y * lat_per_px(s_zoom, s_view_lat);
    refresh(true);
}

static void on_recentre(lv_event_t *)
{
    s_manual_pan = false;
    if (gps_screen_has_lock() && instance.gps.location.isValid()) {
        s_view_lat = instance.gps.location.lat();
        s_view_lon = instance.gps.location.lng();
    }
    refresh(true);
}

// Reflect the recorder state on the REC button (red + point count while active).
static void update_rec_btn()
{
    if (!rec_btn || !rec_lbl) return;
    if (gpxrec::active()) {
        lv_obj_set_style_bg_color(rec_btn, lv_color_make(0xE0, 0x20, 0x20), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(rec_btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_label_set_text_fmt(rec_lbl, "REC %d", gpxrec::points());
    } else {
        lv_obj_set_style_bg_color(rec_btn, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(rec_btn, LV_OPA_70, LV_PART_MAIN);
        lv_label_set_text(rec_lbl, "REC");
    }
}

static void on_rec(lv_event_t *)
{
    if (gpxrec::active()) gpxrec::stop();
    else                  gpxrec::start();
    update_rec_btn();
}

static void on_timer(lv_timer_t *)
{
    if (lv_screen_active() != map_screen) return;
    refresh(false);
    if (gpxrec::active()) update_rec_btn();   // keep the live point count fresh
}

// ---- layout ----------------------------------------------------------------

static lv_obj_t *make_round_btn(const char *text, lv_align_t align,
                                lv_coord_t ox, lv_coord_t oy,
                                lv_event_cb_t cb)
{
    lv_obj_t *b = lv_obj_create(map_screen);
    lv_obj_set_size(b, 52, 52);
    lv_obj_set_style_radius(b, 26, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(b, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(b, align, ox, oy);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_color(l, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, theme_text_font(28), LV_PART_MAIN);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

void map_screen_create()
{
    map_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(map_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(map_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(map_screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(map_screen, LV_OBJ_FLAG_SCROLLABLE);

    // 3x3 tile images — empty until refresh() loads them.
    for (int i = 0; i < TILES; i++) {
        tile_imgs[i] = lv_image_create(map_screen);
        lv_obj_set_size(tile_imgs[i], TILE_PX, TILE_PX);
        lv_obj_add_flag(tile_imgs[i], LV_OBJ_FLAG_HIDDEN);
        s_paths[i][0] = '\0';
    }

    // GPX track polyline — created after the tiles (draws over them) but before
    // the markers below (they draw over it). Full-screen so projected pixel
    // coordinates map 1:1; points are set each refresh by render_gpx_track().
    track_line = lv_line_create(map_screen);
    lv_obj_set_pos(track_line, 0, 0);
    lv_obj_set_size(track_line, MAP_W, MAP_H);
    lv_obj_set_style_line_color(track_line, lv_color_make(0xFF, 0x7A, 0x00), LV_PART_MAIN); // orange
    lv_obj_set_style_line_width(track_line, 5, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(track_line, true, LV_PART_MAIN);
    lv_obj_set_style_line_opa(track_line, LV_OPA_90, LV_PART_MAIN);
    lv_obj_clear_flag(track_line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(track_line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(track_line, LV_OBJ_FLAG_HIDDEN);

    // Centre marker — sits exactly over the watch's GPS location.
    marker = lv_obj_create(map_screen);
    lv_obj_set_size(marker, 14, 14);
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(marker, lv_color_make(0xFF, 0x33, 0x33), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(marker, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(marker, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(marker, 0, LV_PART_MAIN);
    lv_obj_clear_flag(marker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(marker);

    // Peer-node marker pool. Smaller (10x10) and blue so they're
    // visually distinct from the red GPS-centre marker. Labels are
    // 12-pt Montserrat white-on-translucent-black; positioned just
    // below the dot at refresh time. All hidden by default.
    for (int i = 0; i < MESH_MAX_NODES; i++) {
        node_dots[i] = lv_obj_create(map_screen);
        lv_obj_set_size(node_dots[i], 10, 10);
        lv_obj_set_style_radius(node_dots[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(node_dots[i], lv_color_make(0x0A, 0x84, 0xFF), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(node_dots[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(node_dots[i], lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_border_width(node_dots[i], 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(node_dots[i], 0, LV_PART_MAIN);
        lv_obj_clear_flag(node_dots[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(node_dots[i], LV_OBJ_FLAG_HIDDEN);

        node_labels[i] = lv_label_create(map_screen);
        lv_obj_set_style_text_color(node_labels[i], lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(node_labels[i], &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_bg_color(node_labels[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(node_labels[i], LV_OPA_60, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(node_labels[i], 3, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(node_labels[i], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(node_labels[i], 3, LV_PART_MAIN);
        lv_label_set_text(node_labels[i], "");
        lv_obj_add_flag(node_labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    // Info badge floating at the top: "MAP z<level> lat,lon"
    info_badge = lv_obj_create(map_screen);
    lv_obj_set_size(info_badge, LV_SIZE_CONTENT, 28);
    lv_obj_set_style_bg_color(info_badge, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(info_badge, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(info_badge, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(info_badge, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(info_badge, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(info_badge, 0, LV_PART_MAIN);
    lv_obj_clear_flag(info_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(info_badge, LV_ALIGN_TOP_MID, 0, 8);

    info_label = lv_label_create(info_badge);
    lv_obj_set_style_text_color(info_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(info_label, theme_text_font(14), LV_PART_MAIN);
    lv_label_set_text(info_label, "MAP");
    lv_obj_center(info_label);

    // Distance-remaining badge, just under the info badge; shown only while a
    // GPX track is loaded (orange text to echo the track colour).
    dist_badge = lv_obj_create(map_screen);
    lv_obj_set_size(dist_badge, LV_SIZE_CONTENT, 26);
    lv_obj_set_style_bg_color(dist_badge, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dist_badge, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(dist_badge, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(dist_badge, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(dist_badge, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(dist_badge, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dist_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(dist_badge, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_add_flag(dist_badge, LV_OBJ_FLAG_HIDDEN);

    dist_label = lv_label_create(dist_badge);
    lv_obj_set_style_text_color(dist_label, lv_color_make(0xFF, 0xB0, 0x50), LV_PART_MAIN);
    lv_obj_set_style_text_font(dist_label, theme_text_font(14), LV_PART_MAIN);
    lv_label_set_text(dist_label, "");
    lv_obj_center(dist_label);

    // Status overlay shown when there is no GPS fix.
    status_label = lv_label_create(map_screen);
    lv_obj_set_style_text_color(status_label, ARGUS_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(status_label, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(status_label, "No GPS fix\n(enable GPS to centre the map)");
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(status_label);

    // BOTTOM_LEFT / BOTTOM_RIGHT would land the buttons at roughly
    // (38, 464) and (372, 464) on the 410x502 framebuffer - both well
    // outside the ~410-diameter visible disc (centred at 205,251 with
    // radius ~205). Pull them inward to a 7 o'clock / 5 o'clock pair
    // centred on the screen: a 164 px radial offset keeps the button
    // edge ~15 px clear of the bezel and well inside the y=60..440
    // safe-content band documented in the README.
    zoom_out_btn = make_round_btn("-", LV_ALIGN_CENTER, -110, 215, on_zoom_out);
    zoom_in_btn  = make_round_btn("+", LV_ALIGN_CENTER,  110, 215, on_zoom_in);

    // Recentre / "lock to GPS" button - sits between the two zoom
    // buttons so the trio reads as a single map-control cluster.
    // GPS pin icon from the built-in LVGL symbol set.
    recentre_btn = make_round_btn(LV_SYMBOL_GPS, LV_ALIGN_CENTER, 0, 215,
                                  on_recentre);

    // Track-recording toggle, top-left (below the corner curve, clear of the
    // centred info/distance badges). Red with a live point count while active.
    rec_btn = lv_obj_create(map_screen);
    lv_obj_set_size(rec_btn, 84, 38);
    lv_obj_set_style_radius(rec_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rec_btn, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rec_btn, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(rec_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(rec_btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(rec_btn, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rec_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(rec_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(rec_btn, LV_ALIGN_TOP_LEFT, 16, 74);
    lv_obj_add_event_cb(rec_btn, on_rec, LV_EVENT_CLICKED, NULL);

    rec_lbl = lv_label_create(rec_btn);
    lv_obj_set_style_text_color(rec_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(rec_lbl, theme_text_font(14), LV_PART_MAIN);
    lv_label_set_text(rec_lbl, "REC");
    lv_obj_center(rec_lbl);
    update_rec_btn();

    lv_obj_add_event_cb(map_screen, on_gesture, LV_EVENT_GESTURE, NULL);
    // Press-and-drag panning runs alongside swipe-nav. Fast directional
    // flicks still fire LV_EVENT_GESTURE for nav; slow drags accumulate
    // in on_pan via the indev vector.
    lv_obj_add_event_cb(map_screen, on_pan, LV_EVENT_PRESSING, NULL);
    lv_timer_create(on_timer, 2000, NULL);
}

// Case-insensitive ".gpx" suffix test.
static bool ends_with_gpx(const char *s)
{
    size_t L = strlen(s);
    if (L < 4) return false;
    const char *e = s + L - 4;
    return e[0] == '.' &&
           (e[1] == 'g' || e[1] == 'G') &&
           (e[2] == 'p' || e[2] == 'P') &&
           (e[3] == 'x' || e[3] == 'X');
}

// Load a specific GPX file and refresh the overlay. Public so a picker (later
// lot) can drive it; empty/null path clears the track.
void map_screen_load_gpx(const char *sd_path)
{
    if (sd_path && sd_path[0]) gpx::load(sd_path);
    else                       gpx::clear();
    if (map_screen) refresh(true);
}

// Auto-load the first /gpx/*.gpx once per boot so the overlay works without a
// picker; a picker can later override via map_screen_load_gpx().
static void autoload_gpx()
{
    if (s_gpx_autoload_tried || gpx::loaded()) return;
    s_gpx_autoload_tried = true;
    if (!instance.isCardReady() || !SD.exists("/gpx")) return;
    File dir = SD.open("/gpx");
    if (!dir) return;
    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
        const char *nm    = e.name();
        const char *slash = strrchr(nm, '/');
        const char *base  = slash ? slash + 1 : nm;
        if (!e.isDirectory() && ends_with_gpx(base)) {
            char path[96];
            snprintf(path, sizeof(path), "/gpx/%s", base);
            e.close();
            dir.close();
            gpx::load(path);
            return;
        }
        e.close();
    }
    dir.close();
}

void map_screen_show()
{
    main_loop_request_lvgl_priority(12);
    detect_zooms();
    autoload_gpx();
    if (s_zoom < s_zoom_min) s_zoom = s_zoom_min;
    if (s_zoom > s_zoom_max) s_zoom = s_zoom_max;
    // Pull the persisted last-known fix once (no-op on subsequent
    // entries). Lets the map open at the place we last had a position
    // even if GPS is currently off or unlocked.
    load_last_fix();
    refresh(true);
    lv_scr_load(map_screen);
}

bool map_screen_is_active()
{
    return lv_screen_active() == map_screen;
}

bool map_screen_available()
{
    if (!instance.isCardReady()) return false;
    return SD.exists("/map");
}

// ---- GPX picker -------------------------------------------------------------
// A simple full-screen list of the .gpx files in /gpx on the SD card. Tapping a
// row loads that track and jumps to the map; a "Clear track" row removes the
// overlay. Rebuilt fresh on each open (the previous one is async-deleted, so no
// object is cleaned from inside a click handler). Reachable as its own launcher
// entry ("GPX Track").
#define GPX_MAX_FILES 20
static PsramArray<char[96], GPX_MAX_FILES> s_gpx_paths;   // persistent row payloads (PSRAM)
static lv_obj_t *s_picker = nullptr;

static void on_pick_clicked(lv_event_t *e)
{
    const char *path = (const char *)lv_event_get_user_data(e);
    map_screen_load_gpx((path && path[0]) ? path : nullptr);
    map_screen_show();
}

static void picker_row(lv_obj_t *parent, const char *text, const char *path)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_set_height(b, 48);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1A1A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 8, LV_PART_MAIN);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, on_pick_clicked, LV_EVENT_CLICKED, (void *)path);

    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_color(l, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, theme_text_font(20), LV_PART_MAIN);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
}

static void on_picker_gesture(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_event_get_indev(e));
    if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT || dir == LV_DIR_TOP) map_screen_show();
}

void map_gpx_picker_show()
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, on_picker_gesture, LV_EVENT_GESTURE, NULL);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, theme_text_font(20), LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(0x9A9A9A), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(title, 3, LV_PART_MAIN);
    lv_label_set_text(title, "GPX TRACKS");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 360, 384);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 8, LV_PART_MAIN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    s_gpx_paths[0][0] = '\0';
    picker_row(list, "Clear track", s_gpx_paths[0]);

    int idx = 1;
    if (instance.isCardReady() && SD.exists("/gpx")) {
        File dir = SD.open("/gpx");
        if (dir) {
            for (File e = dir.openNextFile(); e && idx < GPX_MAX_FILES; e = dir.openNextFile()) {
                const char *nm    = e.name();
                const char *slash = strrchr(nm, '/');
                const char *base  = slash ? slash + 1 : nm;
                if (!e.isDirectory() && ends_with_gpx(base)) {
                    snprintf(s_gpx_paths[idx], sizeof(s_gpx_paths[idx]), "/gpx/%s", base);
                    picker_row(list, base, s_gpx_paths[idx]);
                    idx++;
                }
                e.close();
            }
            dir.close();
        }
    }
    if (idx == 1) {
        lv_obj_t *empty = lv_label_create(list);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x9A9A9A), LV_PART_MAIN);
        lv_obj_set_style_text_font(empty, theme_text_font(16), LV_PART_MAIN);
        lv_label_set_text(empty, "No .gpx in /gpx on the SD card.");
    }

    lv_scr_load(scr);
    if (s_picker) lv_obj_delete_async(s_picker);
    s_picker = scr;
}
