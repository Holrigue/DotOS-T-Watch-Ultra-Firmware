// dot_tiles.cpp - see dot_tiles.h.
#include "dot_tiles.h"

#include <Preferences.h>

namespace {

const char *const NS = "argustiles";

// The two live slot choices. Index 0 = left, 1 = right.
DotTileKind s_slot[2] = { DOT_TILE_NONE, DOT_TILE_NONE };

// A stored byte from an older/newer firmware could be anything; coerce unknown
// values to NONE so the face never indexes past its table.
DotTileKind sane(uint8_t v)
{
    return (v < DOT_TILE__COUNT) ? (DotTileKind)v : DOT_TILE_NONE;
}

void persist()
{
    Preferences p;
    if (p.begin(NS, false)) {
        p.putUChar("slot0", (uint8_t)s_slot[0]);
        p.putUChar("slot1", (uint8_t)s_slot[1]);
        p.end();
    }
}

}  // namespace

void dot_tiles_boot_restore()
{
    Preferences p;
    if (p.begin(NS, true)) {
        s_slot[0] = sane(p.getUChar("slot0", DOT_TILE_NONE));
        s_slot[1] = sane(p.getUChar("slot1", DOT_TILE_NONE));
        p.end();
    }
}

DotTileKind dot_tiles_get(int slot)
{
    if (slot < 0 || slot > 1) return DOT_TILE_NONE;
    return s_slot[slot];
}

void dot_tiles_set(int slot, DotTileKind kind)
{
    if (slot < 0 || slot > 1) return;
    if (kind >= DOT_TILE__COUNT) kind = DOT_TILE_NONE;

    // Exclusivity: the same indicator must not show in both slots at once.
    if (kind != DOT_TILE_NONE) {
        int other = slot ^ 1;
        if (s_slot[other] == kind) s_slot[other] = DOT_TILE_NONE;
    }

    s_slot[slot] = kind;
    persist();
}
