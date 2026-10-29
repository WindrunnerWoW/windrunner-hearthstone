#!/usr/bin/env python3
"""Pre-cut oval minion portraits (first in-game test): one extra BLP2 texture per minion card.

The match window draws a minion inside the round/oval `minion_oval` ring of the board art. The card textures hold
the whole card, so the addon used to crop a rectangle out of the art window, and that rectangle stuck out of the
ring. The 1.12 client has no texture masks, so the oval is cut here instead: for every card of type MINION
(collectible minions, tokens, generated ones; the same `A.wanted_cards` set as the card textures) the art window of
the render is cropped to the aspect of the ring's oval opening, scaled to 128x128 and written with an elliptic
alpha channel with soft edges (BLP2, DXT5, mip chain), the ellipse inscribed in the texture with a one pixel
transparent margin. The addon puts it inside the ring's `oval` anchor (data/art/board_textures.manifest.json,
MatchUI MU.SetMinionArt) and falls back to the rectangular crop when a card has no portrait.

  tools/art/.venv/Scripts/python tools/art/minion_portraits.py             # every minion
  tools/art/.venv/Scripts/python tools/art/minion_portraits.py --ids CS2_172 EX1_572
  ... --jobs N (default: CPU count), --verify (decode back and print the error), --no-blp (manifest only)

Input : art/patched/<id>.png else art/cache/<id>.png (the 256x388 renders of fetch_renders.py / patch_renders.py;
        the same file convert_blp.py uses), data/art/board_textures.manifest.json (the ring's oval anchor)
Output: art/out/Minions/<id>.blp (gitignored: Blizzard art), art/review/minions/_contact.png (gitignored),
        data/art/minions.manifest.json (committed: ids, crop, hashes; deploy-addon.ps1 lists the ids in
        HS_MinionTextures of the generated CardTextures.lua)

The crop (render pixels, from the card template, checked on every render of the set in the contact sheet): the art
oval of a minion frame is centred at x = 128.5 and spans y = 44 .. 178 (the name banner covers its lower end);
the crop is that height, as wide as the ring's opening needs, so the portrait is not squeezed.
Standard library + Pillow only. Deterministic: same input, same bytes.
"""
import argparse
import json
import math
import multiprocessing
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw  # noqa: E402

import artlib as A  # noqa: E402
import convert_blp as C  # noqa: E402

OUT = os.path.join(A.ART, "out", "Minions")
REVIEW = os.path.join(A.ART, "review", "minions")
MANIFEST = os.path.join(A.ART_DATA, "minions.manifest.json")
BOARD_MANIFEST = os.path.join(A.ART_DATA, "board_textures.manifest.json")

SIZE = 128                  # texture size (square)
CENTER_X, TOP_Y, BOTTOM_Y = 128.5, 44.0, 178.0   # the art oval of the card template (render pixels)
FEATHER = 2.5               # width of the soft edge (texture pixels)
RADIUS = SIZE / 2.0 - 1.0   # the ellipse leaves a one pixel transparent margin


def target_aspect():
    """width / height of the ring's oval opening (the `oval` anchor of minion_oval in pixels of the piece)."""
    pieces = A.load_json(BOARD_MANIFEST)["pieces"]
    p = pieces["minion_oval"]
    o = p["anchors"]["oval"]
    return ((o["right"] - o["left"]) * p["w"]) / ((o["bottom"] - o["top"]) * p["h"])


def crop_box(aspect):
    h = BOTTOM_Y - TOP_Y
    w = h * aspect
    return (CENTER_X - w / 2.0, TOP_Y, CENTER_X + w / 2.0, BOTTOM_Y)


def oval_mask():
    """L mask: 255 inside the inscribed ellipse, a linear ramp of FEATHER pixels at its edge, 0 outside."""
    m = Image.new("L", (SIZE, SIZE), 0)
    px = m.load()
    c = (SIZE - 1) / 2.0
    for y in range(SIZE):
        for x in range(SIZE):
            r = math.sqrt((x - c) ** 2 + (y - c) ** 2)       # a circle in texture space (the crop has the aspect)
            a = (RADIUS - r) / FEATHER + 0.5
            px[x, y] = int(round(255 * max(0.0, min(1.0, a))))
    return m


MASK = None


def portrait(cid, box):
    global MASK
    if MASK is None:
        MASK = oval_mask()
    src, kind = C.source_path(cid)
    im = Image.open(src).convert("RGBA")
    art = im.resize((SIZE, SIZE), Image.LANCZOS, box=box)
    art.putalpha(MASK)
    return art, kind


def one(args):
    cid, box, write = args
    art, kind = portrait(cid, box)
    rec = {"id": cid, "source": kind}
    if write:
        blp, nlev = C.build_blp(art)
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, cid + ".blp"), "wb") as f:
            f.write(blp)
        rec.update({"format": "BLP2 DXT5 alpha8 mips=%d" % nlev, "blp_bytes": len(blp), "blp_sha256": A.sha256_bytes(blp)})
    return rec


def verify(cid, box):
    ref, _ = portrait(cid, box)
    dec = Image.open(os.path.join(OUT, cid + ".blp")).convert("RGBA")
    rp, dp = ref.load(), dec.load()
    err = n = aerr = 0
    for y in range(SIZE):
        for x in range(SIZE):
            r, d = rp[x, y], dp[x, y]
            aerr += abs(r[3] - d[3])
            if r[3] > 0:
                err += abs(r[0] - d[0]) + abs(r[1] - d[1]) + abs(r[2] - d[2])
                n += 3
    return err / max(1, n), aerr / float(SIZE * SIZE)


def contact_sheet(ids, box):
    """Every portrait over a checkerboard with the ring's oval drawn around it (to see the fit), 12 per row."""
    cell = 72
    cols = 14
    rows = (len(ids) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell, rows * (cell + 12)), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for i, cid in enumerate(ids):
        art, _ = portrait(cid, box)
        bg = Image.new("RGB", (SIZE, SIZE), (110, 110, 110))
        bg.paste(art, (0, 0), art.split()[3])
        bg = bg.resize((cell - 6, cell - 6), Image.LANCZOS)
        x, y = (i % cols) * cell, (i // cols) * (cell + 12)
        sheet.paste(bg, (x + 3, y + 2))
        dr.text((x + 2, y + cell - 3), cid[:11], fill=(230, 230, 230))
    os.makedirs(REVIEW, exist_ok=True)
    path = os.path.join(REVIEW, "_contact.png")
    sheet.save(path)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ids", nargs="*", help="only these card ids")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--no-blp", action="store_true", help="contact sheet and manifest only")
    args = ap.parse_args()

    dataset = A.load_dataset()
    minions = [c["id"] for c in A.wanted_cards(dataset) if c["type"] == "MINION"]
    if args.ids:
        unknown = [i for i in args.ids if i not in minions]
        if unknown:
            sys.exit("not minion cards of the art set: " + " ".join(unknown))
        minions = sorted(args.ids)
    aspect = target_aspect()
    box = crop_box(aspect)
    missing = [cid for cid in minions if not os.path.exists(C.source_path(cid)[0])]
    ids = [cid for cid in minions if cid not in missing]
    t0 = time.time()
    work = [(cid, box, not args.no_blp) for cid in ids]
    if args.jobs > 1 and len(work) > 1:
        with multiprocessing.Pool(args.jobs) as pool:
            recs = pool.map(one, work, chunksize=2)
    else:
        recs = [one(w) for w in work]
    dt = time.time() - t0
    prev = A.load_json(MANIFEST, {"entries": {}})
    entries = prev.get("entries", {})
    for r in recs:
        entries[r["id"]] = r
    if not args.ids:
        entries = {k: v for k, v in entries.items() if k in set(ids)}
    A.dump_json(MANIFEST, {
        "schema": 1,
        "note": "One 128x128 oval portrait (BLP2 DXT5 with an elliptic alpha) per minion card, made by "
                "tools/art/minion_portraits.py into art/out/Minions/<id>.blp; deploy-addon.ps1 copies them and lists "
                "the ids in HS_MinionTextures (CardTextures.lua of the client copy). The texture fills the ring's "
                "'oval' anchor (board_textures.manifest.json, minion_oval).",
        "crop": {"center_x": CENTER_X, "top": TOP_Y, "bottom": BOTTOM_Y, "aspect": round(aspect, 5),
                 "box": [round(v, 3) for v in box], "feather": FEATHER, "size": SIZE},
        "entries": entries,
    })
    if args.verify and not args.no_blp:
        worst = 0.0
        for r in recs:
            e, a = verify(r["id"], box)
            worst = max(worst, e)
            if args.ids:
                print("%-12s mean|dRGB|=%.2f mean|dA|=%.2f" % (r["id"], e, a))
        print("verify: worst mean|dRGB| over %d portraits = %.2f" % (len(recs), worst))
    sheet = contact_sheet(ids, box) if not args.ids else None
    print("%d portraits%s in %.1fs (%d jobs), %d missing sources%s" % (
        len(recs), "" if not args.no_blp else " (no blp)", dt, args.jobs, len(missing),
        (": " + " ".join(missing)) if missing else ""))
    if sheet:
        print("contact sheet: " + sheet)


if __name__ == "__main__":
    multiprocessing.freeze_support()
    main()
