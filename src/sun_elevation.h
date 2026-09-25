#pragma once
//
// sun_elevation.h - solar elevation angle for the automatic day/night
// brightness. Pure math (no Arduino, no LVGL) so it stays host-testable.
//
// NOAA "General Solar Position Calculations" (fractional-year form): accurate
// to well under a degree, far more than a brightness ramp needs. Inputs are the
// UTC date/time (the RTC always holds UTC) and the position in degrees, east
// and north positive.
#include <math.h>

// Degrees above the horizon: > 0 sun up, 0..-6 civil twilight, < -6 night.
static inline double sun_elevation_deg(double lat_deg, double lon_deg,
                                       int year, int yday /*0-based*/,
                                       int hour, int minute, int second)
{
    const double kPi = 3.14159265358979323846;
    const double D2R = kPi / 180.0;
    bool   leap  = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    double days  = leap ? 366.0 : 365.0;
    double g     = 2.0 * kPi / days * (yday + (hour - 12) / 24.0);   // fractional year

    double eqtime = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g)
                              - 0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
    double decl = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g)
                - 0.006758 * cos(2 * g) + 0.000907 * sin(2 * g)
                - 0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);

    double minutes = hour * 60.0 + minute + second / 60.0;
    double tst     = minutes + eqtime + 4.0 * lon_deg;   // true solar time, minutes
    double ha      = (tst / 4.0 - 180.0) * D2R;          // hour angle

    double lat    = lat_deg * D2R;
    double cos_zn = sin(lat) * sin(decl) + cos(lat) * cos(decl) * cos(ha);
    if (cos_zn > 1.0)  cos_zn = 1.0;
    if (cos_zn < -1.0) cos_zn = -1.0;
    return 90.0 - acos(cos_zn) / D2R;
}

// Brightness multiplier for a solar elevation: full in daylight, `night` once
// the sun is 6 degrees below the horizon (end of civil twilight, when it is
// dark outside), and a straight ramp through twilight in between.
static inline float sun_brightness_factor(double elevation_deg, float night)
{
    if (elevation_deg >= 0.0)  return 1.0f;
    if (elevation_deg <= -6.0) return night;
    float t = (float)((elevation_deg + 6.0) / 6.0);   // 0 at -6 deg, 1 at horizon
    return night + (1.0f - night) * t;
}
