#!/usr/bin/env python3
"""
gen_map_tiles.py — download offline map tiles for the DotOS hiking map.

The watch reads 256x256 PNG tiles from the SD card in the standard slippy
layout that map_screen.cpp expects:

    /map/<z>/<x>/<y>.png      (on the SD card root)

This tool downloads the tiles covering a bounding box, for a range of zoom
levels, into that exact layout so you can copy the resulting `map/` folder to
the SD card. Standard library only (no pip installs).

Typical hiking prep — grab z12..z16 around a trail's bounding box:

    python3 tools/gen_map_tiles.py \\
        --bbox 46.78 -71.30 46.92 -71.10 \\
        --zoom 12-16 \\
        --out ./sdcard/map

Then copy ./sdcard/map to the SD card so it lands at /map on the card.

  --bbox MIN_LAT MIN_LON MAX_LAT MAX_LON   area to cover (decimal degrees)
  --center LAT LON --radius-km R           alternative to --bbox
  --zoom A-B (or a single Z)               zoom levels, inclusive
  --out DIR                                output root (default ./map)
  --server URL                             tile URL template with {z}/{x}/{y}
                                           (default OpenStreetMap standard)
  --delay S                                seconds between requests (default 1.0)
  --dry-run                                only count tiles + estimate size

IMPORTANT — tile usage policy. The default server is the public OpenStreetMap
tile service, whose policy forbids bulk/automated downloading. This tool is fine
for a *modest personal* area (a hike), run slowly (the 1 s default delay), with a
descriptive User-Agent. For anything larger, use a source that permits bulk
downloads (a paid/keyed provider, or your own tile server / a extract rendered
with e.g. tilemaker), by passing --server. Do a --dry-run first to see the tile
count; if it is in the thousands, narrow the box or the zoom range.
"""
import argparse
import math
import os
import sys
import time
import urllib.request

UA = "DotOS-map-tiler/1.0 (personal offline hiking maps; https://github.com/Holrigue/ARGUS-DotOS)"
DEFAULT_SERVER = "https://tile.openstreetmap.org/{z}/{x}/{y}.png"


def deg2tile(lat, lon, z):
    """(lat,lon,zoom) -> (xtile, ytile), standard slippy math."""
    n = 2.0 ** z
    x = int((lon + 180.0) / 360.0 * n)
    lat_rad = math.radians(lat)
    y = int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n)
    x = max(0, min(int(n) - 1, x))
    y = max(0, min(int(n) - 1, y))
    return x, y


def bbox_from_center(lat, lon, radius_km):
    dlat = radius_km / 111.0
    dlon = radius_km / (111.320 * max(0.01, math.cos(math.radians(lat))))
    return (lat - dlat, lon - dlon, lat + dlat, lon + dlon)


def parse_zoom(s):
    if "-" in s:
        a, b = s.split("-", 1)
        return int(a), int(b)
    z = int(s)
    return z, z


def tiles_for(bbox, z):
    min_lat, min_lon, max_lat, max_lon = bbox
    x0, y0 = deg2tile(max_lat, min_lon, z)   # NW corner -> min x, min y
    x1, y1 = deg2tile(min_lat, max_lon, z)   # SE corner -> max x, max y
    if x1 < x0:
        x0, x1 = x1, x0
    if y1 < y0:
        y0, y1 = y1, y0
    for x in range(x0, x1 + 1):
        for y in range(y0, y1 + 1):
            yield x, y


def main():
    ap = argparse.ArgumentParser(description="Download offline map tiles for DotOS.")
    ap.add_argument("--bbox", nargs=4, type=float, metavar=("MINLAT", "MINLON", "MAXLAT", "MAXLON"))
    ap.add_argument("--center", nargs=2, type=float, metavar=("LAT", "LON"))
    ap.add_argument("--radius-km", type=float, default=3.0)
    ap.add_argument("--zoom", required=True)
    ap.add_argument("--out", default="./map")
    ap.add_argument("--server", default=DEFAULT_SERVER)
    ap.add_argument("--delay", type=float, default=1.0)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if args.bbox:
        bbox = tuple(args.bbox)
    elif args.center:
        bbox = bbox_from_center(args.center[0], args.center[1], args.radius_km)
    else:
        ap.error("give --bbox MIN_LAT MIN_LON MAX_LAT MAX_LON, or --center LAT LON")
    z0, z1 = parse_zoom(args.zoom)

    plan = [(z, x, y) for z in range(z0, z1 + 1) for (x, y) in tiles_for(bbox, z)]
    print(f"bbox={bbox}  zoom={z0}..{z1}  tiles={len(plan)}")
    print(f"~{len(plan) * 20 / 1024:.1f} MB estimated (at ~20 KB/tile)")
    if args.dry_run:
        return 0
    if len(plan) > 5000:
        print(f"REFUSING: {len(plan)} tiles is too many for the public OSM policy.\n"
              f"Narrow --bbox/--zoom, or pass a bulk-friendly --server.", file=sys.stderr)
        return 2

    done = skipped = failed = 0
    for i, (z, x, y) in enumerate(plan):
        d = os.path.join(args.out, str(z), str(x))
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, f"{y}.png")
        if os.path.exists(path) and os.path.getsize(path) > 0:
            skipped += 1
            continue
        url = args.server.format(z=z, x=x, y=y)
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=30) as r:
                data = r.read()
            with open(path, "wb") as fo:
                fo.write(data)
            done += 1
        except Exception as e:  # noqa: BLE001 — report and continue
            failed += 1
            print(f"  FAIL {url}: {e}", file=sys.stderr)
        if (i + 1) % 25 == 0 or i + 1 == len(plan):
            print(f"  {i+1}/{len(plan)}  (new {done}, skip {skipped}, fail {failed})")
        time.sleep(args.delay)

    print(f"done: {done} downloaded, {skipped} already present, {failed} failed")
    print(f"copy '{args.out}' to the SD card so tiles land at /map/<z>/<x>/<y>.png")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
