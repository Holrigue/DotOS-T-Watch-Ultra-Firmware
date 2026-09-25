#pragma once
//
// dot_tiles.h - the two user-selectable data slots on the Dot watchface.
//
// When the watch is unplugged the idle USB dot line at y=130 carries no
// information, so it is replaced by two tiles the wearer picks once (tap a slot
// -> choose an indicator). The choices persist in NVS so they survive a reboot.
// When USB is present the charge/data wave takes the row back.
//
// This is the pure MODEL + persistence only (no LVGL): which indicator each of
// the two slots holds, loaded/saved from NVS. The Dot face renders it and drives
// the picker. Kinds are a small stable enum so the stored bytes keep meaning
// across firmware updates - only ever append new kinds before DOT_TILE__COUNT.
#include <cstdint>

enum DotTileKind : uint8_t {
    DOT_TILE_NONE      = 0,   // empty slot (shows a "tap to choose" prompt)
    DOT_TILE_SLEEP     = 1,   // sleep score
    DOT_TILE_STEP_GOAL = 2,   // daily step goal progress (%)
    DOT_TILE_STEPS     = 3,   // daily steps
    DOT_TILE_BPM       = 4,   // heart rate, highest/lowest over a recent window
    DOT_TILE_MESH      = 5,   // button: open the Meshtastic (LoRa) chat
    DOT_TILE_FIND      = 6,   // button: ring the phone (Find) with one tap
    DOT_TILE__COUNT           // keep last
};

// Load the two saved slot choices from NVS. Call once at boot, after the
// hardware is up and Preferences is usable. Unset slots read as DOT_TILE_NONE.
void dot_tiles_boot_restore();

// Current indicator for a slot (0 = left, 1 = right). DOT_TILE_NONE when unset
// or the slot index is out of range.
DotTileKind dot_tiles_get(int slot);

// Set + persist a slot's indicator. Enforces exclusivity: a non-empty indicator
// can occupy only one slot at a time, so if the OTHER slot already holds the
// same kind it is cleared first. Out-of-range slots and kinds are ignored /
// coerced to NONE.
void dot_tiles_set(int slot, DotTileKind kind);
