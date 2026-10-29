#!/usr/bin/env python3
"""Builds the textures of the arena window (ArenaUI.lua) as BLP2 (DXT5, mips) from the owner's images.

Sources (the owner's own images, committed): art/source/ui/arena/
  background.png  the arena screen without content, 1672 x 941 (the bottom 39 rows are a gold / settings bar that
                  is not part of the window and is cut off: the window is the top BG_CROP_H rows)
  play.png        the round "Play" button (on transparent)
  retire.png      the "RETIRE" plate (on transparent)
  back_frame.png  an empty plate of the same family (on transparent): the Back button and the heading banner of the
                  parchment ("Choose a Card"), with the text written by the addon

Output: art/out/UI/<name>.blp (committed like the other UI pieces; tools/deploy/deploy-addon.ps1 copies them to
Textures\\UI\\):
  ArenaBg_c<col>r<row>  the background scaled to BG_W x BG_H and cut into tiles of 512 (texture sides <= 512)
  ArenaArch             the golden hero arch of the background (the gold and the dark bevel inside it) with a transparent
                        opening and a transparent outside
                        (the addon lays it over the hero portraits: the chosen hero in the background's own arch, and
                        the three hero choices on the parchment)
  ArenaPlay, ArenaRetire, ArenaPlate   the buttons, cropped to their opaque area

The script prints the Lua table AU.ART for ArenaUI.lua (texture shares, aspects, the arch opening and the portrait
strips); paste it there when a source changes.

  tools/art/.venv/Scripts/python tools/art/arena_ui.py [--review]
      --review also writes art/review/arena_*.png (the pieces on a grey checker, to check the cut by eye)
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageChops, ImageDraw, ImageFilter  # noqa: E402

import artlib as A  # noqa: E402
from ui_icons import arch_strips, crop_opaque, scale_premultiplied, write_blp  # noqa: E402

SRC = os.path.join(A.ART, "source", "ui", "arena")
REVIEW = os.path.join(A.ART, "review")

BG_CROP_H = 902                # rows of background.png that belong to the window
BG_W, BG_H = 1536, 829         # the window background in texture pixels (1672 x 902 scaled by 0.9187)
TILE = 512

# The golden arch of the background (source pixels of background.png): a box around the frame, and a point inside
# its opening.
ARCH_BOX = (262, 604, 478, 838)
ARCH_SEED = (370, 730)
ARCH_TEX = 256                 # the arch piece is scaled to this height on a 256 x 256 texture
ARCH_BEVEL = 8                 # the dark inner bevel inside the gold (source pixels): part of the frame, over the portrait
ARCH_SHADOW = 28               # inside the bevel, pixels darker than this (max channel) are the slate's drop shadow, not
                               # the bevel: they go to the opening, so no black band sits over the portrait
ARCH_BEVEL_MIN = 5             # ... but this much bevel next to the gold always stays (it is dark on the right curve)


def source(name):
    path = os.path.join(SRC, name)
    if not os.path.exists(path):
        raise SystemExit("no source image %s" % path)
    return Image.open(path).convert("RGBA")


def review(name, im):
    os.makedirs(REVIEW, exist_ok=True)
    bg = Image.new("RGBA", im.size, (128, 128, 128, 255))
    d = ImageDraw.Draw(bg)
    for y in range(0, im.height, 16):
        for x in range(0, im.width, 16):
            if (x // 16 + y // 16) % 2:
                d.rectangle((x, y, x + 15, y + 15), fill=(160, 160, 160, 255))
    bg.alpha_composite(im)
    bg.convert("RGB").save(os.path.join(REVIEW, "arena_%s.png" % name))


def build_background(args):
    im = source("background.png").convert("RGB")
    if im.width != 1672 or im.height < BG_CROP_H:
        raise SystemExit("background.png is %dx%d, expected 1672 wide and at least %d high" % (im.width, im.height, BG_CROP_H))
    im = im.crop((0, 0, im.width, BG_CROP_H)).resize((BG_W, BG_H), Image.LANCZOS).convert("RGBA")
    cols, rows = (BG_W + TILE - 1) // TILE, (BG_H + TILE - 1) // TILE
    for r in range(rows):
        for c in range(cols):
            x, y = c * TILE, r * TILE
            piece = im.crop((x, y, min(BG_W, x + TILE), min(BG_H, y + TILE)))
            canvas = Image.new("RGBA", (TILE, TILE), (0, 0, 0, 255))
            canvas.paste(piece, (0, 0))
            write_blp("ArenaBg_c%dr%d" % (c, r), canvas)
    return {"bg": (cols, rows)}


def gold_mask(im):
    """255 where the pixel is the yellow gold of the frame."""
    out = Image.new("L", im.size, 0)
    src, dst = im.load(), out.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, _ = src[x, y]
            if r > 70 and r - b > 35 and g - b > 18 and r >= g:
                dst[x, y] = 255
    return out


def build_arch(args):
    bg = source("background.png")
    im = bg.crop(ARCH_BOX)
    gold = gold_mask(im)
    # close pin holes in the gold, so the opening and the outside do not leak into each other
    gold = gold.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3))
    # the opening: everything that is not gold and connected to the seed
    opening = gold.point(lambda v: 0 if v else 255)
    seed = (ARCH_SEED[0] - ARCH_BOX[0], ARCH_SEED[1] - ARCH_BOX[1])
    ImageDraw.floodfill(opening, seed, 128)
    opening = opening.point(lambda v: 255 if v == 128 else 0)
    box = opening.getbbox()
    # the frame keeps the dark bevel inside the gold, like the background's own arch: the portrait sits under it
    # the portrait goes up to the gold, under the bevel: the strips follow the opening before the bevel is cut off
    wide = opening
    opening = opening.filter(ImageFilter.MinFilter(2 * ARCH_BEVEL + 1))
    # the black shadow the gold casts on the empty slate is inside the bevel band (wider on the round top, where the
    # square filter cuts deeper): grow the opening into the dark pixels of the band, keeping the grey bevel
    dark = im.convert("RGB").point(lambda v: 255 if v < ARCH_SHADOW else 0)
    r, g, b = dark.split()
    dark = ImageChops.multiply(ImageChops.multiply(r, g), b)
    dark = ImageChops.multiply(dark, wide.filter(ImageFilter.MinFilter(2 * ARCH_BEVEL_MIN + 1)))
    for _ in range(2 * ARCH_BEVEL):
        opening = ImageChops.lighter(opening, ImageChops.multiply(opening.filter(ImageFilter.MaxFilter(3)), dark))
    # the specks of shadow left between them go too
    opening = opening.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))
    if not box or box[0] <= 1 or box[1] <= 1 or box[2] >= im.width - 1 or box[3] >= im.height - 1:
        raise SystemExit("the arch opening is not closed (it reaches the edge of ARCH_BOX): %s" % (box,))
    # the outside: everything that is not gold and connected to a corner
    outside = gold.point(lambda v: 0 if v else 255)
    for corner in ((0, 0), (im.width - 1, 0)):
        if outside.getpixel(corner) == 255:
            ImageDraw.floodfill(outside, corner, 128)
    outside = outside.point(lambda v: 255 if v == 128 else 0)
    # below the frame (the plate) is outside too: cut under the lowest gold row
    gold_box = gold.getbbox()
    alpha = ImageChops.invert(ImageChops.lighter(opening, outside))
    alpha.paste(0, (0, gold_box[3], im.width, im.height))
    alpha.paste(0, (0, 0, im.width, gold_box[1]))
    # soft 1 px edge
    alpha = alpha.filter(ImageFilter.GaussianBlur(0.6))
    piece = im.copy()
    piece.putalpha(alpha)
    # crop to the frame
    fb = alpha.point(lambda a: 255 if a > 8 else 0).getbbox()
    piece = piece.crop(fb)
    opening = opening.crop(fb)
    wide = wide.crop(fb)
    obox = opening.getbbox()
    scale = ARCH_TEX / piece.height
    w, h = max(1, round(piece.width * scale)), ARCH_TEX
    canvas = Image.new("RGBA", (ARCH_TEX, ARCH_TEX), (0, 0, 0, 0))
    canvas.paste(scale_premultiplied(piece, (w, h)), (0, 0))
    write_blp("ArenaArch", canvas)
    if args.review:
        review("arch", piece)
    frame = (ARCH_BOX[0] + fb[0], ARCH_BOX[1] + fb[1], ARCH_BOX[0] + fb[2], ARCH_BOX[1] + fb[3])
    return {
        "arch": {
            "right": w / ARCH_TEX, "bottom": h / ARCH_TEX, "aspect": piece.width / piece.height,
            "frame": frame,
            "opening": (obox[0] / piece.width, obox[1] / piece.height, obox[2] / piece.width, obox[3] / piece.height),
            "strips": arch_strips(wide, obox),
        }
    }


def build_button(src_name, out_name, args, size):
    im = crop_opaque(source(src_name))
    tw, th = size
    scale = min(tw / im.width, th / im.height)
    w, h = max(1, round(im.width * scale)), max(1, round(im.height * scale))
    canvas = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    canvas.paste(scale_premultiplied(im, (w, h)), (0, 0))
    write_blp(out_name, canvas)
    if args.review:
        review(out_name.lower(), canvas)
    return {out_name: {"right": w / tw, "bottom": h / th, "aspect": im.width / im.height}}


# The molten lava bar (lava_bar.png, 941 x 1672 with stray red specks around it): this box is the bar itself. It is
# stretched over 64 x 256 and the addon scales its height to the mana curve (ArenaUI.lua AU.bars).
LAVA_BOX = (322, 62, 624, 1598)
LAVA_TEX = (64, 256)


def build_lava(args):
    im = source("lava_bar.png").crop(LAVA_BOX)
    canvas = scale_premultiplied(im, LAVA_TEX)
    write_blp("ArenaLava", canvas)
    if args.review:
        review("arenalava", canvas)


def fit_piece(im, out_name, args, size):
    """Scales im to fit a texture of 'size' (top left, aspect kept); returns the AU.ART share/aspect entry."""
    tw, th = size
    scale = min(tw / im.width, th / im.height)
    w, h = max(1, round(im.width * scale)), max(1, round(im.height * scale))
    canvas = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    canvas.paste(scale_premultiplied(im, (w, h)), (0, 0))
    write_blp(out_name, canvas)
    if args.review:
        review(out_name.lower(), canvas)
    return {out_name: {"right": w / tw, "bottom": h / th, "aspect": im.width / im.height}}


# The run panel of the settled screen: the purple key emblem (its laurel medallion holds the win count, "Wins"
# is part of the art) and the "Losses:" label with its box. The banner source has the label and three boxes: only
# the first box is used (one loss ends a run), and ArenaLossesHit is the same piece with the red X laid on the box.
LOSS_CROP = (140, 170, 1110, 520)                 # label + first box (+ room for the X overhang), banner pixels
LOSS_BOX = (766, 211)                             # the first box: its top left in the banner ...
LOSS_X_FRAME = (253, 303, 1001)                   # ... and the frame of the X button (left, top, right) in loss_x.png


def build_run_art(args):
    info = {}
    key = crop_opaque(source("key_emblem.png"))
    info.update(fit_piece(key, "ArenaKey", args, (512, 256)))
    banner = source("losses_banner.png")
    box_w = 328                                   # width of the first box in the banner
    # hide the 2nd and 3rd box: keep only what is left of the first gap
    keep = Image.new("RGBA", banner.size, (0, 0, 0, 0))
    keep.paste(banner.crop((0, 0, 1100, banner.height)), (0, 0))
    plain = keep.crop(LOSS_CROP)
    x = source("loss_x.png")
    k = box_w / (LOSS_X_FRAME[2] - LOSS_X_FRAME[0])
    xs = x.resize((round(x.width * k), round(x.height * k)), Image.LANCZOS)
    hit = keep.copy()
    hit.alpha_composite(xs, (round(LOSS_BOX[0] - LOSS_X_FRAME[0] * k), round(LOSS_BOX[1] - LOSS_X_FRAME[1] * k)))
    hit = hit.crop(LOSS_CROP)
    info.update(fit_piece(plain, "ArenaLosses", args, (512, 256)))
    info.update(fit_piece(hit, "ArenaLossesHit", args, (512, 256)))
    return info


def lua(info):
    cols, rows = info["bg"]
    a = info["arch"]
    out = []
    out.append("AU.ART = {   -- printed by tools/art/arena_ui.py")
    out.append("    bg = { cols = %d, rows = %d, w = %d, h = %d, tile = %d, srcW = 1672, srcH = %d }," % (cols, rows, BG_W, BG_H, TILE, BG_CROP_H))
    for name in ("ArenaPlay", "ArenaRetire", "ArenaPlate", "ArenaKey", "ArenaLosses", "ArenaLossesHit"):
        b = info[name]
        out.append("    %s = { right = %.5f, bottom = %.5f, aspect = %.5f }," % (name, b["right"], b["bottom"], b["aspect"]))
    out.append("    ArenaArch = {")
    out.append("        right = %.5f, bottom = %.5f, aspect = %.5f," % (a["right"], a["bottom"], a["aspect"]))
    out.append("        frame = { %d, %d, %d, %d },   -- the arch in background source pixels (left, top, right, bottom)" % a["frame"])
    out.append("        opening = { %.5f, %.5f, %.5f, %.5f }," % a["opening"])
    out.append("        strips = {")
    for st in a["strips"]:
        out.append("            { %.4f, %.4f, %.4f, %.4f }," % st)
    out.append("        },")
    out.append("    },")
    out.append("}")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--review", action="store_true", help="also write art/review/arena_*.png")
    args = ap.parse_args()
    info = {}
    info.update(build_background(args))
    info.update(build_arch(args))
    info.update(build_button("play.png", "ArenaPlay", args, (512, 512)))
    info.update(build_button("retire.png", "ArenaRetire", args, (512, 256)))
    info.update(build_button("back_frame.png", "ArenaPlate", args, (512, 256)))
    build_lava(args)
    info.update(build_run_art(args))
    print()
    print(lua(info))


if __name__ == "__main__":
    main()
