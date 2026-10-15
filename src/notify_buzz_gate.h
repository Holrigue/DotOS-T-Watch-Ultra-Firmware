#pragma once
#include <stdint.h>
//
// notify_buzz_gate.h - a minimum gap between notification buzzes, so no app can
// make the watch vibrate continuously. Pure and host-tested
// (test/test_notify_buzz_gate.cpp). Incoming calls are exempt (they have their
// own mute/ring handling); this only rate-limits ordinary notification buzzes.
//
// A phone app that updates a notification many times a second (e.g. a live voice
// transcription) used to buzz the watch on every update because each new text
// is a fresh notification to the firmware. This caps that to one buzz per gap.

// Returns true if a buzz is allowed now, updating `last_buzz_ms` when it is.
// Uses unsigned subtraction so it stays correct across the millis() wrap.
// `last_buzz_ms == 0` means "never buzzed yet" and always allows.
inline bool notify_buzz_gate_allow(uint32_t &last_buzz_ms, uint32_t now_ms, uint32_t min_gap_ms)
{
    if (last_buzz_ms != 0 && (uint32_t)(now_ms - last_buzz_ms) < min_gap_ms) return false;
    last_buzz_ms = now_ms == 0 ? 1u : now_ms;   // keep 0 reserved for "never"
    return true;
}
