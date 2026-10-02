// notify_center.cpp - see notify_center.h.
#include "notify_center.h"
#include "notify_log.h"
#include "../haptic.h"     // haptic_alert_soft() - gentler ring for incoming calls
#include <Arduino.h>
#include <LilyGoLib.h>

namespace notify {

NotificationStore& center()
{
    static NotificationStore s_store;
    return s_store;
}

// Cross-task hand-off of the newest arrival for the on-screen banner. Written on
// the BLE task in publish(), read on the UI thread in take_pending(). Guarded by
// a spinlock so the struct copy can't tear across the two cores.
static portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_pending = false;
static Notification  s_pending_notif;

// millis() deadline until which incoming-call buzzes are suppressed (see
// mute_call). 0 = not muted. Set on the UI thread, read on the BLE task.
static constexpr uint32_t CALL_MUTE_MS = 120000;   // ~2 min covers one call
static volatile uint32_t  s_call_mute_until = 0;

bool take_pending(Notification& out)
{
    if (!s_pending) return false;   // cheap early-out, no lock on the common path
    portENTER_CRITICAL(&s_mux);
    out = s_pending_notif;
    s_pending = false;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

// Strip characters the UI font cannot render (emoji, symbols, CJK, ...), which
// otherwise show as a "tofu" box. The label fonts cover ASCII + Latin (accents),
// so we keep code points up to Latin Extended-B, fold a few common typographic
// marks to ASCII, and drop everything else. Rewrites the UTF-8 string in place;
// output is never longer than the input, so two cursors are safe.
static void sanitize(char *s)
{
    const unsigned char *r = (const unsigned char *)s;
    char *w = s;
    while (*r) {
        unsigned char c = *r;
        uint32_t cp;
        int len;
        if (c < 0x80)                { cp = c;        len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { r++; continue; }   // stray continuation / invalid lead byte -> drop

        bool ok = true;
        for (int i = 1; i < len; i++) {
            if ((r[i] & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (r[i] & 0x3F);
        }
        if (!ok) { r++; continue; }   // truncated sequence -> drop one byte, resync

        if (cp < 0x20 || cp == 0x7F) {
            *w++ = ' ';                         // control char -> space
        } else if (cp < 0x80) {
            *w++ = (char)cp;                    // ASCII printable
        } else if (cp == 0x2018 || cp == 0x2019) {
            *w++ = '\'';                        // curly single quotes
        } else if (cp == 0x201C || cp == 0x201D) {
            *w++ = '"';                         // curly double quotes
        } else if (cp == 0x2013 || cp == 0x2014) {
            *w++ = '-';                         // en / em dash
        } else if (cp == 0x2026) {
            *w++ = '.'; *w++ = '.'; *w++ = '.'; // ellipsis (same 3 bytes in)
        } else if (cp <= 0x024F) {
            for (int i = 0; i < len; i++) *w++ = (char)r[i];   // Latin, keep bytes
        }
        // else: emoji / symbol / CJK / unsupported -> drop entirely.
        r += len;
    }
    *w = '\0';
}

void publish(Notification n)
{
    // Stamp arrival time in seconds since boot. The UI shows relative age
    // ("2m ago"); absolute wall-clock is not needed and avoids depending on a
    // valid RTC. millis() wraps after ~49 days, acceptable for a glance list.
    n.epoch = (uint32_t)(millis() / 1000);

    // Drop unrenderable glyphs (emoji, symbols) so they never show as tofu boxes.
    sanitize(n.app);
    sanitize(n.title);
    sanitize(n.body);

    bool is_update = center().contains(n.uid);
    center().add(n);

    // Buzz once on a genuinely new notification, not on in-place content updates
    // (a phone re-sends on edit; we do not want a second buzz for that). Incoming
    // calls are exempt while muted: each re-ring is a fresh (synthetic-uid) arrival
    // so it would otherwise keep buzzing after the call was answered elsewhere.
    if (!is_update) {
        bool is_call = (n.category == Category::IncomingCall);
        bool muted_call = is_call && (int32_t)(s_call_mute_until - millis()) > 0;
        if (!muted_call) {
            // Incoming calls get the soft "wake" tap (restored right after) so a
            // ring feels calm; everything else keeps the user's buzz setting.
            if (is_call) haptic_alert_soft();
            else         instance.vibrator();
        }
    }

    // Debug-only serial mirror (NLOG compiles out unless ARGUS_NOTIFY_DEBUG is
    // set). This prints notification CONTENT, so it stays OFF in normal builds to
    // keep message titles/bodies/senders off the USB serial port.
    NLOG("[notify] uid=%lu cat=%u app=\"%s\" title=\"%s\" body=\"%s\"\n",
                  (unsigned long)n.uid, (unsigned)n.category,
                  n.app, n.title, n.body);

    // Stash for the on-screen banner (drained on the UI thread). Only for new
    // arrivals, not in-place content updates, so we don't re-pop an edit.
    if (!is_update) {
        portENTER_CRITICAL(&s_mux);
        s_pending_notif = n;
        s_pending = true;
        portEXIT_CRITICAL(&s_mux);
    }
}

void retract(uint32_t uid)
{
    if (center().remove_uid(uid)) {
        NLOG("[notify] retract uid=%lu\n", (unsigned long)uid);
    }
}

void clear_all()
{
    center().clear();
}

void mute_call()
{
    uint32_t until = millis() + CALL_MUTE_MS;
    if (until == 0) until = 1;   // 0 is the "not muted" sentinel
    s_call_mute_until = until;
}

}  // namespace notify
