#!/usr/bin/env python3
"""Builds the small UI icons of the addon as BLP2 (DXT5, mips) for the 1.12 client.

  micro_icon  the Hearthstone card back on the micro menu button (owner decision). The source is the owner's
              image of the classic card back (Blizzard art, so neither the source nor the BLP is committed): the
              card is cropped to its opaque area, scaled to 128 pixels high and placed top-left on a transparent
              128x128 canvas. The addon shows it with texcoords (0, RIGHT, 0, 1); RIGHT is printed and must match
              MB.ICON_RIGHT in addon/Hearthstone/MicroButton.lua.

  craft_panel the crystal status panel of the craft window (Disenchant slot, Craft slot, the +/- dust bars and the dust
              plate; source art/source/ui/craft_panel.png, the owner's image). Scaled whole to 512 x 256 (its aspect
              is 2:1, so it fills the texture; the measured rectangles are fractions of the source image). The slot, bar and plate rectangles the addon
              lays buttons and numbers over are measured on the source and listed in CraftUI.lua (CU.PANEL).

  banner_blank the "x2" ownership banner of the book art without its text, so the addon can write any count on it
              ("x3", "x4", ...; the sheet only has "x1" and "x2"). Made from art/out/Collection/banner_x2.blp (run
              slice_collection.py first): the text rectangle is refilled row by row with a linear blend of the plate
              pixels left and right of it (the plate is a smooth gradient). Same 64 x 64 canvas and 60 x 45 plate as the
              banner pieces, so the texcoords are the same (0, 0.9375, 0, 0.703125).

  banner_digits the numerals for the ownership banner of three or more copies ("x3", "x12", "x345"): the owner's
              serif numerals (build/assets/banner_numerals.png, black on transparent, the typeface of the "2" on the
              book's "x2" banner) and the "x" cut from that banner in the sheet (build/assets/all textures.png) at its
              own size, all in the ink of the banner text, as one atlas BannerDigits.blp (512 x 64, a cell of 32 x 48
              per glyph with padding, the digits 34 px high like the "2" of the banner, the x 25 px high on the same
              baseline). The tight rectangles of the glyphs are printed as the Lua table UI.GLYPHS (CollectionUI.lua).
              review/banner_digits_mock.png shows "x3", "x12" and "x345" on the blank banner.

  slot_glow   (also writes SlotShade, the same shape filled with black at 55% opacity) the outline of a craft panel slot (a rectangle with an arched top, drawn by this script): a white 9 px line
              with a soft glow on a 512 x 256 canvas, inset 24 px (the addon tints it blue with SetVertexColor and
              stretches it over a slot, see CU.GLOW_* in CraftUI.lua). Texcoords (0, 1, 0, 1).

Output: art/out/UI/<name>.blp (gitignored); tools/deploy/deploy-addon.ps1 copies it to Textures\\UI\\.

  tools/art/.venv/Scripts/python tools/art/ui_icons.py [--src <image>]
      default source: art/source/ui/micro_icon.webp, else C:/Users/Jonas/Downloads/icon.webp
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageChops, ImageDraw, ImageFilter  # noqa: E402

import artlib as A  # noqa: E402
from convert_blp import build_blp  # noqa: E402

SIZE = 128
OUT = os.path.join(A.ART, "out", "UI")
SOURCES = [os.path.join(A.ART, "source", "ui", "micro_icon.webp"), "C:/Users/Jonas/Downloads/icon.webp"]


def build_micro_icon(src):
    im = Image.open(src).convert("RGBA")
    box = im.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
    if not box:
        raise SystemExit("%s: the image is fully transparent" % src)
    card = im.crop(box)
    w = max(1, round(card.width * SIZE / card.height))
    if w > SIZE:
        raise SystemExit("%s: the card is wider than high, expected a card back" % src)
    card = card.resize((w, SIZE), Image.LANCZOS)
    canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    canvas.paste(card, (0, 0))
    data, levels = build_blp(canvas)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "MicroIcon.blp")
    with open(path, "wb") as fh:
        fh.write(data)
    print("wrote %s (%dx%d, card %dx%d, %d mips): MB.ICON_RIGHT = %.6f" % (path, SIZE, SIZE, w, SIZE, levels, w / SIZE))


def crop_opaque(im):
    box = im.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
    if not box:
        raise SystemExit("the image is fully transparent")
    return im.crop(box)


def scale_premultiplied(im, size):
    """Resizes with premultiplied alpha, so the black of the transparent pixels does not darken the edges."""
    return im.convert("RGBa").resize(size, Image.LANCZOS).convert("RGBA")


def write_blp(name, canvas):
    data, levels = build_blp(canvas)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".blp")
    with open(path, "wb") as fh:
        fh.write(data)
    print("wrote %s (%dx%d, %d mips)" % (path, canvas.width, canvas.height, levels))


def source(name):
    path = os.path.join(A.ART, "source", "ui", name)
    if not os.path.exists(path):
        raise SystemExit("no source image %s" % path)
    return Image.open(path).convert("RGBA")


def build_dust_bottle():
    size = 256
    bottle = crop_opaque(source("dust_bottle.png"))
    w = max(1, round(bottle.width * size / bottle.height))
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(scale_premultiplied(bottle, (w, size)), (0, 0))
    write_blp("DustBottle", canvas)
    print("  BOTTLE_RIGHT = %.6f   (width / height of the bottle: %.4f)" % (w / size, w / size))


def build_craft_panel():
    im = source("craft_panel.png")
    if abs(im.width / im.height - 2) > 0.01:
        raise SystemExit("craft_panel.png is %dx%d, expected 2:1" % im.size)
    write_blp("CraftPanel", scale_premultiplied(im, (512, 256)))
    print("  CraftPanel: the whole image (%dx%d) fills 512 x 256, texcoords (0, 1, 0, 1)" % im.size)


BANNER_SRC = os.path.join(A.ART, "out", "Collection", "banner_x2.blp")
BANNER_TEXT = (16, 10, 43, 27)       # x1, y1, x2, y2 (inclusive): the "x2" with its outline on the 60 x 45 plate
BANNER_EDGE = 2                      # plate pixels left / right of the text rectangle that the blend starts from


def build_banner_blank():
    if not os.path.exists(BANNER_SRC):
        raise SystemExit("no %s: run tools/art/slice_collection.py first" % BANNER_SRC)
    im = Image.open(BANNER_SRC).convert("RGBA")
    px = im.load()
    x1, y1, x2, y2 = BANNER_TEXT
    xl, xr = x1 - BANNER_EDGE, x2 + BANNER_EDGE
    for y in range(y1, y2 + 1):
        left, right = px[xl, y], px[xr, y]
        for x in range(x1, x2 + 1):
            t = (x - xl) / (xr - xl)
            px[x, y] = tuple(round(left[i] + (right[i] - left[i]) * t) for i in range(4))
    write_blp("BannerBlank", im)
    print("  BannerBlank: right = 0.9375, bottom = 0.703125 (the plate is 60 x 45 on 64 x 64)")


NUMERALS = os.path.join(A.ROOT, "build", "assets", "banner_numerals.png")
BANNER_SHEET = os.path.join(A.ROOT, "build", "assets", "all textures.png")
BANNER_SHEET_RECT = (721, 316, 132, 98)   # the "x2" banner in the sheet (x, y, w, h), the same as banner_x2 in slice_collection
BANNER_X_BOX = (40, 33, 64, 57)           # the "x" of that banner (inclusive pixel box, measured)
BANNER_2_BOTTOM = 58                      # the bottom row of its "2": the baseline of the digits
DIGIT_H = 34                              # height of the digits on the banner ("2": rows 25 to 58)
INK = (66, 46, 25)                        # the ink of the banner text (median of its dark pixels)
ATLAS_W, ATLAS_H, CELL_W, CELL_H, PAD = 512, 64, 32, 48, 6
GLYPHS = "0123456789x"


def numeral_boxes(im):
    """The ten numerals of the owner's image as (x0, x1) column runs, left to right."""
    a = im.getchannel("A").point(lambda v: 255 if v > 40 else 0)
    cols = [a.crop((x, 0, x + 1, im.height)).getbbox() is not None for x in range(im.width)]
    runs, start = [], None
    for x, c in enumerate(cols + [False]):
        if c and start is None:
            start = x
        if not c and start is not None:
            runs.append((start, x - 1))
            start = None
    if len(runs) != 10:
        raise SystemExit("%s: expected 10 numerals, found %d" % (NUMERALS, len(runs)))
    return runs


def inked(alpha):
    """An RGBA image of the banner ink with the given alpha (L)."""
    out = Image.new("RGBA", alpha.size, INK + (0,))
    out.putalpha(alpha)
    return out


def build_banner_digits():
    nums = Image.open(NUMERALS).convert("RGBA")
    runs = numeral_boxes(nums)
    alpha = nums.getchannel("A")
    box = alpha.point(lambda v: 255 if v > 40 else 0).getbbox()
    top, bottom = box[1], box[3]                       # the common band of the numerals (round ones overshoot)
    scale = DIGIT_H / (bottom - top)
    glyphs = {}
    for i, (x0, x1) in enumerate(runs):
        g = alpha.crop((x0, top, x1 + 1, bottom))
        w = max(1, round(g.width * scale))
        glyphs[str(i)] = inked(g.resize((w, DIGIT_H), Image.LANCZOS))
    # the x: cut from the banner art at its own size; its alpha is how much darker a pixel is than the plate
    sheet = Image.open(BANNER_SHEET).convert("RGBA")
    ban = sheet.crop((BANNER_SHEET_RECT[0], BANNER_SHEET_RECT[1], BANNER_SHEET_RECT[0] + BANNER_SHEET_RECT[2],
                      BANNER_SHEET_RECT[1] + BANNER_SHEET_RECT[3]))
    bx0, by0, bx1, by1 = BANNER_X_BOX
    m = 2
    xi = ban.crop((bx0 - m, by0 - m, bx1 + 1 + m, by1 + 1 + m)).convert("L")
    lo, hi = 40, 150                                    # ink ... plate luminance
    xa = xi.point(lambda v: max(0, min(255, round((hi - v) * 255 / (hi - lo)))))
    glyphs["x"] = inked(xa)
    xbottom_off = BANNER_2_BOTTOM - by1                 # rows between the x and the baseline
    # atlas: one cell per glyph, the digit band 34 px high at y = PAD, the x on the same baseline
    atlas = Image.new("RGBA", (ATLAS_W, ATLAS_H), INK + (0,))
    table = {}
    for k, ch in enumerate(GLYPHS):
        g = glyphs[ch]
        cx = k * CELL_W
        x0 = cx + (CELL_W - g.width) // 2
        if ch == "x":
            # the crop has m extra rows around the glyph; the glyph's bottom row sits xbottom_off rows above the baseline
            y0 = PAD + DIGIT_H - xbottom_off - (g.height - m) - m
        else:
            y0 = PAD
        atlas.alpha_composite(g, (x0, y0))
        if ch == "x":
            table[ch] = (x0 + m, y0 + m, x0 + g.width - m, y0 + g.height - m)
        else:
            table[ch] = (x0, y0, x0 + g.width, y0 + g.height)
    write_blp("BannerDigits", atlas)
    os.makedirs(os.path.join(A.ART, "review"), exist_ok=True)
    atlas.save(os.path.join(A.ART, "review", "banner_digits.png"))
    print("  paste into CollectionUI.lua (pixel boxes x0, y0, x1, y1 of the %d x %d atlas; digit height %d, baseline = row %d):"
          % (ATLAS_W, ATLAS_H, DIGIT_H, PAD + DIGIT_H))
    print("UI.GLYPHS = {")
    for ch in GLYPHS:
        print('    ["%s"] = { %d, %d, %d, %d },' % ((ch,) + table[ch]))
    print("}")
    # mock: "x3", "x12", "x345" at the banner's source size on the blank banner of the review folder
    blank = os.path.join(A.ART, "review", "banner_blank.png")
    if os.path.exists(blank):
        plate = Image.open(blank).convert("RGBA").resize((BANNER_SHEET_RECT[2], BANNER_SHEET_RECT[3]), Image.LANCZOS)
        sheet_out = Image.new("RGBA", (plate.width * 3 + 40, plate.height + 20), (40, 36, 34, 255))
        for n, text in enumerate(("x3", "x12", "x345")):
            tile = plate.copy()
            widths = [table[c][2] - table[c][0] for c in text]
            total = sum(widths) + 2 * (len(text) - 1)
            fit = min(1.0, 56.0 / total)                # the plate inside is about 56 px wide at this size
            cx = (tile.width - total * fit) / 2
            for c in text:
                x0, y0, x1, y1 = table[c]
                piece = atlas.crop((x0, y0, x1, y1))
                pw, ph = max(1, round(piece.width * fit)), max(1, round(piece.height * fit))
                piece = piece.resize((pw, ph), Image.LANCZOS)
                base = 58 - (PAD + DIGIT_H - y1) * fit
                tile.alpha_composite(piece, (round(cx), round(base - ph)))
                cx += (x1 - x0 + 2) * fit
            sheet_out.alpha_composite(tile, (10 + n * (plate.width + 10), 10))
        sheet_out.save(os.path.join(A.ART, "review", "banner_digits_mock.png"))


GLOW_INSET = 24        # the slot shape's distance from the canvas edge (CU.GLOW_INSET in CraftUI.lua)
GLOW_LINE = 9
GLOW_DOME = 0.26       # height of the arch as a share of the shape's height


def build_slot_glow():
    ss = 4
    w, h = 512 * ss, 256 * ss
    x0, y0, x1, y1 = [v * ss for v in (GLOW_INSET, GLOW_INSET, 512 - GLOW_INSET, 256 - GLOW_INSET)]
    dome = round((y1 - y0) * GLOW_DOME)
    mask = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(mask)
    d.rounded_rectangle([x0, y0 + dome, x1, y1], radius=14 * ss, fill=255, corners=(False, False, True, True))
    d.ellipse([x0, y0, x1, y0 + 2 * dome], fill=255)
    inner = mask.filter(ImageFilter.MinFilter(GLOW_LINE * ss * 2 + 1))
    stroke = ImageChops.subtract(mask, inner).resize((512, 256), Image.LANCZOS)
    glow = stroke.filter(ImageFilter.GaussianBlur(7)).point(lambda v: min(255, v * 2))
    alpha = ImageChops.lighter(stroke, glow.point(lambda v: v * 3 // 4))
    canvas = Image.merge("RGBA", (Image.new("L", (512, 256), 255),) * 3 + (alpha,))
    write_blp("SlotGlow", canvas)
    canvas.save(os.path.join(A.ART, "review", "slot_glow.png"))
    # the same shape filled, to dim a slot that cannot be used (a plain rectangle would also darken the frame corners)
    fill = mask.resize((512, 256), Image.LANCZOS).point(lambda v: v * 140 // 255)
    write_blp("SlotShade", Image.merge("RGBA", (Image.new("L", (512, 256), 0),) * 3 + (fill,)))


def arch_mask(im):
    """The transparent arch inside the emblem: the region around the middle of its upper half that the opaque
    frame closes off, as an "L" mask (255 inside). Raises when there is none or it is not closed."""
    mask = im.getchannel("A").point(lambda a: 255 if a <= 8 else 0)
    seed = (im.width // 2, int(im.height * 0.38))
    if mask.getpixel(seed) != 255:
        raise SystemExit("the emblem has no transparent arch at %s" % (seed,))
    ImageDraw.floodfill(mask, seed, 128)
    arch = mask.point(lambda v: 255 if v == 128 else 0)
    box = arch.getbbox()
    if box[0] <= 1 or box[1] <= 1 or box[2] >= im.width - 1 or box[3] >= im.height - 1:
        raise SystemExit("the arch of the emblem is not closed (it reaches the edge of the image)")
    return arch


def arch_strips(arch, box, count=20, bleed=3):
    """The portrait is laid in horizontal strips that follow the arch (a rectangle would stick out at the round
    top, where the frame is not square). The top part (until the arch has its full width) is cut into `count`
    strips, the rest is one strip. Each strip is as wide as the widest row inside it plus `bleed` pixels that
    hide under the opaque frame. Returned as fractions of the arch box: (x1, y1, x2, y2); may lie a little
    outside 0..1 (the bleed)."""
    l, t, r, b = box
    w, h = r - l, b - t
    px = arch.load()
    rows = []
    for y in range(t, b):
        xs = [x for x in range(l, r) if px[x, y]]
        rows.append((min(xs), max(xs) + 1) if xs else (l + w // 2, l + w // 2))
    widest = max(x2 - x1 for x1, x2 in rows)
    full = next(i for i, (x1, x2) in enumerate(rows) if x2 - x1 >= widest * 0.995)
    edges = [round(full * i / count) for i in range(count + 1)] + [h]
    strips = []
    for i in range(len(edges) - 1):
        y1, y2 = edges[i], edges[i + 1]
        if y2 <= y1:
            continue
        x1 = min(rows[y][0] for y in range(y1, y2)) - bleed
        x2 = max(rows[y][1] for y in range(y1, y2)) + bleed
        top = y1 - (bleed if i == 0 else 0)
        bottom = y2 + (bleed if y2 == h else 0)
        strips.append(((x1 - l) / w, top / h, (x2 - l) / w, bottom / h))
    return strips


def build_emblem(src_name, out_name):
    size = 512
    im = crop_opaque(source(src_name))
    # the arch is measured on the cropped full size image, then scaled with it
    arch = arch_mask(im)
    box = arch.getbbox()
    scale = size / max(im.width, im.height)
    w, h = max(1, round(im.width * scale)), max(1, round(im.height * scale))
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(scale_premultiplied(im, (w, h)), (0, 0))
    write_blp(out_name, canvas)
    print("  %s: right = %.5f, bottom = %.5f, aspect (w / h) = %.5f" % (out_name, w / size, h / size, w / h))
    print("  %s: arch = { %.5f, %.5f, %.5f, %.5f },   -- left, top, right, bottom as fractions of the emblem"
          % (out_name, box[0] / im.width, box[1] / im.height, box[2] / im.width, box[3] / im.height))
    strips = arch_strips(arch, box)
    print("  %s: strips = {   -- x1, y1, x2, y2 as fractions of the arch box" % out_name)
    for st in strips:
        print("      { %.4f, %.4f, %.4f, %.4f }," % st)
    print("  },")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--src", help="source image of the card back (micro icon)")
    ap.add_argument("--only", help="comma separated: micro, bottle, victory, defeat, craft, banner, digits, glow (default: all with a source)")
    args = ap.parse_args()
    only = set(args.only.split(",")) if args.only else None

    def want(name):
        return only is None or name in only

    if want("micro"):
        src = args.src or next((p for p in SOURCES if os.path.exists(p)), None)
        if src and os.path.exists(src):
            build_micro_icon(src)
        elif only is not None:
            raise SystemExit("no source image: pass --src or put it at %s" % SOURCES[0])
    if want("bottle"):
        build_dust_bottle()
    if want("craft"):
        build_craft_panel()
    if want("glow"):
        build_slot_glow()
    if want("banner"):
        build_banner_blank()
    if want("digits"):
        build_banner_digits()
    if want("victory"):
        build_emblem("victory_emblem.png", "VictoryEmblem")
    if want("defeat"):
        build_emblem("defeat_emblem.png", "DefeatEmblem")


if __name__ == "__main__":
    main()
