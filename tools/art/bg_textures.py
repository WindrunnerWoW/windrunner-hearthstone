#!/usr/bin/env python3
"""Generate the small procedural BLP textures used by the Warsong Battlegrounds window.

The Battlegrounds window (addon/Hearthstone/Battlegrounds/BgShopUI.lua) reuses the collection's button plates
(HS_ButtonTextures), so only what has nothing to fit is made here: the gold ring drawn over a golden minion's portrait
(the portrait itself is the normal card's, see tools/art/bg_minions.py), a gold coin for the Gold line and a tavern
tier pip.

The artwork is defined entirely by fixed equations (no external images or random state), so running this script
reproduces the shipped BLPs. Like tools/art/effect_textures.py the BLPs are committed (they are ours, not Blizzard's).

  tools/art/.venv/Scripts/python tools/art/bg_textures.py

Output: addon/Hearthstone/Textures/Bg/<name>.blp (committed), data/art/bg_textures.manifest.json (committed: sizes and
        hashes), art/review/bg_textures_contact.png (gitignored contact sheet).
Standard library + Pillow only.
"""
import hashlib
import json
import math
import os
import sys

from PIL import Image, ImageDraw

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(__file__))
import convert_blp  # noqa: E402

# name -> texture size (a power of two; DXT5 blocks want multiples of 4)
NAMES = {"golden_ring": 128, "coin": 32, "tier_pip": 32, "hero_frame": 64, "hero_glow": 128,
         "quest_frame": 512, "reward_card": 256, "quest_glow": 512, "yogg_wheel": 256, "yogg_pointer": 256, "yogg_glow": 128}
# quest_frame: the stone and parchment plate of a quest (tools/art/assets/quest_frame.png), 512 wide on a 512x512 canvas,
# top aligned: the addon uses texcoords (0, 1, 0, QUEST_FRAME_H / 512). reward_card: the card a quest's reward is drawn
# on (the 256x388 card part of a 256x512 canvas, like the card renders; the reward portrait shows through the round window).
QUEST_FRAME_SRC = os.path.join(ROOT, "tools", "art", "assets", "quest_frame.png")
QUEST_FRAME_H = 348
CARD_W, CARD_H = 256, 388
# quest_glow: the green halo of a quest plate (ADD blended behind it): the plate scaled to GLOW_W x GLOW_H at
# (GLOW_PAD, GLOW_PAD) of a 512x512 canvas, grown and blurred; the addon texcoord bottom is GLOW_CANVAS_H / 512.
GLOW_W, GLOW_H, GLOW_PAD, GLOW_CANVAS_H = 448, 304, 32, 368
# yogg_wheel / yogg_pointer: the Wheel of Yogg-Saron (Yogg-tastic Tasties) and its red pointer, drawn from the two
# pictures tools/art/assets/yogg_wheel.png and yogg_pointer.png. The wheel is cropped square around the disc and scaled to
# 256x256 (the addon spins it with texture coordinates); the pointer keeps its shape on a 128x256 canvas (the addon uses
# texcoords 0..1 on a 1 : 2 frame), the brass pivot near the top. yogg_glow: a purple halo, the disc filling 2/3 of it.
YOGG_WHEEL_SRC = os.path.join(ROOT, "tools", "art", "assets", "yogg_wheel.png")
YOGG_POINTER_SRC = os.path.join(ROOT, "tools", "art", "assets", "yogg_pointer.png")
SPECIAL = ("quest_frame", "reward_card", "quest_glow", "yogg_wheel", "yogg_pointer")
OUT = os.path.join(ROOT, "addon", "Hearthstone", "Textures", "Bg")
MANIFEST = os.path.join(ROOT, "data", "art", "bg_textures.manifest.json")
REVIEW = os.path.join(ROOT, "art", "review", "bg_textures_contact.png")

GOLD = (232, 190, 88)
GOLD_LIT = (255, 236, 168)
GOLD_DARK = (140, 96, 26)
GOLD_DEEP = (92, 60, 14)


def smooth(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def golden_ring(x, y):
    """A gold oval band a little inside the portrait's rim, lit from the upper left."""
    c = (128 - 1) / 2.0
    dx, dy = x - c, y - c
    r = math.hypot(dx, dy)
    outer, inner = 61.5, 52.0
    band = 1.0 - smooth(inner - 2.0, inner + 3.0, r) if r < inner else 1.0 - smooth(outer - 3.0, outer + 2.0, r)
    if r <= inner:
        alpha = 0.0
    elif r >= outer + 2.0:
        alpha = 0.0
    else:
        alpha = max(0.0, band)
    # lighting: bright at the top left, darker at the bottom right
    n = max(-1.0, min(1.0, (dx * -0.7071 + dy * -0.7071) / max(1.0, outer)))
    t = 0.5 + 0.5 * n
    col = mix(GOLD_DARK, GOLD_LIT, t * t)
    if r > (inner + outer) / 2.0:
        col = mix(col, GOLD_DEEP, 0.35)          # a darker outer lip
    return col + (int(round(255 * alpha * 0.96)),)


def coin(x, y):
    """A plain gold coin: soft rim, lighter face, a small highlight."""
    c = (32 - 1) / 2.0
    dx, dy = x - c, y - c
    r = math.hypot(dx, dy)
    if r > 15.6:
        return (0, 0, 0, 0)
    edge = 1.0 - smooth(13.2, 15.4, r)
    rim = smooth(11.0, 12.8, r)                  # 0 in the middle, 1 at the rim
    face = mix(GOLD, GOLD_LIT, max(0.0, 1.0 - (dx * 0.7 + dy * 0.9) / 18.0) * 0.5)
    col = mix(face, GOLD_DARK, rim)
    if r > 14.2:
        col = mix(col, GOLD_DEEP, 0.5)
    return col + (int(round(255 * edge)),)


def tier_pip(x, y):
    """A small round gold pip (the window lights the first `tier` of six)."""
    c = (32 - 1) / 2.0
    dx, dy = x - c, y - c
    r = math.hypot(dx, dy)
    if r > 13.2:
        return (0, 0, 0, 0)
    edge = 1.0 - smooth(11.0, 13.0, r)
    rim = smooth(9.2, 10.8, r)
    face = mix(GOLD, GOLD_LIT, max(0.0, 1.0 - (dx * 0.6 + dy * 0.9) / 16.0) * 0.55)
    col = mix(face, GOLD_DARK, rim)
    return col + (int(round(255 * edge)),)


def rr_dist(x, y, size, inset, radius):
    """Signed distance (negative inside) of the rounded square inset pixels from the edge of a size x size texture."""
    c = (size - 1) / 2.0
    half = c - inset - radius + 0.5
    qx, qy = abs(x - c) - half, abs(y - c) - half
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - radius


def hero_frame(x, y):
    """White rounded-square ring around a lobby portrait (the addon tints it: faction blue / red, gold for you)."""
    outer = rr_dist(x, y, 64, 1.5, 10.0)
    inner = rr_dist(x, y, 64, 5.5, 6.0)
    alpha = (1.0 - smooth(-0.8, 0.6, outer)) * smooth(-0.6, 0.8, inner)
    n = max(-1.0, min(1.0, ((x - 31.5) * -0.7071 + (y - 31.5) * -0.7071) / 40.0))
    v = int(round(200 + 55 * n))         # bevel: lit from the upper left
    return (v, v, v, int(round(255 * alpha)))


def hero_glow(x, y):
    """White soft glow around a rounded square (the next opponent's red glow, a selection)."""
    d = rr_dist(x, y, 128, 36.0, 12.0)
    alpha = math.exp(-d / 9.0) * 0.9 * (1.0 - smooth(14.0, 22.0, d)) if d > 0 else 0.0
    return (255, 255, 255, int(round(255 * min(1.0, alpha))))


def yogg_glow(x, y):
    """A soft purple halo round a disc of radius 42 (the wheel stands on it, ADD blended)."""
    c = (128 - 1) / 2.0
    r = math.hypot(x - c, y - c)
    alpha = 1.0 if r < 42.0 else math.exp(-(r - 42.0) / 8.0)
    alpha *= 1.0 - smooth(54.0, 63.0, r)
    return (176, 84, 255, int(round(255 * min(1.0, alpha * 0.9))))


FIELDS = {"yogg_glow": yogg_glow, "golden_ring": golden_ring, "coin": coin, "tier_pip": tier_pip, "hero_frame": hero_frame, "hero_glow": hero_glow}


def quest_frame():
    src = Image.open(QUEST_FRAME_SRC).convert("RGBA").resize((512, QUEST_FRAME_H), Image.LANCZOS)
    canvas = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
    canvas.paste(src, (0, 0))
    return canvas


def quest_glow():
    from PIL import ImageFilter
    src = Image.open(QUEST_FRAME_SRC).convert("RGBA").resize((GLOW_W, GLOW_H), Image.LANCZOS)
    mask = Image.new("L", (512, 512), 0)
    mask.paste(src.split()[3], (GLOW_PAD, GLOW_PAD))
    grown = mask.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(9))
    alpha = grown.point(lambda v: min(255, int(v * 1.5)))
    im = Image.new("RGBA", (512, 512), (70, 255, 50, 0))
    im.putalpha(alpha)
    clip = Image.new("L", (512, 512), 0)
    clip.paste(255, (0, 0, 512, GLOW_CANVAS_H))
    im.putalpha(Image.composite(im.split()[3], Image.new("L", (512, 512), 0), clip))
    return im


REWARD_FRAME_SRC = os.path.join(ROOT, "tools", "art", "assets", "reward_frame_blank.png")


def reward_card():
    """The blank reward card the owner rendered (tools/art/assets/reward_frame_blank.png): a purple and gold frame with a
    dark stone window for the portrait (drawn over it by the addon), an empty banner for the name and an empty parchment
    box for the text. Cut to its edges and scaled to 256x388 on the top of a 256x512 canvas."""
    src = Image.open(REWARD_FRAME_SRC).convert("RGBA")
    card = src.crop(src.split()[3].getbbox()).resize((CARD_W, CARD_H), Image.LANCZOS)
    canvas = Image.new("RGBA", (256, 512), (0, 0, 0, 0))
    canvas.paste(card, (0, 0))
    return canvas


def yogg_wheel():
    src = Image.open(YOGG_WHEEL_SRC).convert("RGBA")
    l, t, r, b = src.split()[3].getbbox()
    side = max(r - l, b - t) + 4
    cx, cy = (l + r) / 2.0, (t + b) / 2.0
    box = (int(round(cx - side / 2.0)), int(round(cy - side / 2.0)))
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(src, (-box[0], -box[1]))
    return square.resize((256, 256), Image.LANCZOS)


def yogg_pointer():
    src = Image.open(YOGG_POINTER_SRC).convert("RGBA")
    src = src.crop(src.split()[3].getbbox())
    h = 250
    w = int(round(src.width * h / float(src.height)))
    src = src.resize((w, h), Image.LANCZOS)
    canvas = Image.new("RGBA", (128, 256), (0, 0, 0, 0))
    canvas.paste(src, ((128 - w) // 2, 3))
    return canvas


def render(name):
    if name == "yogg_wheel":
        return yogg_wheel()
    if name == "yogg_pointer":
        return yogg_pointer()
    if name == "quest_frame":
        return quest_frame()
    if name == "reward_card":
        return reward_card()
    if name == "quest_glow":
        return quest_glow()
    size = NAMES[name]
    field = FIELDS[name]
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = im.load()
    for y in range(size):
        for x in range(size):
            px[x, y] = field(x, y)
    return im


def build():
    os.makedirs(OUT, exist_ok=True)
    records = {}
    for name in sorted(NAMES):
        image = render(name)
        blp, mip_count = convert_blp.build_blp(image)
        with open(os.path.join(OUT, name + ".blp"), "wb") as f:
            f.write(blp)
        records[name] = {
            "path": "Interface\\AddOns\\Hearthstone\\Textures\\Bg\\" + name,
            "file": "addon/Hearthstone/Textures/Bg/" + name + ".blp",
            "size": list(image.size), "format": "BLP2 DXT5 alpha8",
            "mipmaps": mip_count, "bytes": len(blp),
            "sha256": hashlib.sha256(blp).hexdigest(),
        }
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"schema": 1, "generator": "tools/art/bg_textures.py", "entries": records}, f, indent=2)
        f.write("\n")
    make_contact_sheet()
    return records


def make_contact_sheet():
    """The ring over a real portrait plus the small textures at 1x and 3x, on light and dark backgrounds."""
    from PIL import Image as I
    cell = 200
    sheet = I.new("RGB", (cell * 3 + 140, cell + 90), (245, 245, 245))
    dr = ImageDraw.Draw(sheet)
    # the ring over a portrait, if one is cached from bg_minions.py
    sample = os.path.join(ROOT, "art", "out", "BgMinions", "BG19_010.blp")
    tile = I.new("RGB", (128, 128), (110, 110, 110))
    if os.path.exists(sample):
        tile.paste(I.open(sample).convert("RGBA"), (0, 0), I.open(sample).convert("RGBA").split()[3])
    ring = I.open(os.path.join(OUT, "golden_ring.blp")).convert("RGBA")
    tile.paste(ring, (0, 0), ring.split()[3])
    sheet.paste(tile, (10, 10))
    dr.text((12, cell - 4), "golden_ring over a portrait", fill=(30, 30, 30))
    for i, name in enumerate(("coin", "tier_pip", "hero_frame")):
        img = I.open(os.path.join(OUT, name + ".blp")).convert("RGBA")
        big = img.resize((96, 96), I.NEAREST)
        for row, bg in enumerate(((36, 36, 40), (245, 245, 245))):
            back = I.new("RGB", (110, 110), bg)
            back.paste(big, (7, 7), big.split()[3])
            sheet.paste(back, (cell + i * 140, 10 + row * 96))
        dr.text((cell + i * 140 + 4, cell - 4), name, fill=(30, 30, 30))
    os.makedirs(os.path.dirname(REVIEW), exist_ok=True)
    sheet.save(REVIEW)
    return REVIEW


if __name__ == "__main__":
    for name, info in build().items():
        print("%-12s %6d bytes  %s" % (name, info["bytes"], info["sha256"]))
    print("review: " + REVIEW)
