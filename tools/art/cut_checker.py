#!/usr/bin/env python3
"""Cut a picture out of a baked-in transparency checkerboard (the tavern tier shields).

Some of the owner's artwork arrives as a JPEG whose "transparent" background is a light grey / white checkerboard
painted into the pixels. This removes it: the background is every light, colourless pixel (min(r,g,b) >= --light,
max - min <= --grey) that is 4-connected to the image border, so light grey parts inside the picture (the stone's
highlights, enclosed by its dark outline) stay. The alpha edge is eroded by --shrink pixels (the JPEG's grey fringe)
and softened, the colour is kept. Output is an RGBA PNG for slice_board.py ("alpha": true sheets).

  tools/art/.venv/Scripts/python tools/art/cut_checker.py "build/assets/tier/Tier 1.jpg" build/assets/bg_tier_1.png
  ... --all     every build/assets/tier/Tier <n>.jpg to build/assets/bg_tier_<n>.png

Standard library + Pillow only. Deterministic.
"""
import argparse
import glob
import os
import re
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TIER_DIR = os.path.join(ROOT, "build", "assets", "tier")
ASSETS = os.path.join(ROOT, "build", "assets")


def cut(src, dst, light=180, grey=22, shrink=2, soften=1.0):
    rgb = Image.open(src).convert("RGB")
    w, h = rgb.size
    r, g, b = rgb.split()
    lo = ImageChops.darker(ImageChops.darker(r, g), b)
    hi = ImageChops.lighter(ImageChops.lighter(r, g), b)
    spread = ImageChops.subtract(hi, lo)
    # candidate background: 255 where light and colourless, else 0
    cand = ImageChops.multiply(lo.point(lambda v: 255 if v >= light else 0),
                               spread.point(lambda v: 255 if v <= grey else 0))
    # keep only the part connected to the border: flood the border seeds with 128
    for x in range(0, w, 4):
        for y in (0, h - 1):
            if cand.getpixel((x, y)) == 255:
                ImageDraw.floodfill(cand, (x, y), 128)
    for y in range(0, h, 4):
        for x in (0, w - 1):
            if cand.getpixel((x, y)) == 255:
                ImageDraw.floodfill(cand, (x, y), 128)
    alpha = cand.point(lambda v: 0 if v == 128 else 255)
    if shrink > 0:
        alpha = alpha.filter(ImageFilter.MinFilter(2 * shrink + 1))
    # only the biggest piece stays (stray checker specks are gone with the flood; this drops lone islands)
    if soften > 0:
        alpha = alpha.filter(ImageFilter.GaussianBlur(soften))
    out = rgb.convert("RGBA")
    out.putalpha(alpha)
    bbox = alpha.point(lambda v: 255 if v > 8 else 0).getbbox()
    if bbox:
        out = out.crop(bbox)
    out.save(dst)
    return out.size


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("src", nargs="?")
    ap.add_argument("dst", nargs="?")
    ap.add_argument("--all", action="store_true")
    a = ap.parse_args(argv)
    jobs = []
    if a.all:
        for f in sorted(glob.glob(os.path.join(TIER_DIR, "Tier *.jpg"))):
            n = re.search(r"Tier (\d+)\.jpg$", f).group(1)
            jobs.append((f, os.path.join(ASSETS, "bg_tier_%s.png" % n)))
    elif a.src and a.dst:
        jobs.append((a.src, a.dst))
    else:
        ap.error("give src and dst, or --all")
    for src, dst in jobs:
        size = cut(src, dst)
        print("%s -> %s %dx%d" % (os.path.relpath(src, ROOT), os.path.relpath(dst, ROOT), size[0], size[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
