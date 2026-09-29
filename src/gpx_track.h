#pragma once
#include <stdint.h>
#include <stddef.h>

// gpx_track — loads a GPX file from the SD card and exposes its points for the
// map overlay (hiking track follow). Points are stored in a PSRAM buffer; a very
// long track is decimated on load so it always fits and stays cheap to draw.
//
// Deliberately tiny and dependency-free: a streaming attribute scan pulls every
// lat="…"/lon="…" pair out of <trkpt>/<rtept>/<wpt> tags (the same shape for all
// three), so it handles tracks, routes and waypoints without a full XML parser.
namespace gpx {

// Parse a .gpx at an Arduino-SD path ("/gpx/hike.gpx"). Replaces any previously
// loaded track. Returns true and a non-zero count() on success.
bool load(const char *sd_path);

// Forget the current track (frees nothing; the PSRAM buffer is reused).
void clear();

bool   loaded();
int    count();
double lat(int i);
double lon(int i);
const char *name();     // basename of the loaded file, for the map badge

// Bounding box of the track (false when empty). Lets the map "zoom to fit".
bool bounds(double *min_lat, double *min_lon, double *max_lat, double *max_lon);

// Full track length in kilometres (0 when empty).
double total_km();

// Distance in kilometres from the track point nearest (cur_lat,cur_lon) onward
// to the end — the "distance remaining" for a hiker following the track.
double remaining_km(double cur_lat, double cur_lon);

}  // namespace gpx
