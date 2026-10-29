#!/usr/bin/env python3
"""Reads the natural size of the preset models from the WoW 1.12 client's MPQ archives (needs `pip install mpyq`).

The match effects (addon/Hearthstone/MatchFx.lua) scale every spell model so that its on-screen size fits its anchor
(an impact about 1.25x the target frame, a missile about half a minion, an area burst about the area it covers). For
that they need to know how big a model is at scale 1. The .m2 header of a 1.12 model holds the bounding sphere of
its vertices, in yards (header offset 0xB4: vertex box min xyz, max xyz, radius). It is exact for mesh models and
only a lower bound for models that are mostly particles (many fire / shadow effects have no mesh at all: radius 0, they
get the default of their role). Per-preset `scale` in the manifest stays the way to correct a model that looks wrong.

    python tools/fx/model_extents.py                 print the radius of every preset model
    python tools/fx/model_extents.py --update        also write "ext" (radius in yards) into data/fx/effect_profiles.json
                                                     (then run tools/fx/gen_effect_profiles.py)
    python tools/fx/model_extents.py "Spells\\\\Frostbolt.mdx"   a single model
"""
import argparse
import glob
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MANIFEST = os.path.join(ROOT, "data", "fx", "effect_profiles.json")
DEFAULT_DATA = "C:/TurtleDev/04_client/game/Data"
OFS_VERTEX_BOX = 0xB4          # 6 floats (min xyz, max xyz) then the radius, in the MD20 header of version 256


def archive_order(path):
    """Later patches win: base archives first, patch.mpq, patch-2 ... patch-9, patch-W (lowest priority first)."""
    n = os.path.basename(path).lower()
    m = re.match(r"patch-(\w)\.mpq$", n)
    if n == "patch.mpq":
        return (1, "")
    if m:
        return (2, m.group(1))
    return (0, n)


def open_archives(data_dir):
    try:
        import mpyq
    except ImportError:
        raise SystemExit("this tool needs the mpyq module: pip install mpyq")
    out = []
    for a in sorted(glob.glob(os.path.join(data_dir, "*.[mM][pP][qQ]")), key=archive_order):
        try:
            out.append(mpyq.MPQArchive(a, listfile=False))
        except Exception as e:  # a damaged archive does not stop the run
            print("skip %s: %s" % (a, e), file=sys.stderr)
    out.reverse()
    if not out:
        raise SystemExit("no MPQ archives found in %s (use --data)" % data_dir)
    return out


def read_model(archives, path):
    name = path.replace("/", "\\")
    if name.lower().endswith(".mdx"):
        name = name[:-4] + ".m2"
    for a in archives:
        try:
            d = a.read_file(name)
        except Exception:
            d = None
        if d:
            return d
    return None


def radius(data):
    """The bounding radius in yards of an MD20 model (0.0 when the header holds none), or None when it is no model."""
    if len(data) < OFS_VERTEX_BOX + 28 or data[:4] != b"MD20":
        return None
    f = struct.unpack_from("<7f", data, OFS_VERTEX_BOX)
    r = abs(f[6])
    if r < 0.01:
        r = max(abs(v) for v in f[:6])
    if r != r or r > 1000:
        return None
    return round(r, 2) if r >= 0.01 else 0.0


def update_manifest(found):
    with open(MANIFEST, "r", encoding="utf-8") as fh:
        text = fh.read()
    out = []
    for line in text.split("\n"):
        m = re.search(r'"key": "([a-z0-9_]+)"', line)
        if m and m.group(1) in found and '"model"' in line:
            r = found[m.group(1)]
            line = re.sub(r'"ext": [0-9.]+, ', "", line)
            if r:
                line = re.sub(r'("dur": [0-9.]+, )', lambda mm: mm.group(1) + '"ext": %s, ' % repr(r), line, count=1)
        out.append(line)
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(out))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--data", default=DEFAULT_DATA)
    ap.add_argument("--update", action="store_true", help="write the radii into the manifest")
    args = ap.parse_args(argv)
    archives = open_archives(args.data)
    if args.paths:
        for p in args.paths:
            d = read_model(archives, p)
            print("%-48s %s" % (p, "not found" if d is None else radius(d)))
        return 0
    with open(MANIFEST, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
    found = {}
    for p in doc["presets"]:
        d = read_model(archives, p["model"])
        r = None if d is None else radius(d)
        found[p["key"]] = r
        print("%-18s radius %-6s %s" % (p["key"], "?" if r is None else r, p["model"]))
    if args.update:
        update_manifest(found)
        print("manifest updated; run tools/fx/gen_effect_profiles.py")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
