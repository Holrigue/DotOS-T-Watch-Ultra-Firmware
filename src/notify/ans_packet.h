#pragma once
// ans_packet.h - small pure helpers for the Alert Notification Service packets the Android
// companion app writes (host-tested in test/test_ans_packet.cpp).
//
// New Alert layout: [category][count] then 0x00-separated UTF-8 strings (title, body).
// The app also uses it to say "this call is over": category 3 (incoming call) with count 0.
// A phone that answers or rejects a call, or one answered on another device (Teams on a PC),
// removes the call notification; without this the watch kept ringing until its own timeout.
#include <cstddef>
#include <cstdint>

constexpr uint8_t ANS_CAT_INCOMING_CALL = 3;

// True for the "call ended" packet: incoming-call category with a zero count.
inline bool ans_is_call_end(const uint8_t *d, size_t len)
{
    return d && len >= 2 && d[0] == ANS_CAT_INCOMING_CALL && d[1] == 0;
}
