// face_watch.cpp - see face_watch.h.
#include "face_watch.h"

#include <Preferences.h>

namespace {

const char *const NS = "argusface";

FaceHourFont  s_hour  = FACE_HOUR_DOTS;
FaceDateFont  s_date  = FACE_DATE_ORBITRON;
FaceAccent    s_acc   = FACE_ACC_RED;
FaceDateOrder s_order = FACE_ORDER_DMY;

template <typename E>
E sane(uint8_t v, uint8_t count, E dflt)
{
    return (v < count) ? (E)v : dflt;
}

void persist()
{
    Preferences p;
    if (p.begin(NS, false)) {
        p.putUChar("hour",  (uint8_t)s_hour);
        p.putUChar("date",  (uint8_t)s_date);
        p.putUChar("acc",   (uint8_t)s_acc);
        p.putUChar("order", (uint8_t)s_order);
        p.end();
    }
}

}  // namespace

void face_watch_boot_restore()
{
    Preferences p;
    if (p.begin(NS, true)) {
        s_hour  = sane<FaceHourFont>(p.getUChar("hour",  FACE_HOUR_DOTS),     FACE_HOUR__COUNT,  FACE_HOUR_DOTS);
        s_date  = sane<FaceDateFont>(p.getUChar("date",  FACE_DATE_ORBITRON), FACE_DATE__COUNT,  FACE_DATE_ORBITRON);
        s_acc   = sane<FaceAccent>  (p.getUChar("acc",   FACE_ACC_RED),       FACE_ACC__COUNT,   FACE_ACC_RED);
        s_order = sane<FaceDateOrder>(p.getUChar("order", FACE_ORDER_DMY),    FACE_ORDER__COUNT, FACE_ORDER_DMY);
        p.end();
    }
}

FaceHourFont  face_hour_font()  { return s_hour; }
FaceDateFont  face_date_font()  { return s_date; }
FaceAccent    face_accent()     { return s_acc; }
FaceDateOrder face_date_order() { return s_order; }

void face_set_hour_font(FaceHourFont v)  { s_hour  = (v < FACE_HOUR__COUNT)  ? v : FACE_HOUR_DOTS;     persist(); }
void face_set_date_font(FaceDateFont v)  { s_date  = (v < FACE_DATE__COUNT)  ? v : FACE_DATE_ORBITRON; persist(); }
void face_set_accent(FaceAccent v)       { s_acc   = (v < FACE_ACC__COUNT)   ? v : FACE_ACC_RED;       persist(); }
void face_set_date_order(FaceDateOrder v){ s_order = (v < FACE_ORDER__COUNT) ? v : FACE_ORDER_DMY;     persist(); }

uint32_t face_accent_rgb()
{
    switch (s_acc) {
        case FACE_ACC_WHITE: return 0xFFFFFF;
        case FACE_ACC_GREY:  return 0x9A9A9A;
        case FACE_ACC_AMBER: return 0xF0A020;
        case FACE_ACC_BLUE:  return 0x9BBCD6;   // steel-blue (ARGUS accent)
        case FACE_ACC_RED:
        default:             return 0xE02020;   // dot face red
    }
}
