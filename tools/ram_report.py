#!/usr/bin/env python3
"""ram_report.py - where the static RAM goes, from the linker map of a firmware build.

Usage: ram_report.py <firmware.map> [--top N] [--budget BYTES]

Reads the .data / .bss / .noinit contributions in the map file (what PlatformIO's
"RAM: used N bytes" line adds up) and prints them grouped by owner, then the biggest
single objects and symbols, so "static RAM is 61%" becomes "these 20 things are why".

Groups: our code (src/), our libs (lib/...), the Arduino core, and the prebuilt
ESP-IDF archives (libwifi.a, libbt.a, liblwip.a, ...). Static RAM here means INTERNAL
DRAM: sections placed in PSRAM (.ext_ram.bss) are listed separately and not counted.

Exit status is 0 unless --budget is given and the internal total exceeds it.
Pure standard library; no ESP toolchain needed beyond the map file the build writes.
"""
import argparse
import re
import sys
from collections import defaultdict

# Output sections that live in internal DRAM and are counted as "RAM used".
DRAM_SECTIONS = (".dram0.data", ".dram0.bss", ".data", ".bss", ".noinit", ".tbss", ".tdata")
PSRAM_SECTIONS = (".ext_ram.bss", ".ext_ram_noinit", ".ext_ram.data")

HEX = r"0x[0-9a-fA-F]+"
# "  .bss.s_foo   0x3fc9a2c0   0x400 path/to/obj.o"  (the name may sit alone on the line before)
INPUT_RE = re.compile(r"^\s+(\.\S+)\s+(%s)\s+(%s)\s+(\S.*?)\s*$" % (HEX, HEX))
NAME_ONLY_RE = re.compile(r"^\s+(\.\S+)\s*$")
FILL_RE = re.compile(r"^\s+\*fill\*\s+(%s)\s+(%s)" % (HEX, HEX))
CONT_RE = re.compile(r"^\s+(%s)\s+(%s)\s+(\S.*?)\s*$" % (HEX, HEX))
OUTSEC_RE = re.compile(r"^(\.\S+)\s+(%s)\s+(%s)" % (HEX, HEX))
OUTSEC_NAME_ONLY = re.compile(r"^(\.\S+)\s*$")


def owner_of(path):
    """Map an object path to a (group, owner) pair."""
    p = path.replace("\\", "/")
    if p == "(alignment padding)":
        return ("Alignment padding", "(between symbols)")
    m = re.search(r"/(lib[\w+.-]+\.a)\(", p)
    if m:
        return ("ESP-IDF / prebuilt", m.group(1))
    if "/src/" in p and "/lib" not in p.split("/src/")[0].rsplit("/", 1)[-1]:
        m = re.search(r"(?:^|/)src/(.+)$", p)
        if m and "FrameworkArduino" not in p:
            return ("Our code (src/)", "src/" + m.group(1))
    m = re.search(r"/lib[0-9a-f]{3}/([^/]+)/", p)
    if m:
        return ("Libraries", m.group(1))
    m = re.search(r"/(?:libc[0-9a-f]{2}|lib[0-9a-f]{3})/([^/]+)/", p)
    if m:
        return ("Libraries", m.group(1))
    if "FrameworkArduino" in p:
        return ("Arduino core", "FrameworkArduino/" + p.rsplit("/", 1)[-1])
    if "/lib/" in p or "lib_" in p:
        return ("Libraries", p.rsplit("/", 1)[-1])
    return ("Other", p.rsplit("/", 1)[-1])


def parse(path):
    rows = []          # (section_kind, size, symbol_section, objpath)
    out_sec = None
    pending = None
    with open(path, errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            m = OUTSEC_RE.match(line)
            if m:
                out_sec, pending = m.group(1), None
                continue
            m = OUTSEC_NAME_ONLY.match(line)
            if m and not line.startswith(" "):
                out_sec, pending = m.group(1), None
                continue
            if out_sec is None:
                continue
            m = FILL_RE.match(line)
            if m:
                rows.append((out_sec, int(m.group(2), 16), "*fill*", "(alignment padding)"))
                pending = None
                continue
            m = INPUT_RE.match(line)
            if m:
                insec, _addr, size, obj = m.groups()
                rows.append((out_sec, int(size, 16), insec, obj))
                pending = None
                continue
            m = NAME_ONLY_RE.match(line)
            if m:
                pending = m.group(1)
                continue
            m = CONT_RE.match(line)
            if m and pending:
                _addr, size, obj = m.groups()
                rows.append((out_sec, int(size, 16), pending, obj))
                pending = None
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--budget", type=int, default=0)
    a = ap.parse_args()

    rows = parse(a.map)
    dram = [r for r in rows if r[0] in DRAM_SECTIONS and r[1] > 0]
    psram = [r for r in rows if r[0] in PSRAM_SECTIONS and r[1] > 0]
    total = sum(r[1] for r in dram)

    groups = defaultdict(int)
    owners = defaultdict(int)
    for _sec, size, _insec, obj in dram:
        g, o = owner_of(obj)
        groups[g] += size
        owners[(g, o)] += size

    print("== Static internal RAM (.data + .bss + .noinit): %d bytes (%.1f KB)" % (total, total / 1024))
    for g, s in sorted(groups.items(), key=lambda kv: -kv[1]):
        print("  %-22s %8d  %5.1f%%" % (g, s, 100.0 * s / total if total else 0))
    print("  (PSRAM static, not counted: %d bytes)" % sum(r[1] for r in psram))

    print("\n== Biggest owners (object file / archive member group)")
    for (g, o), s in sorted(owners.items(), key=lambda kv: -kv[1])[: a.top]:
        print("  %8d  %-18s %s" % (s, g.split(" ")[0], o))

    print("\n== Biggest single symbols (input sections), internal RAM")
    for _sec, size, insec, obj in sorted(dram, key=lambda r: -r[1])[: a.top]:
        print("  %8d  %-40s %s" % (size, insec[:40], owner_of(obj)[1]))

    if a.budget and total > a.budget:
        print("\nOVER BUDGET: %d > %d" % (total, a.budget))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
