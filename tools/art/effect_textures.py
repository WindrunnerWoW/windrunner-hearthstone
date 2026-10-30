#!/usr/bin/env python3
"""Generate the small procedural BLP textures used by the 2D spell effects.

The artwork is defined entirely by fixed equations (no external images or random
state), so running this script reproduces the shipped 256x256 DXT5 BLPs.
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

SIZE = 256
ORIGINAL_NAMES = ("fire_core", "soft_glow", "spark", "shockwave")
NEW_NAMES = ("neutral_core", "neutral_glow", "neutral_spark", "neutral_ring", "frost_shard", "slash_arc")
WAVE3_NAMES = ("lightning_arc", "smoke_puff", "portal_ring", "shield_shell")
NAMES = ORIGINAL_NAMES + NEW_NAMES + WAVE3_NAMES
OUT = os.path.join(ROOT, "addon", "Hearthstone", "Textures", "Effects")
MANIFEST = os.path.join(ROOT, "data", "art", "effect_textures.manifest.json")
REVIEW = os.path.join(ROOT, "art", "review", "effect_textures_contact.png")


def smooth(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def segment_distance(x, y, ax, ay, bx, by):
    dx, dy = bx-ax, by-ay
    denom = dx*dx + dy*dy
    t = 0.0 if denom == 0 else max(0.0, min(1.0, ((x-ax)*dx + (y-ay)*dy) / denom))
    return math.hypot(x-(ax+t*dx), y-(ay+t*dy))


def rgba_field(name, x, y):
    """Sample a primitive at normalized coordinates centered on the texture."""
    r = math.hypot(x, y)
    theta = math.atan2(y, x)
    if name == "soft_glow":
        alpha = max(0.0, 1.0 - smooth(0.0, 1.0, r)) ** 2.4
        return (255, 222 + int(28 * (1-r)), 150 + int(90 * (1-r)), int(245 * alpha))
    if name == "fire_core":
        # Fixed harmonics and offset lobes make a turbulent flame instead of a stamped blob.
        edge = .64 + .08 * math.sin(5*theta + .4) + .052 * math.sin(9*theta - .7) + .035 * math.cos(13*theta)
        edge += .035 * math.sin(3*theta + 1.2)
        flame = 1.0 - smooth(edge - .035, edge + .13, r)
        halo = math.exp(-((r / .79) ** 2) * 2.1) * .22 * (1.0 - smooth(.72, .92, r))
        lobes = 0.0
        for ox, oy, spread, strength in ((-.23, -.10, .19, .42), (.18, -.18, .16, .35),
                                         (-.10, .20, .20, .31), (.25, .12, .13, .28),
                                         (-.28, .22, .12, .18)):
            lobes += strength * math.exp(-(((x-ox)**2 + (y-oy)**2) / (spread*spread)))
        lobe_glow = min(.78, lobes)
        core = math.exp(-((r / .39) ** 2) * 2.4)
        hot = max(0.0, min(1.0, .78 * core + .32 * lobe_glow))
        alpha = min(1.0, flame * (.48 + .52 * core) + halo + .22*lobe_glow)
        red = 255
        green = int(72 + 174 * hot)
        blue = int(8 + 232 * max(0.0, min(1.0, (hot - .28) * 1.22)))
        return (red, green, blue, int(255 * alpha))
    if name == "spark":
        # A slender tapered ember tilted diagonally, with a small transparent warm halo.
        along = x * .42 - y * .91
        across = x * .91 + y * .42
        t = max(0.0, min(1.0, (along + .63) / 1.18))
        width = .018 + .105 * (1.0 - t) ** .72
        taper = abs(across - .035 * math.sin(t * math.pi))
        body = 1.0 - smooth(width * .70, width * 1.35, taper)
        body *= (1.0 - smooth(.47, .56, along)) * smooth(-.66, -.50, along)
        halo = math.exp(-((taper / (width * 2.8)) ** 2)) * body * .34
        alpha = min(1.0, body * .94 + halo)
        heat = max(0.0, min(1.0, 1.0 - t + .22 * math.exp(-(taper / .045) ** 2)))
        return (255, int(127 + 118*heat), int(30 + 205*heat), int(255*alpha))
    if name == "shockwave":
        # A broken, soft annulus. Its radial falloff makes a clean round wave, not a badge.
        wobble = .012 * math.sin(7*theta) + .008 * math.cos(11*theta + .3)
        d = abs(r - (.60 + wobble))
        ring = 1.0 - smooth(.035, .13, d)
        outer = 1.0 - smooth(.79, .94, r)
        gaps = .68 + .32 * max(0.0, math.sin(3*theta + .7))
        return (255, int(136 + 78 * (1-d/.13)), 40, int(205 * ring * outer * gaps))
    if name == "neutral_core":
        # Reuse the fire-core silhouette and alpha, with white luminosity for UI tinting.
        _, green, blue, alpha = rgba_field("fire_core", x, y)
        lum = int(.2126*255 + .7152*green + .0722*blue)
        return (lum, lum, lum, alpha)
    if name == "neutral_glow":
        alpha = max(0.0, 1.0 - smooth(0.0, 1.0, r)) ** 2.4
        lum = int(165 + 90 * (1-r))
        return (lum, lum, lum, int(245 * alpha))
    if name == "neutral_spark":
        along = x * .42 - y * .91
        across = x * .91 + y * .42
        t = max(0.0, min(1.0, (along + .63) / 1.18))
        width = .018 + .105 * (1.0 - t) ** .72
        taper = abs(across - .035 * math.sin(t * math.pi))
        body = 1.0 - smooth(width * .70, width * 1.35, taper)
        body *= (1.0 - smooth(.47, .56, along)) * smooth(-.66, -.50, along)
        halo = math.exp(-((taper / (width * 2.8)) ** 2)) * body * .34
        alpha = min(1.0, body * .94 + halo)
        lum = int(125 + 130 * max(0.0, min(1.0, 1.0-t)))
        return (lum, lum, lum, int(255*alpha))
    if name == "neutral_ring":
        wobble = .012 * math.sin(7*theta) + .008 * math.cos(11*theta + .3)
        d = abs(r - (.60 + wobble))
        ring = 1.0 - smooth(.035, .13, d)
        outer = 1.0 - smooth(.79, .94, r)
        gaps = .68 + .32 * max(0.0, math.sin(3*theta + .7))
        lum = int(155 + 85 * max(0.0, 1-d/.13))
        return (lum, lum, lum, int(205 * ring * outer * gaps))
    if name == "frost_shard":
        # Long faceted crystal aligned from lower-left to upper-right.
        along = x * .72 - y * .69
        across = x * .69 + y * .72
        t = (along + .78) / 1.56
        half = .15 * (1.0 - abs(2*t-1)) + .012
        edge = abs(across - .025 * math.sin(t*math.pi))
        body = (1.0 - smooth(half-.018, half+.018, edge)) * (1-smooth(.94, 1.0, abs(2*t-1)))
        # A narrow bright facet and soft outer edge make the silhouette read as crystal.
        facet = math.exp(-((across + .035) / .025) ** 2) * body
        alpha = min(1.0, .78*body + .2*facet)
        blue = int(210 + 45*facet)
        return (int(185+45*facet), int(225+25*facet), blue, int(255*alpha))
    if name == "slash_arc":
        # Crescent blade stroke: curved tapered body with a translucent central channel.
        t = (x + .78) / 1.56
        curve = .20 * math.sin((t-.5)*math.pi)
        width = .018 + .13 * max(0.0, math.sin(math.pi*max(0.0, min(1.0, t)))) ** .72
        d = abs(y-curve)
        body = (1.0-smooth(width-.018, width+.018, d)) * smooth(-.82, -.68, x) * (1-smooth(.68, .82, x))
        channel = 1.0 - .72 * math.exp(-(d / max(.012, width*.34))**2)
        alpha = body * channel
        lum = int(175 + 80 * max(0.0, 1-d/max(width,.01)))
        return (lum, lum, lum, int(235*alpha))
    if name == "lightning_arc":
        # Main jagged stroke plus two short forks; distance fields keep it crisp at game scale.
        paths = (
            ((-.42, -.68), (-.08, -.25), (-.27, -.18), (.08, .24), (-.04, .25), (.34, .66)),
            ((-.09, -.25), (-.47, .08), (-.24, .06)),
            ((.04, .22), (.43, .45), (.22, .46)),
        )
        d = 2.0
        for path in paths:
            for j in range(len(path)-1):
                d = min(d, segment_distance(x, y, path[j][0], path[j][1], path[j+1][0], path[j+1][1]))
        core = 1.0 - smooth(.018, .052, d)
        glow = math.exp(-((d/.12)**2)) * .46 * (1.0-smooth(.18, .30, d))
        alpha = min(1.0, core + glow)
        lum = int(165 + 90*core)
        return (lum, lum, lum, int(255*alpha))
    if name == "smoke_puff":
        # Overlapping irregular soft lobes produce a billowing cloud with wispy outer alpha.
        lobes = ((-.30, .10, .23, .75), (-.14, -.16, .25, .82), (.12, -.23, .24, .78),
                 (.34, -.06, .20, .72), (.20, .20, .24, .78), (-.12, .30, .21, .68))
        empty = 1.0
        for ox, oy, spread, strength in lobes:
            wobble = 1.0 + .055*math.sin(9*theta + ox*17) + .035*math.cos(13*theta + oy*11)
            coverage = strength * math.exp(-(((x-ox)**2 + (y-oy)**2) / ((spread*wobble)**2)))
            empty *= (1.0 - min(.96, coverage))
        density = 1.0-empty
        alpha = min(.88, density * .88)
        lum = int(125 + 105*density)
        return (lum, lum, lum, int(255*alpha))
    if name == "portal_ring":
        # Segmented ring with angular twist and a clean open center.
        radius = .57 + .025*math.sin(5*theta + 2*r) + .012*math.sin(9*theta - 1.4*r)
        d = abs(r-radius)
        band = 1.0 - smooth(.045, .105, d)
        segment = .28 + .72*(.5 + .5*math.sin(8*theta + 4*r))
        swirl = .72 + .28*max(0.0, min(1.0, (r-.30)/.42))
        alpha = band*segment*swirl
        lum = int(145 + 100*max(0.0, 1-d/.105))
        return (lum, lum, lum, int(240*alpha))
    if name == "shield_shell":
        # Rounded top, narrower shoulders and a tapered lower point from a radial shield contour.
        if math.sin(theta) < 0:
            bound = .52 + .13*(-math.sin(theta))*(1-.42*abs(math.cos(theta)))
        else:
            bound = .52 + math.sin(theta)*(.19-.20*abs(math.cos(theta)))
        d = abs(r-bound)
        shell = 1.0-smooth(.018, .052, d)
        interior = (1.0-smooth(bound-.10, bound-.035, r)) * .20
        alpha = min(1.0, shell*.95 + interior)
        lum = int(175 + 80*shell)
        return (lum, lum, lum, int(255*alpha))
    raise ValueError(name)


def render(name):
    # 2x fixed supersampling yields smooth alpha and is deterministic across runs.
    scale = 2
    im = Image.new("RGBA", (SIZE*scale, SIZE*scale))
    px = im.load()
    for py in range(im.height):
        y = (py + .5 - im.height/2) / (im.height/2)
        for xpix in range(im.width):
            x = (xpix + .5 - im.width/2) / (im.width/2)
            px[xpix, py] = rgba_field(name, x, y)
    return im.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def build():
    os.makedirs(OUT, exist_ok=True)
    records = {}
    for name in NAMES:
        image = render(name)
        blp, mip_count = convert_blp.build_blp(image)
        path = os.path.join(OUT, name + ".blp")
        with open(path, "wb") as f:
            f.write(blp)
        records[name] = {
            "path": "Interface\\Hearthstone\\Textures\\Effects\\" + name,
            "file": "addon/Hearthstone/Textures/Effects/" + name + ".blp",
            "size": [SIZE, SIZE], "format": "BLP2 DXT5 alpha8",
            "mipmaps": mip_count, "bytes": len(blp),
            "sha256": hashlib.sha256(blp).hexdigest(),
        }
    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"schema": 1, "generator": "tools/art/effect_textures.py", "entries": records}, f, indent=2)
        f.write("\n")
    make_contact_sheet()
    return records


def make_contact_sheet():
    thumb = 256
    columns = 5
    block_height = thumb * 3 + 28
    groups = (len(NAMES) + columns - 1) // columns
    sheet = Image.new("RGB", (thumb * columns, block_height * groups), (245, 245, 245))
    draw = ImageDraw.Draw(sheet)
    for i, name in enumerate(NAMES):
        fx = Image.open(os.path.join(OUT, name + ".blp")).convert("RGBA")
        tintable = name in NEW_NAMES + WAVE3_NAMES and name != "frost_shard"
        variants = ((23, 27, 34), (242, 242, 238), (23, 27, 34))
        for row, bg in enumerate(variants):
            shown = fx
            if row == 2 and tintable:
                shown = fx.copy()
                tint = (82, 164, 255) if i % 2 == 0 else (180, 92, 255)
                alpha = shown.getchannel("A")
                shown = Image.new("RGBA", fx.size, tint + (255,))
                shown.putalpha(alpha)
            tile = Image.new("RGBA", fx.size, bg + (255,))
            tile.alpha_composite(shown)
            sheet.paste(tile.convert("RGB"), ((i % columns)*thumb, (i // columns)*block_height + row*thumb))
        draw.text(((i % columns)*thumb+8, (i // columns)*block_height + thumb*3+5), name, fill=(20, 20, 20))
    os.makedirs(os.path.dirname(REVIEW), exist_ok=True)
    sheet.save(REVIEW)


if __name__ == "__main__":
    for name, info in build().items():
        print("%-12s %5d bytes  %s" % (name, info["bytes"], info["sha256"]))
    print("review: " + REVIEW)
