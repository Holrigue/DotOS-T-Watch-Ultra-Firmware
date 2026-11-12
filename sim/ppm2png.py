#!/usr/bin/env python3
"""Convert the simulator's PPM stills to PNG. Standard library only (zlib), so it
runs anywhere the sim builds, with no image package to install or to drift.

    python3 ppm2png.py build/gallery      # writes a .png beside every .ppm
"""
import glob
import struct
import sys
import zlib


def convert(path):
    data = open(path, "rb").read()
    magic, dims, maxval, pixels = data.split(b"\n", 3)
    if magic != b"P6" or maxval != b"255":
        raise SystemExit(f"{path}: not an 8-bit binary PPM")
    w, h = map(int, dims.split())
    rows = b"".join(b"\x00" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    with open(path[:-4] + ".png", "wb") as out:
        out.write(b"\x89PNG\r\n\x1a\n"
                  + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                  + chunk(b"IDAT", zlib.compress(rows, 6))
                  + chunk(b"IEND", b""))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    files = sorted(glob.glob(sys.argv[1] + "/*.ppm"))
    for f in files:
        convert(f)
    print(f"{len(files)} PNG(s) written to {sys.argv[1]}")
