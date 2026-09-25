// ans.h - Android notification mirroring via the BLE Alert Notification Service.
//
// Android has no ANCS equivalent, so we ride Gadgetbridge (open-source, F-Droid).
// The watch presents itself as an InfiniTime/PineTime (a device Gadgetbridge
// already supports) by exposing the standard Alert Notification Service (0x1811)
// as a GATT SERVER. Gadgetbridge writes each phone notification to the New Alert
// characteristic (0x2A46); we parse it and push it into notify::center().
//
// No ARGUS app to build or maintain - the user installs Gadgetbridge and it
// forwards notifications to us over the standard service.
//
// This is the mirror image of ancs.h: there the watch is a GATT client to iOS;
// here the watch is a GATT server to the Android phone. The Daily-wear/Field-tool
// mode owner starts exactly one of {ancs, ans} at a time (never both), so they do
// not contend for the radio.
#pragma once

#include <cstdint>   // uint8_t in the find API below

namespace ans {

// Bring up BLE (guarded against WiFi), expose the Alert Notification Service, and
// advertise as an InfiniTime so Gadgetbridge detects and drives us. Returns false
// if WiFi is up or the stack fails. Idempotent.
bool start();

// Tear the server + stack down. Idempotent.
void stop();

bool is_running();

// True once a phone (Gadgetbridge) has connected.
bool is_connected();

// --- Find My Watch / Find My Phone (companion app only) -----------------------
// A small bidirectional "find" channel on a dedicated characteristic. The phone
// WRITES an op to ring the watch; the watch NOTIFIES an op to ring the phone.
// Op 0x01 = start ringing, 0x00 = stop. Gadgetbridge ignores this characteristic;
// only the DotOS companion app uses it.

// Register the handler run when the phone writes a find op (0x01 start / 0x00
// stop). Set once at boot; safe to set before start().
void set_find_handler(void (*fn)(uint8_t op));

// Notify the phone with a find op. Returns false if no phone is connected.
bool find_notify(uint8_t op);

}  // namespace ans
