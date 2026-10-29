#!/usr/bin/env python3
"""Alternative hero portraits (reward track): one BLP2 texture per skin of data/heroes/portraits.json.

The nine classic heroes are ordinary cards (art/out/Cards/HERO_01..09.blp). Every other portrait of the catalog is a
plain 256x388 render of its hero card, fetched from HearthstoneJSON like those nine, padded into the same 256x512
texture and written with the same BLP2 DXT5 encoder, so the addon crops the arch out of it exactly as it does for a
classic hero (the texcoord bottom is 388/512 = 0.7578125 for all of them).

  tools/art/.venv/Scripts/python tools/art/hero_skins.py [--fetch] [--ids ID ...] [--no-blp] [--verify]

Input : art/cache/heroes/<id>.png (--fetch downloads the missing ones, 4 requests/s at most)
Output: art/out/Heroes/<id>.blp (gitignored: Blizzard art), art/review/heroes/_contact.png (gitignored, one sheet per
        class to check that the face sits in the arch), data/art/hero_skins.manifest.json (committed hashes);
        deploy-addon.ps1 copies the BLPs to Textures\\Heroes\\ and lists the ids in HS_HeroTextures.
A skin whose render is missing or is not 256 wide is reported and skipped: the addon then shows the default hero.
Standard library + Pillow only. Deterministic.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw  # noqa: E402

import artlib as A  # noqa: E402
import convert_blp as C  # noqa: E402

CATALOG = os.path.join(A.ROOT, "data", "heroes", "portraits.json")
CACHE = os.path.join(A.CACHE, "heroes")
OUT = os.path.join(A.ART, "out", "Heroes")
REVIEW = os.path.join(A.ART, "review", "heroes")
MANIFEST = os.path.join(A.ART_DATA, "hero_skins.manifest.json")
URL = "https://art.hearthstonejson.com/v1/render/latest/enUS/256x/%s.png"
UA = "mod-hearthstone-art-pipeline/1.0 (private local cache)"
MIN_INTERVAL = 0.25


def skins():
    """[(class, id, name)] of the alternative portraits, in catalog order."""
    with open(CATALOG, encoding="utf-8-sig") as f:
        data = json.load(f)
    return [(cls, e["card_id"], e["name"]) for cls, entries in sorted(data["classes"].items())
            for e in entries if not e["default"]]


def cache_path(cid):
    return os.path.join(CACHE, cid + ".png")


def fetch(ids):
    """Download the missing renders. Returns the ids that could not be fetched."""
    os.makedirs(CACHE, exist_ok=True)
    failed = []
    for cid in ids:
        if os.path.exists(cache_path(cid)):
            continue
        time.sleep(MIN_INTERVAL)
        try:
            req = urllib.request.Request(URL % cid, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=60) as r:
                data = r.read()
            with open(cache_path(cid), "wb") as f:
                f.write(data)
        except (urllib.error.URLError, OSError) as e:
            print("  no render for %s (%s)" % (cid, e))
            failed.append(cid)
    return failed


def canvas_of(cid):
    im = Image.open(cache_path(cid)).convert("RGBA")
    if im.width != A.TEX_W or im.height > A.TEX_H:
        raise ValueError("%s is %dx%d, expected %d wide and at most %d high" % (cid, im.width, im.height, A.TEX_W, A.TEX_H))
    canvas = Image.new("RGBA", (A.TEX_W, A.TEX_H), (0, 0, 0, 0))
    canvas.paste(im, (0, 0))
    return im, canvas


def convert(cid, write):
    im, canvas = canvas_of(cid)
    rec = {"id": cid, "source": "render", "source_size": [im.width, im.height], "texture_size": [A.TEX_W, A.TEX_H],
           "texcoord": {"left": 0.0, "right": 1.0, "top": 0.0, "bottom": im.height / float(A.TEX_H)}}
    if write:
        blp, nlev = C.build_blp(canvas)
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, cid + ".blp"), "wb") as f:
            f.write(blp)
        rec.update({"format": "BLP2 DXT5 alpha8 mips=%d" % nlev, "blp_bytes": len(blp), "blp_sha256": A.sha256_bytes(blp)})
    return rec


def contact_sheets(rows):
    """One sheet per class: the card render with the arch window of the match hero drawn over it."""
    os.makedirs(REVIEW, exist_ok=True)
    cell_w, cell_h = 128, 194
    by_class = {}
    for cls, cid, name in rows:
        if os.path.exists(cache_path(cid)):
            by_class.setdefault(cls, []).append((cid, name))
    paths = []
    for cls, items in sorted(by_class.items()):
        sheet = Image.new("RGB", (5 * cell_w, ((len(items) + 4) // 5) * (cell_h + 14)), (40, 40, 40))
        dr = ImageDraw.Draw(sheet)
        for i, (cid, name) in enumerate(items):
            im = Image.open(cache_path(cid)).convert("RGBA").resize((cell_w, cell_h), Image.LANCZOS)
            bg = Image.new("RGB", im.size, (90, 90, 90))
            bg.paste(im, (0, 0), im.split()[3])
            x, y = (i % 5) * cell_w, (i // 5) * (cell_h + 14)
            sheet.paste(bg, (x, y))
            dr.text((x + 2, y + cell_h), name[:22], fill=(230, 230, 230))
        path = os.path.join(REVIEW, "_contact_%s.png" % cls.lower())
        sheet.save(path)
        paths.append(path)
    return paths


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ids", nargs="*")
    ap.add_argument("--fetch", action="store_true", help="download the missing renders first (HearthstoneJSON)")
    ap.add_argument("--no-blp", action="store_true")
    ap.add_argument("--verify", action="store_true", help="decode each BLP and report the mean error")
    args = ap.parse_args()
    rows = skins()
    known = {cid for _, cid, _ in rows}
    ids = [cid for _, cid, _ in rows]
    if args.ids:
        bad = [i for i in args.ids if i not in known]
        if bad:
            sys.exit("not alternative portraits of the catalog: " + " ".join(bad))
        ids = list(args.ids)
    if args.fetch:
        fetch(ids)
    missing = [c for c in ids if not os.path.exists(cache_path(c))]
    recs, bad_size = [], []
    for cid in ids:
        if cid in missing:
            continue
        try:
            recs.append(convert(cid, not args.no_blp))
        except ValueError as e:
            print("  skipped: %s" % e)
            bad_size.append(cid)
    entries = A.load_json(MANIFEST, {"entries": {}}).get("entries", {})
    for r in recs:
        entries[r["id"]] = r
    entries = {k: v for k, v in entries.items() if k in known}
    A.dump_json(MANIFEST, {
        "schema": 1,
        "note": "One 256x512 BLP2 (DXT5, mips) per alternative hero portrait of data/heroes/portraits.json, made by "
                "tools/art/hero_skins.py into art/out/Heroes/<id>.blp from the plain HearthstoneJSON render; "
                "deploy-addon.ps1 copies them to Textures\\Heroes and lists the ids in HS_HeroTextures.",
        "entries": entries,
    })
    if args.verify and not args.no_blp:
        worst = 0.0
        for r in recs:
            _, ref = canvas_of(r["id"])
            dec = Image.open(os.path.join(OUT, r["id"] + ".blp")).convert("RGBA")
            rp, dp = ref.load(), dec.load()
            err = n = 0
            for y in range(0, ref.height, 2):
                for x in range(0, ref.width, 2):
                    if rp[x, y][3] > 0:
                        err += sum(abs(rp[x, y][k] - dp[x, y][k]) for k in range(3))
                        n += 3
            worst = max(worst, err / max(1, n))
        print("verify: worst mean|dRGB| over %d portraits = %.2f" % (len(recs), worst))
    sheets = [] if args.ids else contact_sheets(rows)
    print("%d hero skins converted, %d without a render%s%s" % (
        len(recs), len(missing), (": " + " ".join(missing)) if missing else "",
        ("; wrong size: " + " ".join(bad_size)) if bad_size else ""))
    for s in sheets:
        print("contact sheet: " + s)


if __name__ == "__main__":
    main()
