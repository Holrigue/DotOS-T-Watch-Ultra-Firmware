// face_watch.cpp - see face_watch.h.
#include "face_watch.h"

#include <Preferences.h>

namespace {

const char *const NS = "argusface";

FaceHourFont  s_hour  = FACE_HOUR_DOTS;
FaceDateFont  s_date  = FACE_DATE_ORBITRON;
FaceAccent    s_acc   = FACE_ACC_ORANGE;   // the DotOS design accent
FaceDateOrder s_order = FACE_ORDER_DMY;
FaceTextFont  s_text  = FACE_TEXT_DEFAULT;

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
        p.putUChar("text",  (uint8_t)s_text);
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
        s_acc   = sane<FaceAccent>  (p.getUChar("acc",   FACE_ACC_ORANGE),    FACE_ACC__COUNT,   FACE_ACC_ORANGE);
        s_order = sane<FaceDateOrder>(p.getUChar("order", FACE_ORDER_DMY),    FACE_ORDER__COUNT, FACE_ORDER_DMY);
        s_text  = sane<FaceTextFont> (p.getUChar("text",  FACE_TEXT_DEFAULT), FACE_TEXT__COUNT,  FACE_TEXT_DEFAULT);
        p.end();
        // The steel-blue accent was retired (too close to Grey): a watch that had
        // it saved falls back to Grey, the nearest remaining colour.
        if ((uint8_t)s_acc == FACE_ACC_RETIRED_STEEL_BLUE) s_acc = FACE_ACC_GREY;
    }
}

FaceHourFont  face_hour_font()  { return s_hour; }
FaceDateFont  face_date_font()  { return s_date; }
FaceAccent    face_accent()     { return s_acc; }
FaceDateOrder face_date_order() { return s_order; }
FaceTextFont  face_text_font()  { return s_text; }

void face_set_hour_font(FaceHourFont v)  { s_hour  = (v < FACE_HOUR__COUNT)  ? v : FACE_HOUR_DOTS;     persist(); }
void face_set_date_font(FaceDateFont v)  { s_date  = (v < FACE_DATE__COUNT)  ? v : FACE_DATE_ORBITRON; persist(); }
void face_set_accent(FaceAccent v)
{
    if ((uint8_t)v == FACE_ACC_RETIRED_STEEL_BLUE) v = FACE_ACC_GREY;
    s_acc = (v < FACE_ACC__COUNT) ? v : FACE_ACC_ORANGE;
    persist();
}
void face_set_date_order(FaceDateOrder v){ s_order = (v < FACE_ORDER__COUNT) ? v : FACE_ORDER_DMY;     persist(); }
void face_set_text_font(FaceTextFont v)  { s_text  = (v < FACE_TEXT__COUNT)  ? v : FACE_TEXT_DEFAULT;  persist(); }

uint32_t face_accent_rgb_of(FaceAccent a)
{
    switch (a) {
        case FACE_ACC_GREY:  return 0x9A9A9A;
        case FACE_ACC_AMBER: return 0xF0A020;   // "pale orange"
        case FACE_ACC_CYAN:  return 0x00E5FF;   // bright cyan
        case FACE_ACC_GREEN: return 0x2BFF66;   // vivid Pip-Boy green
        case FACE_ACC_ORANGE:return 0xFF5A1A;   // DotOS design accent
        case FACE_ACC_PURPLE:return 0xB05CFF;   // atomic purple
        case FACE_ACC_RED:
        default:             return 0xE02020;   // dot face red
    }
}

const char *face_accent_name(FaceAccent a)
{
    switch (a) {
        case FACE_ACC_GREY:  return "Grey";
        case FACE_ACC_AMBER: return "Pale orange";
        case FACE_ACC_CYAN:  return "Cyan";
        case FACE_ACC_GREEN: return "Green";
        case FACE_ACC_ORANGE:return "Orange";
        case FACE_ACC_PURPLE:return "Atomic purple";
        case FACE_ACC_RED:
        default:             return "Red";
    }
}

uint32_t face_accent_rgb() { return face_accent_rgb_of(s_acc); }
