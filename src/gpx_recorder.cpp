// gpx_recorder.cpp — see gpx_recorder.h.
#include "gpx_recorder.h"
#include "gps_screen.h"

#include <LilyGoLib.h>
#include <SD.h>
#include <lvgl.h>
#include <math.h>
#include <stdio.h>

namespace {

File        s_file;
bool        s_active   = false;
int         s_points   = 0;
char        s_path[64] = "";
double      s_last_lat = 0, s_last_lon = 0;
bool        s_have_last = false;
uint32_t    s_last_ms  = 0;
lv_timer_t *s_timer    = nullptr;

constexpr uint32_t MIN_INTERVAL_MS = 15000;   // force a point at least this often
constexpr double   MIN_MOVE_M      = 5.0;     // …or once moved this far

double move_m(double la1, double lo1, double la2, double lo2)
{
    double dlat = (la2 - la1) * 0.017453292519943295;
    double dlon = (lo2 - lo1) * 0.017453292519943295;
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(la1 * 0.017453292519943295) * cos(la2 * 0.017453292519943295) *
               sin(dlon / 2) * sin(dlon / 2);
    return 6371000.0 * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

void timer_cb(lv_timer_t *) { gpxrec::tick(); }

}  // namespace

namespace gpxrec {

bool active()      { return s_active; }
int  points()      { return s_points; }
const char *path() { return s_path; }

bool start()
{
    if (s_active) return true;
    if (!instance.isCardReady()) return false;
    if (!SD.exists("/gpx")) SD.mkdir("/gpx");

    struct tm t;
    instance.rtc.getDateTime(&t);
    snprintf(s_path, sizeof(s_path), "/gpx/rec_%04d%02d%02d-%02d%02d%02d.gpx",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);

    s_file = SD.open(s_path, FILE_WRITE);
    if (!s_file) { s_path[0] = '\0'; return false; }
    s_file.print("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                 "<gpx version=\"1.1\" creator=\"DotOS\" "
                 "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
                 "<trk><name>DotOS hike</name><trkseg>\n");
    s_file.flush();

    s_points    = 0;
    s_have_last = false;
    s_last_ms   = 0;
    s_active    = true;
    if (!s_timer) s_timer = lv_timer_create(timer_cb, 1000, nullptr);
    return true;
}

void stop()
{
    if (!s_active) return;
    s_active = false;
    if (s_timer) { lv_timer_del(s_timer); s_timer = nullptr; }
    if (s_file) {
        s_file.print("</trkseg></trk></gpx>\n");
        s_file.flush();
        s_file.close();
    }
}

void tick()
{
    if (!s_active || !s_file) return;
    if (!gps_screen_has_lock() || !instance.gps.location.isValid()) return;

    double lat = instance.gps.location.lat();
    double lon = instance.gps.location.lng();
    uint32_t now = lv_tick_get();

    if (s_have_last) {
        bool moved = move_m(s_last_lat, s_last_lon, lat, lon) >= MIN_MOVE_M;
        bool timed = (now - s_last_ms) >= MIN_INTERVAL_MS;
        if (!moved && !timed) return;
    }

    struct tm t;
    instance.rtc.getDateTime(&t);   // RTC holds UTC
    char line[160];
    int n = snprintf(line, sizeof(line),
        "<trkpt lat=\"%.6f\" lon=\"%.6f\">", lat, lon);
    if (instance.gps.altitude.isValid())
        n += snprintf(line + n, sizeof(line) - n, "<ele>%.1f</ele>",
                      instance.gps.altitude.meters());
    n += snprintf(line + n, sizeof(line) - n,
        "<time>%04d-%02d-%02dT%02d:%02d:%02dZ</time></trkpt>\n",
        t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);

    if (s_file.print(line) == 0) { stop(); return; }   // write failed (card gone)
    if ((s_points % 8) == 0) s_file.flush();

    s_last_lat = lat; s_last_lon = lon; s_last_ms = now; s_have_last = true;
    s_points++;
}

}  // namespace gpxrec
