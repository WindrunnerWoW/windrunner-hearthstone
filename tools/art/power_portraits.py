#!/usr/bin/env python3
"""Pre-cut round hero power portraits (second in-game test): one BLP2 texture per hero power card.

The match window draws a hero power inside the round `hero_power_ring` piece. The card textures hold the whole
card, so the addon used to crop a rectangle out of the card, which did not fit the ring (it sat low and spilled out
at the bottom). The 1.12 client has no texture masks, so the circle is cut here: for every card of type HERO_POWER
of the art set (the 12 powers incl. Jaraxxus's INFERNO!, EX1_tk33) the round art window of the render is cropped to a
square, scaled to 128x128 and written with a circular alpha channel with a soft edge (BLP2 DXT5, mip chain), the
circle inscribed in the texture with a one pixel transparent margin. The addon puts it into the ring's `circle`
anchor (the ring's opening grown by 2.5 px so the ring, drawn above it, hides the soft edge) and falls back to the
rectangular crop when a card has no portrait.

  tools/art/.venv/Scripts/python tools/art/power_portraits.py [--ids ID ...] [--jobs N] [--verify] [--no-blp]

Input : art/patched/<id>.png else art/cache/<id>.png (the 256x388 renders, as convert_blp.py)
Output: art/out/Powers/<id>.blp (gitignored: Blizzard art), art/review/powers/_contact.png (gitignored),
        data/art/powers.manifest.json (committed; deploy-addon.ps1 lists the ids in HS_PowerTextures)

Source of the picture, best first:
  1. the bare portrait art of HearthstoneJSON, https://art.hearthstonejson.com/v1/512x/<id>.jpg (512x512, no frame,
     no gem: nothing is baked in). Cached in art/cache/powers512/<id>.jpg; `--fetch` downloads missing ones. A circle
     of API_RADIUS (41% of the width) around the middle is used: it stays on real art for all twelve powers (a few
     have black padding at the very edge of the square).
  2. else the 256x388 card render: the art circle is centred at (128, 112); the crop has the radius 44. That keeps
     the cost gem (above y = 68) and the gold rim out of most of the portrait, but the inner edge of the rim and the
     notch of the gem socket are baked into it (the border the owner saw inside the ring, 2026-10-03).
Standard library + Pillow only. Deterministic.
"""
import argparse
import multiprocessing
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw  # noqa: E402

import artlib as A  # noqa: E402
import convert_blp as C  # noqa: E402
import minion_portraits as M  # noqa: E402

OUT = os.path.join(A.ART, "out", "Powers")
REVIEW = os.path.join(A.ART, "review", "powers")
MANIFEST = os.path.join(A.ART_DATA, "powers.manifest.json")
SIZE = M.SIZE
CENTER = (128.0, 112.0)
RADIUS = 44.0
BOX = (CENTER[0] - RADIUS, CENTER[1] - RADIUS, CENTER[0] + RADIUS, CENTER[1] + RADIUS)
API_DIR = os.path.join(A.ART, "cache", "powers512")
API_URL = "https://art.hearthstonejson.com/v1/512x/%s.jpg"
API_RADIUS = 0.41 * 512                # pixels of the 512 square; the circle is centred on it
ROUND_TYPES = ("HERO_POWER", "WEAPON")   # weapons get the same round portrait (owner, 2026-10-03: the weapon sits in a ring too)


def api_path(cid):
    return os.path.join(API_DIR, cid + ".jpg")


def fetch(ids):
    """Download the missing bare portraits (HearthstoneJSON). Returns the ids that could not be fetched."""
    import urllib.request
    os.makedirs(API_DIR, exist_ok=True)
    failed = []
    for cid in ids:
        if os.path.exists(api_path(cid)):
            continue
        try:
            req = urllib.request.Request(API_URL % cid, headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req, timeout=60) as r:
                data = r.read()
            with open(api_path(cid), "wb") as f:
                f.write(data)
        except Exception as e:  # noqa: BLE001 - report and fall back to the card render
            print("  no API art for %s (%s)" % (cid, e))
            failed.append(cid)
    return failed


def portrait(cid):
    if os.path.exists(api_path(cid)):
        im = Image.open(api_path(cid)).convert("RGBA")
        c = im.width / 2.0
        r = API_RADIUS * im.width / 512.0
        art = im.resize((SIZE, SIZE), Image.LANCZOS, box=(c - r, c - r, c + r, c + r))
        kind = "api512"
    else:
        src, kind = C.source_path(cid)
        im = Image.open(src).convert("RGBA")
        art = im.resize((SIZE, SIZE), Image.LANCZOS, box=BOX)
    art.putalpha(M.oval_mask())          # a circle: the crop is square
    return art, kind


def one(args):
    cid, write = args
    art, kind = portrait(cid)
    rec = {"id": cid, "source": kind}
    if write:
        blp, nlev = C.build_blp(art)
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, cid + ".blp"), "wb") as f:
            f.write(blp)
        rec.update({"format": "BLP2 DXT5 alpha8 mips=%d" % nlev, "blp_bytes": len(blp), "blp_sha256": A.sha256_bytes(blp)})
    return rec


def contact_sheet(ids):
    cell = 140
    sheet = Image.new("RGB", (6 * cell, ((len(ids) + 5) // 6) * (cell + 12)), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for i, cid in enumerate(ids):
        art, _ = portrait(cid)
        bg = Image.new("RGB", (SIZE, SIZE), (110, 110, 110))
        bg.paste(art, (0, 0), art.split()[3])
        x, y = (i % 6) * cell, (i // 6) * (cell + 12)
        sheet.paste(bg, (x + 6, y + 2))
        dr.text((x + 4, y + cell - 4), cid, fill=(230, 230, 230))
    os.makedirs(REVIEW, exist_ok=True)
    path = os.path.join(REVIEW, "_contact.png")
    sheet.save(path)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ids", nargs="*")
    ap.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--no-blp", action="store_true")
    ap.add_argument("--fetch", action="store_true", help="download the missing bare portraits first (HearthstoneJSON)")
    args = ap.parse_args()
    powers = [c["id"] for c in A.wanted_cards(A.load_dataset()) if c["type"] in ROUND_TYPES]
    if args.ids:
        bad = [i for i in args.ids if i not in powers]
        if bad:
            sys.exit("not hero powers / weapons of the art set: " + " ".join(bad))
        powers = sorted(args.ids)
    if args.fetch:
        fetch(powers)
    missing = [c for c in powers if not os.path.exists(api_path(c)) and not os.path.exists(C.source_path(c)[0])]
    ids = [c for c in powers if c not in missing]
    t0 = time.time()
    work = [(c, not args.no_blp) for c in ids]
    if args.jobs > 1 and len(work) > 1:
        with multiprocessing.Pool(args.jobs) as pool:
            recs = pool.map(one, work, chunksize=1)
    else:
        recs = [one(w) for w in work]
    dt = time.time() - t0
    entries = A.load_json(MANIFEST, {"entries": {}}).get("entries", {})
    for r in recs:
        entries[r["id"]] = r
    if not args.ids:
        entries = {k: v for k, v in entries.items() if k in set(ids)}
    A.dump_json(MANIFEST, {
        "schema": 1,
        "note": "One 128x128 round portrait (BLP2 DXT5 with a circular alpha) per hero power card, made by "
                "tools/art/power_portraits.py into art/out/Powers/<id>.blp; deploy-addon.ps1 copies them and lists "
                "the ids in HS_PowerTextures (CardTextures.lua of the client copy). The texture fills the 'circle' "
                "anchor of hero_power_ring (board_textures.manifest.json).",
        "crop": {"center": list(CENTER), "radius": RADIUS, "feather": M.FEATHER, "size": SIZE,
                 "api_radius_of_512": API_RADIUS},
        "entries": entries,
    })
    if args.verify and not args.no_blp:
        worst = 0.0
        for r in recs:
            ref, _ = portrait(r["id"])
            dec = Image.open(os.path.join(OUT, r["id"] + ".blp")).convert("RGBA")
            rp, dp = ref.load(), dec.load()
            err = n = 0
            for y in range(SIZE):
                for x in range(SIZE):
                    if rp[x, y][3] > 0:
                        err += sum(abs(rp[x, y][k] - dp[x, y][k]) for k in range(3))
                        n += 3
            worst = max(worst, err / max(1, n))
        print("verify: worst mean|dRGB| over %d portraits = %.2f" % (len(recs), worst))
    sheet = contact_sheet(ids) if not args.ids else None
    print("%d power portraits in %.1fs, %d missing sources%s" % (len(recs), dt, len(missing), (": " + " ".join(missing)) if missing else ""))
    if sheet:
        print("contact sheet: " + sheet)


if __name__ == "__main__":
    multiprocessing.freeze_support()
    main()
