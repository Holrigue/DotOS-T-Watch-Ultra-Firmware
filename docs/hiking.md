# Hiking: offline map, GPX follow & track recording

DotOS turns the watch into a simple offline hiking companion: an offline
map centred on your GPS fix, a GPX track drawn on top so you can follow a
planned route, a live "distance remaining" readout, and one-tap recording of
the track you actually walk.

This is **v1 — visual follow**: the map shows where you are, where the route
goes, and how far is left. There is no turn-by-turn / off-route alerting yet
(planned as "Nav Complète" v2).

Everything is offline — no phone, no data connection needed on the trail.

---

## 1. Prepare the SD card (once, at home)

The watch reads two folders from the root of the microSD card:

```
/map/<z>/<x>/<y>.png     ← offline map tiles (256×256 PNG, slippy layout)
/gpx/*.gpx               ← the routes you want to follow (+ recordings land here)
```

### Map tiles

Use the bundled helper `tools/gen_map_tiles.py` (Python 3, standard library
only) on your computer to download the area you'll hike:

```bash
# Estimate first (no download):
python3 tools/gen_map_tiles.py --center 46.90 -71.30 --radius-km 5 --zoom 12-16 --dry-run

# Then download into ./map :
python3 tools/gen_map_tiles.py --center 46.90 -71.30 --radius-km 5 --zoom 12-16 --out ./map
```

Options:

| Flag | Meaning |
|------|---------|
| `--center LAT LON` | Area centre; pair with `--radius-km` (default 3 km). |
| `--bbox MINLAT MINLON MAXLAT MAXLON` | Exact bounding box instead of a centre. |
| `--zoom 12-16` | Zoom range to fetch. 12–13 = overview, 15–16 = trail detail. |
| `--out ./map` | Output folder (default `./map`). |
| `--dry-run` | Print tile count + size estimate and stop. |
| `--server URL` | Alternate `{z}/{x}/{y}.png` source. |
| `--delay 1.0` | Seconds between requests. |

Then copy the generated `map` folder to the **root** of the SD card so the
paths become `/map/12/...`, `/map/16/...`, etc.

**Be reasonable with the public OSM server.** The tool refuses more than
5000 tiles from the default `tile.openstreetmap.org` (that's their usage
policy — a modest personal area is fine). For a big region, download from a
bulk-friendly source via `--server`. A 5 km radius over zoom 12–16 is a few
hundred tiles / a few MB.

### GPX routes

Drop the `.gpx` files of the routes you want to follow into `/gpx` on the SD
card. Plain GPX 1.1 with `<trkpt>` / `<rtept>` / `<wpt>` points works;
`minlat/maxlat` bounds headers are ignored. Long tracks are automatically
thinned to fit memory, so multi-thousand-point files are fine.

---

## 2. On the watch

### Open the map

- **Home → swipe up** opens the **Apps** sub-menu.
- Tap **Map (GPS/GPX)** for the map, or **GPX Track** to pick a route.

The map is also part of the Meshtastic swipe chain (between NODES and SEND
MESSAGE) when `/map` tiles are present.

### Following a route

- On first open, the map **auto-loads the first `/gpx/*.gpx`** it finds and
  draws it as an **orange line** over the tiles.
- Your live position is the GPS marker; the route tracks pan / zoom / recentre.
- A badge under the info badge shows **distance remaining** — "N km left"
  from your nearest point on the route once GPS is locked (total length
  before lock).
- Controls: **+ / −** zoom (steps through the zoom levels actually on the
  card), and the **GPS** button recentres on your position.

### Switching routes — GPX Track picker

Open **GPX Track** (Apps sub-menu) for a full list of the `.gpx` files in
`/gpx`. Tap one to load it and jump to the map. There's also a **Clear
track** row to remove the overlay.

### Recording your hike — REC

- On the map, tap **REC** (top-left). It turns **red** and shows a live
  point count while recording.
- The track is written to **`/gpx/rec_<YYYYMMDD-HHMMSS>.gpx`** (named from
  the watch clock, UTC).
- Recording runs in the **background** on its own 1 Hz timer — it keeps
  logging even if you leave the map screen or the display dims.
- Points are throttled (logged once you've moved ~5 m, or every 15 s at
  rest) so files stay small.
- Tap **REC** again to stop and close the file cleanly. Recorded files show
  up in the GPX Track picker and can be copied off the card afterwards.

> Tip: start REC at the trailhead, stop it at the car. The resulting
> `rec_*.gpx` can be re-loaded later to retrace the same hike.

---

## Limits (v1)

- Visual follow only — no off-route buzz / turn-by-turn yet.
- "Distance remaining" is the route suffix from your nearest point, not a
  routed distance.
- GPS accuracy and time-to-first-fix depend on sky view, as usual.
