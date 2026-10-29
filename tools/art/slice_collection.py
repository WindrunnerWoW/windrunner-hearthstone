#!/usr/bin/env python3
"""Slice the owner's collection art into textures (merged collection + deck view).

Reads data/art/collection_pieces.json, cuts every piece out of build/assets/all textures.png and
build/assets/book background.png (both 1536x1024), and writes

  art/out/Collection/<name>.blp              BLP2 DXT5 with mip chain (gitignored, like art/out/Board)
  art/review/collection/<name>.png           preview on a checkerboard + _contact.png (gitignored)
  data/art/collection_textures.manifest.json w/h/right/bottom per piece (committed; tools/deploy/deploy-addon.ps1
                                             reads it to write CollectionTextures.lua, so deploy needs no Python)

It reuses the helpers of slice_board.py (same piece format, same BLP writer, same manifest layout) and
adds what the collection sheet needs. slice_board.py itself is not changed.

THE 512 RULE. The 1.12 client does not load a texture with a side above 512 (it draws solid green, which is how the
1024 wide bar and the 1024 high deck panel failed in the first in-game test). This script never writes a texture
side above 512: big pieces are cut into tiles ("tiles", "split"), and the run stops with an error (and the check at
the end of every run, also with --check) when any entry of the manifest is larger. tests/lua_tests.lua checks the
committed manifest the same way.

Usage (module root):
  tools/art/.venv/Scripts/python tools/art/slice_collection.py            # every piece
  tools/art/.venv/Scripts/python tools/art/slice_collection.py --only tab_druid gem_cost
  ... --no-blp       previews and manifest only (fast), --verify decode the BLPs and print the error
  ... --dump NAME..  raw 4x crop with a 10 pixel grid in art/review/collection/<name>_raw.png
  ... --check        only check the committed manifest against the 512 rule
  ... --art-root DIR take the owner's sheets (DIR/build/assets), the card renders (DIR/art/cache) and write the
                     textures and previews (DIR/art/out/Collection, DIR/art/review/collection) there instead of in
                     this module root. Use it to run the script of a git worktree for the main checkout:
                     tools/art/.venv/Scripts/python tools/art/slice_collection.py --art-root <main checkout> --verify
                     (the manifest data/art/collection_textures.manifest.json is always written next to the script)

Piece keys are those of slice_board.py (name, sheet, rect, mode, tolerance, holes, slice9, size, tex, stretch,
feather, trim, ...) with these additions:
  mode "halo"     for pieces that have a dark outline on a bright, hazy sheet background: the background is every
                  pixel brighter than "tolerance" (default 45) that is connected to the crop edge; everything
                  inside the dark outline stays opaque (shrink / soften as in "flood")
  mode "chan"     glow frames on a coloured haze: alpha from one colour channel, alpha = (c - lo) / (hi - lo),
                  keys "channel" ("r", "g" or "b"), "lo", "hi"; the colour stays the sheet colour
  fill            [{"rect": [l, t, r, b], "band": [y0, y1]}, ...] before the alpha is made: the rows of the rect are
                  replaced by the rows of the band (crop-relative pixels; removes the baked empty slots of the
                  deck panel so it can carry the deck list, and the baked text of the buttons)
  prescale        factor: the cut is scaled by this factor right after the alpha is made (before slice9, so the
                  nine-slice margins are in scaled pixels)
  split           [tw, th]: the finished piece is cut into tiles of at most tw x th pixels, named
                  <name>_c<col>r<row>; each tile is one texture (power of two sides, at most 512). The addon puts
                  the tiles next to each other (CollectionUI.Tiles)
  tiles           {"sheet", "rect", "size": [w, h], "tile": [tw, th], "surround": {...}} in place of rect: the rect
                  is scaled to size and cut into tiles like "split"
  surround        (with tiles) {"tol": 8, "keep": [[l, t, r, b], ...], "shrink": 1, "soften": 0.6}: everything
                  outside the object becomes transparent: the near black (brightness <= tol) that is connected to the
                  border of the crop is background. The "keep" rectangles (crop pixels) are never background and
                  block the flood, so the black gap of the book's spine stays dark
  image           {"file", "trim", "size": [w, h], "tex": [tw, th]} in place of rect: a picture of build/assets with its own
                  alpha (the pack art), cut to its opaque part when "trim" is set and scaled to size. With "alpha": "luma"
                  (and "crop": [l, t, r, b], "gain", "feather") a glow on a black background: brightness becomes the alpha
  (top level) "hero_strips"  {"size": [w, h], "tex": [tw, th], "crop_w": n, "heroes": {CLASS: {"card": "HERO_08",
                  "center": [x, y]}}}: one piece hero_<class> per entry, a strip of the hero card's portrait (crop_w
                  pixels of the 256 x 388 render, centred on the face at "center", the aspect of the strip) for the
                  deck tiles (DecksUI.lua uses them, with the crop of the card texture as fallback)
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageChops, ImageDraw, ImageFilter  # noqa: E402

import artlib as A  # noqa: E402
import convert_blp as C  # noqa: E402
import slice_board as B  # noqa: E402

PIECES = os.path.join(A.ART_DATA, "collection_pieces.json")
MANIFEST = os.path.join(A.ART_DATA, "collection_textures.manifest.json")
OUT = os.path.join(A.ART, "out", "Collection")
REVIEW = os.path.join(A.REVIEW, "collection")
RENDERS = [A.PATCHED, A.CACHE]          # card renders: a patched render wins over the downloaded one
MAX_SIDE = 512                          # the client's maximum texture side (see THE 512 RULE)
B.MAX_TEX = MAX_SIDE

_orig_make_alpha = B.make_alpha


def set_art_root(root):
    """Sheets, card renders, textures and previews come from / go to <root> (default: this module root)."""
    global OUT, REVIEW, RENDERS
    root = os.path.abspath(root)
    art = os.path.join(root, "art")
    OUT = os.path.join(art, "out", "Collection")
    REVIEW = os.path.join(art, "review", "collection")
    RENDERS = [os.path.join(art, "patched"), os.path.join(art, "cache")]
    B.SHEET_DIR = os.path.join(root, "build", "assets")


def apply_fill(piece, rgb):
    ops = piece.get("fill")
    if not ops:
        return rgb
    rgb = rgb.copy()
    px = rgb.load()
    for op in ops:
        l, t, r, b = op["rect"]
        y0, y1 = op["band"]
        n = y1 - y0
        for y in range(t, b):
            sy = y0 + ((y - t) % n)
            for x in range(l, r):
                px[x, y] = px[x, sy]
    return rgb


def halo_alpha(rgb, piece):
    w, h = rgb.size
    mx = B.maxch(rgb)
    tol = piece.get("tolerance", 45)
    # bright haze connected to the crop edge is background: flood on the inverted brightness
    bg = B.flood_bg(ImageChops.invert(mx), 255 - tol)
    a = Image.frombytes("L", (w, h), bytes(0 if v else 255 for v in bg))
    if piece.get("keep", "largest") == "largest":
        merged = B.largest_component(a.filter(ImageFilter.MaxFilter(5)))
        a = ImageChops.multiply(a, merged)
    for _ in range(piece.get("shrink", 1)):
        a = a.filter(ImageFilter.MinFilter(3))
    soft = piece.get("soften", 0.8)
    if soft:
        a = a.filter(ImageFilter.GaussianBlur(soft))
    return a


def chan_alpha(rgb, piece):
    ch = {"r": 0, "g": 1, "b": 2}[piece["channel"]]
    lo, hi = piece["lo"], piece["hi"]
    band = rgb.split()[ch]
    return band.point(lambda v: 0 if v <= lo else (255 if v >= hi else int(255 * (v - lo) / float(hi - lo) + .5)))


def make_alpha(piece, rgb):
    mode = piece.get("mode", "flood")
    rgb = apply_fill(piece, rgb)
    if mode in ("halo", "chan"):
        base = dict(piece)
        base["mode"] = "none"
        out = _orig_make_alpha(base, rgb)           # holes / clip / feather of the piece
        a = halo_alpha(rgb, piece) if mode == "halo" else chan_alpha(rgb, piece)
        out.putalpha(ImageChops.multiply(out.split()[3], a))
    else:
        out = _orig_make_alpha(piece, rgb)
    pre = piece.get("prescale")
    if pre:
        out = B.resize_rgba(out, (max(1, int(round(out.width * pre))), max(1, int(round(out.height * pre)))))
    return out


B.make_alpha = make_alpha


def surround_alpha(rgb, spec):
    """RGBA of rgb where the near black surround (connected to the border, not in a keep rectangle) is transparent."""
    mx = B.maxch(rgb)
    keep = spec.get("keep") or []
    if keep:
        mx = mx.copy()
        dr = ImageDraw.Draw(mx)
        for l, t, r, b in keep:
            dr.rectangle([l, t, r - 1, b - 1], fill=255)
    bg = B.flood_bg(mx, spec.get("tol", 8))
    a = Image.frombytes("L", rgb.size, bytes(0 if v else 255 for v in bg))
    for _ in range(spec.get("shrink", 1)):
        a = a.filter(ImageFilter.MinFilter(3))
    soft = spec.get("soften", 0.6)
    if soft:
        a = a.filter(ImageFilter.GaussianBlur(soft))
    out = rgb.convert("RGBA")
    out.putalpha(a)
    return out


def tile_images(name, im, tile):
    """[(tile name, col, row, x, y, image)] of im cut into tiles of at most tile = [tw, th]."""
    tw, th = tile
    out = []
    for r in range((im.height + th - 1) // th):
        for c in range((im.width + tw - 1) // tw):
            x, y = c * tw, r * th
            out.append(("%s_c%dr%d" % (name, c, r), c, r, x, y,
                        im.crop((x, y, min(im.width, x + tw), min(im.height, y + th)))))
    return out


def hero_strip(card, center, spec):
    """The portrait strip of a hero card render: RGBA of spec["size"] (the aspect of the strip around the face)."""
    path = None
    for d in RENDERS:
        p = os.path.join(d, card + ".png")
        if os.path.exists(p):
            path = p
            break
    if path is None:
        sys.exit("hero render not found: %s.png in %s (run tools/art/fetch_renders.py)" % (card, " or ".join(RENDERS)))
    im = Image.open(path).convert("RGBA")
    w, h = spec["size"]
    cw = spec["crop_w"]
    ch = cw * h / float(w)
    cx, cy = center
    box = (cx - cw / 2.0, cy - ch / 2.0, cx + cw / 2.0, cy + ch / 2.0)
    strip = im.convert("RGBa").crop(tuple(int(round(v)) for v in box)).resize((w, h), Image.LANCZOS).convert("RGBA")
    return strip


def pieces_to_images(spec, sheets, wanted, args):
    """Yields (piece dict, image, meta) for every texture to write; meta has tile_of / tile / offset for tiles."""
    for p in spec["pieces"]:
        base = p["name"]
        if wanted is not None and base not in wanted:
            continue
        img = p.get("image")
        if img:
            # a picture with its own alpha (not one of the RGB sheets): trimmed to its opaque part, scaled to size
            path = os.path.join(B.SHEET_DIR, img["file"])
            if not os.path.exists(path):
                sys.exit("picture not found: %s (copy it to build/assets, or pass --art-root)" % path)
            if img.get("alpha") == "luma":
                # a glow on black (no alpha of its own): the brightness becomes the alpha and the colour is brought up to
                # full brightness, so blended over a dark page it looks like the picture on black
                rgb = Image.open(path).convert("RGB")
                if img.get("crop"):
                    rgb = rgb.crop(tuple(img["crop"]))
                rgb = rgb.resize(tuple(img["size"]), Image.LANCZOS)
                gain = img.get("gain", 1.2)
                feather = float(img.get("feather", 0))     # the glow fades out over this many pixels at the edge
                w, h = rgb.size
                out = []
                for i, (r, g, b) in enumerate(rgb.getdata()):
                    m = max(r, g, b)
                    if m <= 2:
                        out.append((0, 0, 0, 0))
                        continue
                    k = 255.0 / m
                    a = m * gain
                    if feather:
                        x, y = i % w, i // w
                        a *= min(1.0, min(x, y, w - 1 - x, h - 1 - y) / feather)
                    out.append((min(255, int(r * k)), min(255, int(g * k)), min(255, int(b * k)), min(255, int(a))))
                im = Image.new("RGBA", rgb.size)
                im.putdata(out)
                yield {"name": base, "base": base, "mode": "image", "sheet": img["file"], "rect": img.get("crop"),
                       "tex": img.get("tex")}, B.bleed_color(im), {}
                continue
            im = Image.open(path).convert("RGBA")
            if img.get("trim"):
                im = im.crop(im.split()[-1].point(lambda v: 255 if v > 8 else 0).getbbox())
            im = B.resize_rgba(im, tuple(img["size"]))
            yield {"name": base, "base": base, "mode": "image", "sheet": img["file"], "rect": None,
                   "tex": img.get("tex")}, B.bleed_color(im), {}
            continue
        t = p.get("tiles")
        if t:
            x, y, w, h = t["rect"]
            rgb = sheets[t["sheet"]].crop((x, y, x + w, y + h))
            if t.get("surround"):
                im = surround_alpha(rgb, t["surround"])
            else:
                im = rgb.convert("RGBA")
            im = B.resize_rgba(im, tuple(t["size"]))
            for name, c, r, tx, ty, tim in tile_images(base, im, t["tile"]):
                yield {"name": name, "base": base, "mode": "surround" if t.get("surround") else "none",
                       "sheet": t["sheet"], "rect": [x, y, w, h]}, B.bleed_color(tim), {"tile_of": base, "tile": [c, r], "offset": [tx, ty]}
            continue
        piece = dict(p)
        piece["base"] = base
        if p.get("split"):
            piece.pop("tex", None)          # every tile gets the power of two texture of its own size
        im, _ = B.process(piece, sheets, {})
        if p.get("split"):
            for name, c, r, tx, ty, tim in tile_images(base, im, p["split"]):
                tp = dict(piece)
                tp["name"] = name
                yield tp, tim, {"tile_of": base, "tile": [c, r], "offset": [tx, ty]}
        else:
            yield piece, im, {}
    hs = spec.get("hero_strips")
    if hs:
        for cls in sorted(hs["heroes"]):
            name = "hero_" + cls.lower()
            if wanted is not None and name not in wanted and "hero_strips" not in wanted:
                continue
            h = hs["heroes"][cls]
            im = B.bleed_color(hero_strip(h["card"], h["center"], hs))
            yield {"name": name, "base": name, "mode": "hero", "tex": hs["tex"], "card": h["card"],
                   "center": h["center"], "rect": None}, im, {}


def check_manifest(entries):
    """Fails when a texture of the manifest has a side above the client's maximum."""
    bad = []
    for name in sorted(entries):
        tw, th = entries[name]["texture_size"]
        if tw > MAX_SIDE or th > MAX_SIDE:
            bad.append("%s %dx%d" % (name, tw, th))
    if bad:
        sys.exit("textures above %d pixels (the client draws them solid green): %s" % (MAX_SIDE, ", ".join(bad)))


def load_sheets(spec):
    sheets = {}
    for key, fname in spec["sheets"].items():
        p = os.path.join(B.SHEET_DIR, fname)
        if not os.path.exists(p):
            sys.exit("sheet not found: %s (copy the owner's sheets to build/assets, or pass --art-root)" % p)
        sheets[key] = Image.open(p).convert("RGB")
    return sheets


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", nargs="*", help="process only these pieces (a piece that is cut into tiles: its base name; "
                    "hero_strips: all hero strips)")
    ap.add_argument("--no-blp", action="store_true", help="previews and manifest only")
    ap.add_argument("--verify", action="store_true", help="decode each BLP with Pillow and print the error")
    ap.add_argument("--dump", nargs="+", metavar="PIECE", help="only write the raw crops of these pieces (4x, grid)")
    ap.add_argument("--check", action="store_true", help="only check the committed manifest against the 512 rule")
    ap.add_argument("--art-root", metavar="DIR", help="module root that holds build/assets and art/ (default: this one)")
    args = ap.parse_args()
    if args.art_root:
        set_art_root(args.art_root)

    if args.check:
        with open(MANIFEST, encoding="utf-8") as f:
            entries = json.load(f)["pieces"]
        check_manifest(entries)
        print("%d textures, none above %d pixels" % (len(entries), MAX_SIDE))
        return

    with open(PIECES, encoding="utf-8") as f:
        spec = json.load(f)
    sheets = load_sheets(spec)

    if args.dump:
        os.makedirs(REVIEW, exist_ok=True)
        by_name = {p["name"]: p for p in spec["pieces"]}
        for n in args.dump:
            piece = by_name.get(n)
            if not piece or "rect" not in piece:
                sys.exit("unknown piece " + n)
            im = B.cut(piece, sheets).resize((piece["rect"][2] * 4, piece["rect"][3] * 4), Image.NEAREST)
            dr = ImageDraw.Draw(im)
            for gx in range(0, im.width, 40):
                dr.line([(gx, 0), (gx, im.height)], fill=(0, 255, 255) if gx % 200 == 0 else (0, 120, 120))
            for gy in range(0, im.height, 40):
                dr.line([(0, gy), (im.width, gy)], fill=(0, 255, 255) if gy % 200 == 0 else (0, 120, 120))
            out = os.path.join(REVIEW, n + "_raw.png")
            im.save(out)
            print("%s: crop %r -> %s" % (n, piece["rect"], out))
        return

    wanted = None
    if args.only:
        wanted = set(args.only)

    os.makedirs(OUT, exist_ok=True)
    os.makedirs(REVIEW, exist_ok=True)
    prev = {}
    if os.path.exists(MANIFEST):
        with open(MANIFEST, encoding="utf-8") as f:
            prev = json.load(f).get("pieces", {})
    entries = dict(prev)
    contact = []
    names = []
    for piece, im, meta in pieces_to_images(spec, sheets, wanted, args):
        name = piece["name"]
        tex = B.compose_texture(im, piece)
        w, h = im.size
        entry = {
            "w": w, "h": h,
            "texture_size": [tex.width, tex.height],
            "right": w / float(tex.width),
            "bottom": h / float(tex.height),
            "mode": piece.get("mode", "flood"),
            "source": {"sheet": spec["sheets"].get(piece.get("sheet", ""), meta.get("tile_of")), "rect": piece.get("rect")},
        }
        if piece.get("card"):
            entry["source"] = {"card": piece["card"], "center": piece["center"]}
        entry.update(meta)
        if not args.no_blp:
            blp, nlev = C.build_blp(tex)
            with open(os.path.join(OUT, name + ".blp"), "wb") as f:
                f.write(blp)
            entry["blp_bytes"] = len(blp)
            entry["blp_sha256"] = A.sha256_bytes(blp)
            entry["format"] = "BLP2 DXT5 alpha8 mips=%d" % nlev
        elif name in prev:
            for k in ("blp_bytes", "blp_sha256", "format"):
                if k in prev[name]:
                    entry[k] = prev[name][k]
        entries[name] = entry
        names.append(name)
        contact.append((name, B.preview(im, os.path.join(REVIEW, name + ".png"))))
        line = "%-22s %4dx%-4d tex %3dx%-4d right=%.5f bottom=%.5f  %s" % (
            name, w, h, tex.width, tex.height, entry["right"], entry["bottom"], entry["mode"])
        if args.verify and not args.no_blp:
            dec = Image.open(os.path.join(OUT, name + ".blp")).convert("RGBA")
            rp, dp = tex.load(), dec.load()
            err = n = 0
            for y in range(tex.height):
                for x in range(tex.width):
                    r, d = rp[x, y], dp[x, y]
                    if r[3] > 0:
                        err += abs(r[0] - d[0]) + abs(r[1] - d[1]) + abs(r[2] - d[2])
                        n += 3
            line += "  mean|dRGB|=%.2f" % (err / max(1, n))
        print(line)

    # a full run knows every piece: drop the entries (and textures) of pieces that no longer exist
    if wanted is None:
        keep = set(names)
        entries = {k: v for k, v in entries.items() if k in keep}
        if not args.no_blp:
            for fn in sorted(os.listdir(OUT)):
                if fn.endswith(".blp") and fn[:-4] not in keep:
                    os.remove(os.path.join(OUT, fn))
                    print("removed stale texture " + fn)
    check_manifest(entries)
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        json.dump({
            "schema": 1,
            "note": "w/h = used pixels of the texture, right = w / texture width, bottom = h / texture height "
                    "(SetTexCoord(0, right, 0, bottom)). No texture side is above 512 (the client's limit); big pieces "
                    "are tiles <name>_c<col>r<row>. Written by tools/art/slice_collection.py, read by "
                    "tools/deploy/deploy-addon.ps1.",
            "pieces": entries,
        }, f, indent=2, sort_keys=True)
        f.write("\n")
    if wanted is None and contact:
        B.contact_sheet(contact, os.path.join(REVIEW, "_contact.png"))
    print("%d pieces -> %s" % (len(contact), OUT))


if __name__ == "__main__":
    main()
