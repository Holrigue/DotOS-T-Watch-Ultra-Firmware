// gpx_track.cpp — see gpx_track.h.
//
// A dependency-free streaming reader: it scans the file for ` lat="…"` /
// ` lon="…"` attributes (the shape shared by <trkpt>/<rtept>/<wpt>) and pairs
// them per tag. A 6-char sliding window makes the match boundary-safe across
// SD read chunks and, by requiring a non-letter before the token, skips the
// <bounds minlat/maxlat/minlon/maxlon> header. Points land in a PSRAM buffer,
// decimated on the fly so an arbitrarily long track always fits and stays cheap.
#include "gpx_track.h"

#include <LilyGoLib.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

namespace {

constexpr int MAXPTS = 3000;          // 3000 * 2 floats = 24 KB PSRAM

float   *s_lat      = nullptr;
float   *s_lon      = nullptr;
int      s_count    = 0;
int      s_stride   = 1;              // streaming decimation: keep 1 of every s_stride
int      s_since    = 0;              // points seen since the last kept one
double   s_total_km = 0.0;
char     s_name[32] = "";

double deg2rad(double d) { return d * 0.017453292519943295; }

// Great-circle distance in km (haversine).
double haversine_km(double lat1, double lon1, double lat2, double lon2)
{
    double dlat = deg2rad(lat2 - lat1);
    double dlon = deg2rad(lon2 - lon1);
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(deg2rad(lat1)) * cos(deg2rad(lat2)) * sin(dlon / 2) * sin(dlon / 2);
    return 6371.0 * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

bool ensure_buffer()
{
    if (s_lat && s_lon) return true;
    s_lat = (float *)heap_caps_malloc(MAXPTS * sizeof(float), MALLOC_CAP_SPIRAM);
    s_lon = (float *)heap_caps_malloc(MAXPTS * sizeof(float), MALLOC_CAP_SPIRAM);
    return s_lat && s_lon;
}

// Halve the stored track in place (keep every other point) and double the
// keep-stride, so further points are subsampled to match. Called when full.
void compact()
{
    int w = 0;
    for (int r = 0; r < s_count; r += 2) { s_lat[w] = s_lat[r]; s_lon[w] = s_lon[r]; w++; }
    s_count  = w;
    s_stride *= 2;
}

void push_point(double lat, double lon)
{
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return;
    if (++s_since < s_stride) return;     // subsample per the current stride
    s_since = 0;
    if (s_count >= MAXPTS) compact();
    s_lat[s_count] = (float)lat;
    s_lon[s_count] = (float)lon;
    s_count++;
}

}  // namespace

namespace gpx {

void clear()
{
    s_count = 0; s_stride = 1; s_since = 0; s_total_km = 0.0; s_name[0] = '\0';
}

bool loaded() { return s_count > 0; }
int  count()  { return s_count; }
double lat(int i) { return (i >= 0 && i < s_count) ? (double)s_lat[i] : 0.0; }
double lon(int i) { return (i >= 0 && i < s_count) ? (double)s_lon[i] : 0.0; }
const char *name() { return s_name; }

bool load(const char *sd_path)
{
    clear();
    if (!sd_path || !ensure_buffer()) return false;
    if (!instance.isCardReady()) return false;
    File f = SD.open(sd_path, FILE_READ);
    if (!f) return false;

    // Basename for the badge.
    const char *base = strrchr(sd_path, '/');
    base = base ? base + 1 : sd_path;
    strncpy(s_name, base, sizeof(s_name) - 1);
    s_name[sizeof(s_name) - 1] = '\0';

    char    win[6] = {0};        // last 6 chars seen (win[5] = newest)
    bool    reading = false;     // currently accumulating a number
    bool    is_lat  = false;     // which attribute the number is for
    char    num[24]; int numn = 0;
    bool    have_lat = false, have_lon = false;
    double  plat = 0, plon = 0;

    uint8_t buf[512];
    int n;
    while ((n = f.read(buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];

            if (reading) {
                if (c == '"') {
                    num[numn] = '\0';
                    double v = atof(num);
                    if (is_lat) { plat = v; have_lat = true; }
                    else        { plon = v; have_lon = true; }
                    if (have_lat && have_lon) { push_point(plat, plon); have_lat = have_lon = false; }
                    reading = false;
                } else if (numn < (int)sizeof(num) - 1 &&
                           (isdigit((unsigned char)c) || c == '.' || c == '-' ||
                            c == '+' || c == 'e' || c == 'E')) {
                    num[numn++] = c;
                }
                // keep the window coherent even while reading a number
                memmove(win, win + 1, 5); win[5] = c;
                continue;
            }

            memmove(win, win + 1, 5); win[5] = c;
            // Match ` lat="` / ` lon="`: the 5 chars win[1..5], with win[0] a
            // non-letter so "minlat=/maxlon=" don't trip it.
            if (!isalpha((unsigned char)win[0]) && win[4] == '=' && win[5] == '"' &&
                win[1] == 'l' && win[3] == 't' /* la?t */ ) {
                if (win[2] == 'a') { reading = true; is_lat = true;  numn = 0; }
            }
            if (!isalpha((unsigned char)win[0]) && win[4] == '=' && win[5] == '"' &&
                win[1] == 'l' && win[2] == 'o' && win[3] == 'n') {
                reading = true; is_lat = false; numn = 0;
            }
        }
    }
    f.close();

    // Precompute total length once.
    s_total_km = 0.0;
    for (int i = 1; i < s_count; i++)
        s_total_km += haversine_km(s_lat[i - 1], s_lon[i - 1], s_lat[i], s_lon[i]);

    return s_count > 0;
}

bool bounds(double *min_lat, double *min_lon, double *max_lat, double *max_lon)
{
    if (s_count == 0) return false;
    double a = s_lat[0], b = s_lat[0], c = s_lon[0], d = s_lon[0];
    for (int i = 1; i < s_count; i++) {
        if (s_lat[i] < a) a = s_lat[i];
        if (s_lat[i] > b) b = s_lat[i];
        if (s_lon[i] < c) c = s_lon[i];
        if (s_lon[i] > d) d = s_lon[i];
    }
    if (min_lat) *min_lat = a;
    if (max_lat) *max_lat = b;
    if (min_lon) *min_lon = c;
    if (max_lon) *max_lon = d;
    return true;
}

double total_km() { return s_total_km; }

double remaining_km(double cur_lat, double cur_lon)
{
    if (s_count < 2) return 0.0;
    // Nearest stored point to the current position.
    int    best = 0;
    double bestd = 1e18;
    for (int i = 0; i < s_count; i++) {
        double dd = haversine_km(cur_lat, cur_lon, s_lat[i], s_lon[i]);
        if (dd < bestd) { bestd = dd; best = i; }
    }
    double rem = 0.0;
    for (int i = best + 1; i < s_count; i++)
        rem += haversine_km(s_lat[i - 1], s_lon[i - 1], s_lat[i], s_lon[i]);
    return rem;
}

}  // namespace gpx
