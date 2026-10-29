#!/usr/bin/env python3
"""Apply data/art/fixes.json to the cached renders and write art/patched/<id>.png.

Needs Pillow (tools/art/.venv) and the hi-res donors (fetch_renders.py --hires). Deterministic:
the same cache and fixes.json give byte-identical PNGs. After a run the manifest entries of the
touched ids get patched_sha256 / patched_size, and art/review/fixes_before_after.png is rebuilt.

  tools/art/.venv/Scripts/python tools/art/patch_renders.py [--only ID ...] [--no-review]

Operations (all coordinates in native 256x388 unless noted):
  gem     copy the inside of a stat gem (cost / attack / health) from a donor render showing the value
  text    rebuild the rules text from word images (own words first, then donors), 512x painting
  title   rebuild the banner title from letters of other titles, along the banner curve, 512x painting
  art     fill the art window with the grey "no art" placeholder, or with real art-only image
          ("image": id of art/cache/artonly/<id>.jpg, fetched via fixes.json art_sources)
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw  # noqa: E402

import artlib as A  # noqa: E402
import artwin  # noqa: E402
import banner as B  # noqa: E402
import cardimg as C  # noqa: E402

# digit box of each stat gem plus a margin (native px); the gems are identical apart from the digit
GEM_BOXES = {
    "cost": (26, 47, 56, 87),
    "attack": (30, 299, 59, 339),
    "health": (198, 302, 227, 339),
}
TEXT_ROI = {  # hi-res search box of the rules text per card type
    "SPELL": (102, 492, 410, 626),
    "MINION": (100, 478, 412, 632),
    "WEAPON": (100, 478, 412, 632),
}
TEXT_EXCLUDE = {  # hi-res corners where the stat gems overlap the text box
    "SPELL": [],
    "MINION": [(100, 574, 150, 632), (368, 566, 412, 632)],
    "WEAPON": [(100, 574, 150, 632), (368, 566, 412, 632)],
}


def source_text_map(manifest):
    cur = {c["id"]: c for c in A.load_json(A.CARDS_LATEST)}
    return {cid: cur[e["source_id"]].get("text", "") for cid, e in manifest.items()
            if e.get("source_id") in cur}


def run_fix(fix, ctx):
    cid = fix["id"]
    cards = ctx["cards"]
    base = fix.get("template", cid)
    typ = cards[base]["type"]
    lo = C.load_lo(base)
    hi_regions = []
    for op in fix["ops"]:
        kind = op["op"]
        if kind == "text":
            roi = tuple(op.get("roi", TEXT_ROI[typ]))
            lines = op.get("lines") or op["text"]
            him, box, _ = C.render_text(base, lines, op.get("donors", []), roi,
                                        TEXT_EXCLUDE[typ], ctx["texts"], op.get("max_width"),
                                        op.get("compose"), op.get("text_overrides"))
            hi_regions.append((him, box))
        elif kind == "title":
            him, box, used = B.render_title(base, op["name"], ctx["curves"], op["donors"], cards,
                                            op.get("glyphs"))
            hi_regions.append((him, box))
            ctx["used_glyphs"][cid] = used
    for him, box in hi_regions:
        C.paste_region_lo(lo, him, box)
    for op in fix["ops"]:
        if op["op"] == "art":
            c = cards[base]
            mask = artwin.window_mask(ctx["card_list"], c["class"], c["type"])
            if op.get("image"):
                art = Image.open(os.path.join(A.CACHE, "artonly", op["image"] + ".jpg"))
                artwin.paste_art(lo, mask, art, op["size"], op["offset"])
            else:
                artwin.placeholder(lo, mask)
    for op in fix["ops"]:
        if op["op"] == "gem":
            C.gem_swap(lo, C.load_lo(op["donor"]), GEM_BOXES[op["gem"]])
    return lo


def review_sheet(fixes, ctx, path):
    """Before/after contact sheet: original render (or template) left, result right, labelled."""
    cell_w, cell_h = A.SRC_W, A.SRC_H
    pad, label_h = 10, 40
    cols = 2
    rows = (len(fixes) + cols - 1) // cols
    W = cols * (2 * cell_w + 3 * pad) + pad
    H = rows * (cell_h + label_h + pad) + pad
    sheet = Image.new("RGB", (W, H), (32, 32, 36))
    d = ImageDraw.Draw(sheet)
    for i, fx in enumerate(fixes):
        r, c = divmod(i, cols)
        x0 = pad + c * (2 * cell_w + 3 * pad)
        y0 = pad + r * (cell_h + label_h + pad)
        cid = fx["id"]
        card = ctx["cards"][cid]
        tpl = fx.get("template")
        before_id = tpl or cid
        d.text((x0, y0), "%s %s  [%s]" % (cid, card["name"], fx["mode"]), fill=(240, 240, 240))
        d.text((x0, y0 + 14), fx["summary"], fill=(200, 200, 120))
        d.text((x0, y0 + 26), "BEFORE" + (" (template %s)" % tpl if tpl else "") + "               AFTER",
               fill=(150, 200, 255))
        for k, src in enumerate((os.path.join(A.CACHE, before_id + ".png"),
                                 os.path.join(A.PATCHED, cid + ".png"))):
            im = Image.open(src).convert("RGBA")
            bg = Image.new("RGBA", im.size, (32, 32, 36, 255))
            bg.alpha_composite(im)
            sheet.paste(bg.convert("RGB"), (x0 + k * (cell_w + pad), y0 + label_h))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sheet.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", nargs="*", help="only these ids")
    ap.add_argument("--no-review", action="store_true")
    args = ap.parse_args()

    ds = A.load_dataset()
    manifest_doc = A.load_json(A.MANIFEST)
    manifest = manifest_doc["entries"]
    fixes_doc = A.load_json(A.FIXES)
    cj = A.load_json(os.path.join(A.ART_DATA, "banner_curves.json"))
    ctx = {
        "cards": {c["id"]: c for c in ds["cards"]},
        "card_list": ds["cards"],
        "texts": source_text_map(manifest),
        "curves": {k: B.Curve.from_json(v) for k, v in cj.items()},
        "used_glyphs": {},
    }
    os.makedirs(A.PATCHED, exist_ok=True)
    done = []
    for fx in fixes_doc["fixes"]:
        if args.only and fx["id"] not in args.only:
            continue
        print("patching", fx["id"], ctx["cards"][fx["id"]]["name"])
        lo = run_fix(fx, ctx)
        out = os.path.join(A.PATCHED, fx["id"] + ".png")
        lo.save(out, optimize=False)
        e = manifest[fx["id"]]
        e["patched_sha256"] = A.sha256_file(out)
        e["patched_size"] = os.path.getsize(out)
        e["fix_reason"] = fx["reason"]
        if fx.get("template"):
            e["template_id"] = fx["template"]
        done.append(fx)
    A.dump_json(A.MANIFEST, manifest_doc)
    if not args.no_review and not args.only:
        review_sheet(fixes_doc["fixes"], ctx, os.path.join(A.REVIEW, "fixes_before_after.png"))
    for cid, used in ctx["used_glyphs"].items():
        print("glyph sources for", cid, json.dumps(used, sort_keys=True))
    print("patched", len(done))


if __name__ == "__main__":
    main()
