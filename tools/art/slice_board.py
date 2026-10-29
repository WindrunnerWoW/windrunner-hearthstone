#!/usr/bin/env python3
"""Slice the owner's UI asset sheets into board textures (part D).

Reads data/art/board_pieces.json (sheet file, crop rectangle, background mode, size, anchors for every
piece), cuts each piece out of its sheet (build/assets/*.png, 1536x1024 RGB on black), removes the black
background, optionally scales it, pads it to a power-of-two canvas and writes

  art/out/Board/<name>.blp          BLP2 DXT5 with mip chain (gitignored, like art/out/Cards)
  art/review/board/<name>.png       preview on a checkerboard (gitignored) + _contact.png of all pieces
  data/art/board_textures.manifest.json   w/h/right/bottom/anchors per piece (committed; deploy-addon.ps1
                                    reads it to write BoardTextures.lua, so deploy needs no Python)

Usage (module root):
  tools/art/.venv/Scripts/python tools/art/slice_board.py              # every piece
  tools/art/.venv/Scripts/python tools/art/slice_board.py --only gem_health card_back
  ... --no-blp       previews and manifest only (fast), --verify decode the BLPs and print the error
  ... --dump arch_top arch_bottom   only write the raw crops, 4x enlarged with a 10-pixel grid, to
                     art/review/board/<name>_raw.png (to choose seeds and tolerances; nothing else changes)

Pieces (keys of an entry in board_pieces.json; coordinates are sheet pixels unless noted):
  name, sheet (key of "sheets"), rect [x, y, w, h]
  mode        "flood": background = pixels with max(r,g,b) <= tolerance that are connected to the crop
                       edge (enclosed dark areas stay opaque); the alpha edge is shrunk and softened.
              "luma":  alpha from brightness (glows, sparkles): alpha = (max(r,g,b) - tolerance) /
                       (knee - tolerance), colour un-premultiplied. Dark areas that are enclosed by bright
                       ones (text, holes) stay opaque (knee = brightness that counts as solid).
              "none":  fully opaque (use with clip).
              "alpha": the sheet is RGBA ({"file": ..., "alpha": true} in "sheets"): its own alpha is kept.
  tolerance   noise floor of the black background (default 30 for flood, 10 for luma)
  knee        luma only, default 255
  shrink      flood only: pixels the alpha is eroded by before softening (default 1), soften (default 0.8)
  keep        flood only: "largest" (default) keeps only the biggest connected piece
  clip        [shape, ...] opaque only inside the union of the shapes (crop-relative pixels, or sheet
              pixels with "clip_sheet": true; holes likewise):
              {"shape": "ellipse"|"rect", "rect": [l, t, r, b]} or {"shape": "polygon", "points": [[x, y], ...]}
  holes       same shapes, made transparent
  hole_flood  {"seeds": [[x, y], ...], "min": m, "tolerance": t, "warm_only": b, "symmetric": b, "grow": g,
              "soften": s}: transparent where m <= max(r,g,b) <= t (and with warm_only not bluish, b <= r) and
              4-connected to a seed (crop-relative pixels): cuts the opening out of a frame (the hero arches),
              following the real edge of the art. symmetric: each row becomes one span, mirrored around the
              opening's centre and smoothed over 5 rows (a clean arch from a ragged fill); then grown by g
              pixels (default 1) and softened
  feather     n: alpha fades to 0 over the n outermost pixels of the crop (an overlay cut out of a bigger
              piece then blends into that piece without a seam)
  mirror_bottom  the crop holds the upper half of a symmetric piece, the lower half is its vertical mirror
  derive      {"from": <piece>, "saturation": f, "brightness": f, "tint": [r, g, b], "tint_amount": f,
              "channels": "ggb"}: no crop, the piece is a recoloured copy of an earlier piece (empty crystal, armor gem);
              channels names the source channel (r, g or b) of each output channel (the yellow glow is the green one
              with its green copied into red)
  trim        true: crop to the visible pixels (alpha > 8) before scaling
  size        [w, h]: scale to fit inside (aspect kept), or exactly with "stretch": true
  slice9      [left, top, right, bottom] margins (source px of the cut-out): nine-slice resize to "size",
              corners are kept 1:1 (used for outline glows that must fit a tall card)
  silhouette  {"from": <piece>, "sigma": s, "inside": d, "peak": a, "margin": m}: no crop, a white soft glow that follows
              the solid outline of an earlier piece (a Taunt frame's shape, pointed or round), on a canvas m pixels
              larger on every side; the piece's rectangle is its "inner" anchor
  halo        {"margin": m, "sigma": s, "peak": a, "color": [r, g, b]}: after scaling, the piece on a canvas m pixels
              larger on every side over a soft glow of its own alpha in its mean colour (or "color"); the piece's
              rectangle is its "inner" anchor (deck_empty: the glowing bar laid into an empty deck slot)
  tex         [w, h] force the texture size (default: next power of two of the image size)
  stretch     with size: scale to exactly size; the manifest then records the native aspect (w/h of the crop)
  anchors     {name: {left, top, right, bottom}} fractions of the crop rectangle (kept through trim/scale)
  anchors_out same, but fractions of the FINAL used image (after trim and scaling): for anchors that were measured
              on the finished piece (the "oval" interior of minion_oval)
  procedural  {"kind": "glow", "shape": "ellipse"|"arch"|"gothic", "inner": [l, t, r, b], "sigma": s, "inside": d,
              "peak": a, "dome": f}: no crop, our own drawing: a white soft glow (alpha only carries the glow, the
              colour is white so the addon tints it with SetVertexColor) around a shape. "inner" is the shape's
              rectangle as fractions of the canvas (size), "sigma" the outside fall-off and "inside" the depth of the
              faint inner edge in canvas pixels, "peak" the alpha at the shape's edge (0..1), "dome" (arch, gothic; "round" for gothic: 1 elliptic, smaller more pointed) the
              height of the elliptic top as a share of the shape's height. The shape rectangle is written as the
              piece's "inner" anchor: the addon maps it onto the frame it glows around (MatchUI MU.LayoutGlow).
              "kind": "bubble" instead: a speech bubble in full colour (procedural_bubble): "body" [l, t, r, b] texels,
              "radius", "tail" [[x, y] x3] texels, "rim" width and "rim_color", "fill" RGB; no anchors.

The image is placed in the top-left corner of the texture; the manifest/BoardTextures.lua values are
w, h (the used pixels), right = w / texture width, bottom = h / texture height (SetTexCoord(0, right, 0, bottom)).
Standard library + Pillow only (as the other art tools). Deterministic: same input, same bytes.
"""
import argparse
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageChops, ImageDraw, ImageEnhance, ImageFilter  # noqa: E402

import artlib as A  # noqa: E402
import convert_blp as C  # noqa: E402

PIECES = os.path.join(A.ART_DATA, "board_pieces.json")
MANIFEST = os.path.join(A.ART_DATA, "board_textures.manifest.json")
OUT = os.path.join(A.ART, "out", "Board")
REVIEW = os.path.join(A.REVIEW, "board")
SHEET_DIR = os.path.join(A.ROOT, "build", "assets")
MAX_TEX = 512


# ---------------------------------------------------------------- helpers

def pow2(n):
    p = 4
    while p < n:
        p *= 2
    return p


def maxch(im):
    r, g, b = im.convert("RGB").split()
    return ImageChops.lighter(ImageChops.lighter(r, g), b)


def flood_bg(mx, tol):
    """Pixels with value <= tol that are 4-connected to a border pixel with value <= tol (1 = background)."""
    w, h = mx.size
    d = mx.tobytes()
    bg = bytearray(w * h)
    stack = []
    for x in range(w):
        for y in (0, h - 1):
            p = y * w + x
            if d[p] <= tol and not bg[p]:
                bg[p] = 1
                stack.append(p)
    for y in range(h):
        for x in (0, w - 1):
            p = y * w + x
            if d[p] <= tol and not bg[p]:
                bg[p] = 1
                stack.append(p)
    while stack:
        p = stack.pop()
        x = p % w
        if x > 0 and not bg[p - 1] and d[p - 1] <= tol:
            bg[p - 1] = 1
            stack.append(p - 1)
        if x < w - 1 and not bg[p + 1] and d[p + 1] <= tol:
            bg[p + 1] = 1
            stack.append(p + 1)
        if p >= w and not bg[p - w] and d[p - w] <= tol:
            bg[p - w] = 1
            stack.append(p - w)
        if p < w * (h - 1) and not bg[p + w] and d[p + w] <= tol:
            bg[p + w] = 1
            stack.append(p + w)
    return bg


def flood_seeds(mx, tol, seeds, lo=0, warm=None):
    """Pixels with lo <= value <= tol (and warm[p] when given) 4-connected to one of the seeds (1 = filled)."""
    w, h = mx.size
    d = mx.tobytes()
    out = bytearray(w * h)
    stack = []

    def fits(p):
        return lo <= d[p] <= tol and (warm is None or warm[p])

    for x, y in seeds:
        if not (0 <= x < w and 0 <= y < h):
            raise ValueError("hole_flood seed %r outside the %dx%d crop" % ([x, y], w, h))
        p = y * w + x
        if not fits(p):
            raise ValueError("hole_flood seed %r does not fit (brightness %d, allowed %d..%d); run slice_board.py "
                             "--dump <piece> and look at art/review/board/<piece>_raw.png" % ([x, y], d[p], lo, tol))
        if not out[p]:
            out[p] = 1
            stack.append(p)
    while stack:
        p = stack.pop()
        x = p % w
        for q, ok in ((p - 1, x > 0), (p + 1, x < w - 1), (p - w, p >= w), (p + w, p < w * (h - 1))):
            if ok and not out[q] and fits(q):
                out[q] = 1
                stack.append(q)
    return out


def symmetric_rows(filled, w, h):
    """One span per row, mirrored around the centre of the filled area and smoothed over 5 rows."""
    spans = {}
    for y in range(h):
        xs = [x for x in range(w) if filled[y * w + x]]
        if xs:
            spans[y] = (min(xs), max(xs))
    if not spans:
        return filled
    cx = sum(l + r for l, r in spans.values()) / (2.0 * len(spans))
    half = {y: max(cx - l, r - cx) for y, (l, r) in spans.items()}
    out = bytearray(w * h)
    for y in spans:
        near = sorted(half[yy] for yy in range(y - 2, y + 3) if yy in half)
        hw = near[len(near) // 2]
        for x in range(max(0, int(round(cx - hw))), min(w, int(round(cx + hw)) + 1)):
            out[y * w + x] = 1
    return out


def feather_mask(size, n):
    """L mask: 255 inside, a linear ramp to 0 over the n outermost pixels."""
    w, h = size
    m = Image.new("L", (w, h), 0)
    px = m.load()
    for y in range(h):
        for x in range(w):
            dist = min(x, y, w - 1 - x, h - 1 - y)
            px[x, y] = 255 if dist >= n else int(255 * (dist + 1) / float(n + 1))
    return m


def largest_component(mask_img):
    """mask_img: L image (0/255). Returns an L image holding only the largest 4-connected blob."""
    w, h = mask_img.size
    d = mask_img.tobytes()
    seen = bytearray(w * h)
    best, best_n = None, 0
    for s in range(w * h):
        if d[s] and not seen[s]:
            comp = [s]
            seen[s] = 1
            i = 0
            while i < len(comp):
                p = comp[i]
                i += 1
                x = p % w
                for q, ok in ((p - 1, x > 0), (p + 1, x < w - 1), (p - w, p >= w), (p + w, p < w * (h - 1))):
                    if ok and d[q] and not seen[q]:
                        seen[q] = 1
                        comp.append(q)
            if len(comp) > best_n:
                best, best_n = comp, len(comp)
    out = bytearray(w * h)
    for p in best or []:
        out[p] = 255
    return Image.frombytes("L", (w, h), bytes(out))


def shapes_mask(size, shapes, ss=4):
    """Union of shapes as an anti-aliased L mask (drawn at ss times the size)."""
    w, h = size
    m = Image.new("L", (w * ss, h * ss), 0)
    dr = ImageDraw.Draw(m)
    for s in shapes:
        k = s["shape"]
        if k == "polygon":
            dr.polygon([(x * ss, y * ss) for x, y in s["points"]], fill=255)
        else:
            l, t, r, b = s["rect"]
            box = [l * ss, t * ss, r * ss, b * ss]
            if k == "ellipse":
                dr.ellipse(box, fill=255)
            elif k == "rect":
                dr.rectangle(box, fill=255)
            else:
                raise ValueError("unknown shape " + k)
    return m.resize((w, h), Image.BOX)


def bleed_color(im, passes=4):
    """Give fully transparent pixels the colour of the nearest visible neighbours, so texture
    filtering does not pull black into the edges."""
    px = im.load()
    w, h = im.size
    known = [[px[x, y][3] > 0 for x in range(w)] for y in range(h)]
    for _ in range(passes):
        upd = []
        for y in range(h):
            for x in range(w):
                if known[y][x]:
                    continue
                sr = sg = sb = n = 0
                for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, 1), (-1, 1), (1, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < w and 0 <= yy < h and known[yy][xx]:
                        c = px[xx, yy]
                        sr += c[0]
                        sg += c[1]
                        sb += c[2]
                        n += 1
                if n:
                    upd.append((x, y, (sr // n, sg // n, sb // n, 0)))
        for x, y, c in upd:
            px[x, y] = c
            known[y][x] = True
    return im


def resize_rgba(im, size):
    """Resize with premultiplied alpha (no dark halos)."""
    return im.convert("RGBa").resize(size, Image.LANCZOS).convert("RGBA")


def alpha_bbox(im, thr=8):
    a = im.split()[3].point(lambda v: 255 if v > thr else 0)
    return a.getbbox()


# ---------------------------------------------------------------- piece processing

def cut(piece, sheets):
    sheet = sheets[piece["sheet"]]
    x, y, w, h = piece["rect"]
    im = sheet.crop((x, y, x + w, y + h))
    if sheet.mode != "RGBA":
        im = im.convert("RGB")
    if piece.get("mirror_bottom"):
        full = Image.new("RGB", (w, h * 2))
        full.paste(im, (0, 0))
        full.paste(im.transpose(Image.FLIP_TOP_BOTTOM), (0, h))
        im = full
    return im


def shift_shapes(shapes, dx, dy):
    out = []
    for s in shapes:
        s = dict(s)
        if "points" in s:
            s["points"] = [[x - dx, y - dy] for x, y in s["points"]]
        if "rect" in s:
            l, t, r, b = s["rect"]
            s["rect"] = [l - dx, t - dy, r - dx, b - dy]
        out.append(s)
    return out


def make_alpha(piece, rgb):
    mode = piece.get("mode", "flood")
    w, h = rgb.size
    ox, oy = (piece["rect"][0], piece["rect"][1]) if piece.get("clip_sheet") else (0, 0)
    if mode == "none":
        out = rgb.convert("RGBA")
    elif mode == "alpha":
        out = rgb.convert("RGBA")             # the sheet is an RGBA image: its own alpha channel is the piece's
    elif mode == "flood":
        tol = piece.get("tolerance", 30)
        bg = flood_bg(maxch(rgb), tol)
        a = Image.frombytes("L", (w, h), bytes(0 if v else 255 for v in bg))
        keep = piece.get("keep", "largest")
        if keep == "largest":
            merged = largest_component(a.filter(ImageFilter.MaxFilter(5)))
            a = ImageChops.multiply(a, merged)
        for _ in range(piece.get("shrink", 1)):
            a = a.filter(ImageFilter.MinFilter(3))
        soft = piece.get("soften", 0.8)
        if soft:
            a = a.filter(ImageFilter.GaussianBlur(soft))
        out = rgb.convert("RGBA")
        out.putalpha(a)
    elif mode == "luma":
        lo = piece.get("tolerance", 10)
        hi = piece.get("knee", 255)
        mx = maxch(rgb)
        solid = flood_bg(mx, hi - 1)          # edge-connected area below the knee stays translucent
        src = rgb.load()
        mxd = mx.load()
        out = Image.new("RGBA", (w, h))
        op = out.load()
        for y in range(h):
            for x in range(w):
                r, g, b = src[x, y]
                m = mxd[x, y]
                if m >= hi or not solid[y * w + x]:
                    op[x, y] = (r, g, b, 255)
                elif m <= lo:
                    op[x, y] = (0, 0, 0, 0)
                else:
                    a = (m - lo) / float(hi - lo)
                    k = 1.0 / a
                    op[x, y] = (min(255, int(r * k + .5)), min(255, int(g * k + .5)), min(255, int(b * k + .5)),
                                int(a * 255 + .5))
    else:
        raise ValueError("unknown mode " + mode)
    if piece.get("clip"):
        m = shapes_mask((w, h), shift_shapes(piece["clip"], ox, oy))
        out.putalpha(ImageChops.multiply(out.split()[3], m))
    if piece.get("holes"):
        m = shapes_mask((w, h), shift_shapes(piece["holes"], ox, oy))
        out.putalpha(ImageChops.multiply(out.split()[3], ImageChops.invert(m)))
    hf = piece.get("hole_flood")
    if hf:
        warm = None
        if hf.get("warm_only"):
            r, g, b = rgb.convert("RGB").split()
            warm = bytes(1 if bb <= rr else 0 for rr, bb in zip(r.tobytes(), b.tobytes()))
        filled = flood_seeds(maxch(rgb), hf.get("tolerance", 60), [tuple(sd) for sd in hf["seeds"]],
                             hf.get("min", 0), warm)
        if hf.get("symmetric"):
            filled = symmetric_rows(filled, w, h)
        m = Image.frombytes("L", (w, h), bytes(255 if v else 0 for v in filled))
        for _ in range(hf.get("grow", 1)):
            m = m.filter(ImageFilter.MaxFilter(3))
        if hf.get("soften", 0.7):
            m = m.filter(ImageFilter.GaussianBlur(hf.get("soften", 0.7)))
        out.putalpha(ImageChops.multiply(out.split()[3], ImageChops.invert(m)))
    if piece.get("feather"):
        out.putalpha(ImageChops.multiply(out.split()[3], feather_mask((w, h), piece["feather"])))
    return out


def nine_slice(im, margins, size):
    l, t, r, b = margins
    w, h = im.size
    W, H = size
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    xs = [(0, l, 0, l), (l, w - r, l, W - r), (w - r, w, W - r, W)]
    ys = [(0, t, 0, t), (t, h - b, t, H - b), (h - b, h, H - b, H)]
    for sx0, sx1, dx0, dx1 in xs:
        for sy0, sy1, dy0, dy1 in ys:
            part = im.crop((sx0, sy0, sx1, sy1))
            dw, dh = dx1 - dx0, dy1 - dy0
            if part.size != (dw, dh):
                part = resize_rgba(part, (dw, dh))
            out.paste(part, (dx0, dy0))
    return out


def derive(piece, base):
    d = piece["derive"]
    rgb = base.convert("RGB")
    a = base.split()[3]
    if "channels" in d:
        src = rgb.split()
        rgb = Image.merge("RGB", tuple(src["rgb".index(c)] for c in d["channels"]))
    if "saturation" in d:
        rgb = ImageEnhance.Color(rgb).enhance(d["saturation"])
    if "tint" in d:
        tint = Image.new("RGB", rgb.size, tuple(d["tint"]))
        rgb = Image.blend(rgb, ImageChops.multiply(rgb, tint), d.get("tint_amount", 1.0)) \
            if d.get("multiply", True) else Image.blend(rgb, tint, d.get("tint_amount", 0.5))
    if "brightness" in d:
        rgb = ImageEnhance.Brightness(rgb).enhance(d["brightness"])
    if "contrast" in d:
        rgb = ImageEnhance.Contrast(rgb).enhance(d["contrast"])
    out = rgb.convert("RGBA")
    out.putalpha(a)
    return out


def _sdf_ellipse(x, y, cx, cy, a, b):
    """Approximate signed distance (pixels, positive outside) to the ellipse with centre (cx, cy), semi axes a, b."""
    dx, dy = x - cx, y - cy
    r = math.sqrt((dx / a) ** 2 + (dy / b) ** 2)
    if r < 1e-9:
        return -min(a, b)
    gx, gy = dx / (a * a), dy / (b * b)
    g = math.sqrt(gx * gx + gy * gy) / r
    return (r - 1.0) / g


def _sdf_rect(x, y, x0, y0, x1, y1):
    dx = max(x0 - x, x - x1, 0.0)
    dy = max(y0 - y, y - y1, 0.0)
    if dx > 0 or dy > 0:
        return math.sqrt(dx * dx + dy * dy)
    return -min(x - x0, x1 - x, y - y0, y1 - y)


def _gothic_outline(x0, y0, x1, y1, dome, rnd, n=160):
    """Outline (polyline) of a pointed arch: straight sides below y0 + dome, a pointed top above it. The half width at
    the height u below the apex is (w / 2) * (1 - t^2) ^ (1 / (2 * rnd)), t = (dome - u) / dome (rnd 1 = ellipse,
    smaller = more pointed; the same curve as the addon's MU.LayoutArch)."""
    cx = (x0 + x1) / 2.0
    half_w = (x1 - x0) / 2.0
    left = []
    for i in range(n + 1):
        u = dome * i / n
        t = (dome - u) / dome
        k = max(0.0, 1.0 - t * t)
        left.append((cx - half_w * k ** (1.0 / (2.0 * rnd)), y0 + u))
    pts = [(x0, y1)] + list(reversed(left)) + [(2 * cx - x, y) for x, y in left] + [(x1, y1)]
    return pts, cx, half_w


def _sdf_gothic(x, y, outline, y0, dome, rnd, cx, half_w, y1):
    best = 1e18
    for i in range(len(outline) - 1):
        ax, ay = outline[i]
        bx, by = outline[i + 1]
        vx, vy = bx - ax, by - ay
        ll = vx * vx + vy * vy
        t = 0.0 if ll == 0 else max(0.0, min(1.0, ((x - ax) * vx + (y - ay) * vy) / ll))
        dx, dy = x - (ax + t * vx), y - (ay + t * vy)
        dd = dx * dx + dy * dy
        if dd < best:
            best = dd
    # the closing edge along the bottom
    ax, ay = outline[-1]
    bx, by = outline[0]
    t = max(0.0, min(1.0, (x - ax) / (bx - ax)))
    dx, dy = x - (ax + t * (bx - ax)), y - ay
    best = min(best, dx * dx + dy * dy)
    d = math.sqrt(best)
    if y < y0 or y > y1:
        inside = False
    elif y >= y0 + dome:
        inside = abs(x - cx) <= half_w
    else:
        t = (dome - (y - y0)) / dome
        k = max(0.0, 1.0 - t * t)
        inside = abs(x - cx) <= half_w * k ** (1.0 / (2.0 * rnd))
    return -d if inside else d


def procedural_glow(piece):
    """A white soft glow with alpha around an ellipse or an arch (rectangle with an elliptic top)."""
    spec = piece["procedural"]
    w, h = piece["size"]
    l, t, r, b = spec["inner"]
    x0, y0, x1, y1 = l * w, t * h, r * w, b * h
    sigma = float(spec.get("sigma", 5.0))
    inside = float(spec.get("inside", 3.0))
    peak = float(spec.get("peak", 0.9))
    shape = spec["shape"]
    cx = (x0 + x1) / 2.0
    cy = (y0 + y1) / 2.0
    dome = float(spec.get("dome", 0.4)) * (y1 - y0)
    rnd = float(spec.get("round", 0.7))
    outline = None
    if shape == "gothic":
        outline, gcx, ghalf = _gothic_outline(x0, y0, x1, y1, dome, rnd)
    im = Image.new("RGBA", (w, h), (255, 255, 255, 0))
    px = im.load()
    ss = 2                                       # 2x2 samples per texel keep the thin inner edge smooth
    for yy in range(h):
        for xx in range(w):
            acc = 0.0
            for sy in range(ss):
                for sx in range(ss):
                    x = xx + (sx + 0.5) / ss
                    y = yy + (sy + 0.5) / ss
                    if shape == "ellipse":
                        d = _sdf_ellipse(x, y, cx, cy, (x1 - x0) / 2.0, (y1 - y0) / 2.0)
                    elif shape == "arch":
                        d = min(_sdf_rect(x, y, x0, y0 + dome, x1, y1),
                                _sdf_ellipse(x, y, cx, y0 + dome, (x1 - x0) / 2.0, dome))
                    elif shape == "gothic":
                        d = _sdf_gothic(x, y, outline, y0, dome, rnd, gcx, ghalf, y1)
                    else:
                        raise ValueError("unknown glow shape " + shape)
                    if d >= 0:
                        a = peak * math.exp(-(d / sigma) ** 2)
                    else:
                        a = peak * math.exp(-(d / inside) ** 2)
                    acc += a
            px[xx, yy] = (255, 255, 255, int(round(255 * acc / (ss * ss))))
    return im


def procedural_bubble(piece):
    """A speech bubble in full colour (the addon does not tint it): a light body with a dark rim and a tail at the
    bottom left. Drawn 4x and reduced, so the edges are smooth. The spec's coordinates are texels of the canvas."""
    spec = piece["procedural"]
    w, h = piece["size"]
    S = 4
    l, t, r, b = [v * S for v in spec["body"]]
    rad = spec["radius"] * S
    rim = int(spec["rim"]) * S
    outer = Image.new("L", (w * S, h * S), 0)
    d = ImageDraw.Draw(outer)
    d.rounded_rectangle((l, t, r, b), radius=rad, fill=255)
    d.polygon([(x * S, y * S) for x, y in spec["tail"]], fill=255)
    inner = outer.filter(ImageFilter.MinFilter(2 * rim + 1))     # the rim is what the erosion takes off
    body = Image.composite(Image.new("RGBA", outer.size, tuple(spec["fill"]) + (255,)),
                           Image.new("RGBA", outer.size, tuple(spec["rim_color"]) + (255,)), inner)
    body.putalpha(outer)
    return body.resize((w, h), Image.LANCZOS)


def _fill_from_corner(img, value):
    """Sets to value every 255 pixel 4-connected to the top left corner (an iterative flood fill; this Pillow has none)."""
    w, h = img.size
    px = img.load()
    if px[0, 0] != 255:
        return
    px[0, 0] = value
    stack = [(0, 0)]
    while stack:
        x, y = stack.pop()
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < w and 0 <= ny < h and px[nx, ny] == 255:
                px[nx, ny] = value
                stack.append((nx, ny))


def silhouette_glow(base, spec):
    """A white soft glow that follows the outline of an earlier piece (its solid silhouette: holes like the taunt
    opening count as solid). Canvas = the piece plus 'margin' pixels on every side; the piece's rectangle is the
    "inner" anchor. Outside: a fall-off of 'sigma' pixels; inside: a faint band 'inside' pixels deep."""
    sigma = float(spec.get("sigma", 6.0))
    inside = int(round(float(spec.get("inside", 3))))
    peak = float(spec.get("peak", 0.85))
    m = int(spec.get("margin", 16))
    W, H = base.width + 2 * m, base.height + 2 * m
    # transparent pixels: 255; the ones reached from the canvas edge become 128 (outside); the rest is solid
    free = Image.new("L", (W, H), 255)
    free.paste(base.getchannel("A").point(lambda v: 0 if v >= 128 else 255), (m, m))
    _fill_from_corner(free, 128)
    solid = free.point(lambda v: 0 if v == 128 else 255)
    blur = solid.filter(ImageFilter.GaussianBlur(sigma)).point(lambda v: min(255, 2 * v))
    outer = ImageChops.multiply(blur, ImageChops.invert(solid))
    if inside > 0:
        eroded = solid.filter(ImageFilter.MinFilter(2 * inside + 1))
        band = ImageChops.subtract(solid, eroded)
        inner = band.filter(ImageFilter.GaussianBlur(max(0.5, sigma / 2.0))).point(lambda v: min(255, 2 * v))
    else:
        inner = Image.new("L", (W, H), 0)
    alpha = ImageChops.lighter(outer, inner).point(lambda v: int(round(v * peak)))
    im = Image.new("RGBA", (W, H), (255, 255, 255, 0))
    im.putalpha(alpha)
    return im, m


def add_halo(im, anchors, spec):
    """The piece on a canvas 'margin' pixels larger on every side, over a soft glow of its own alpha (blurred by
    'sigma', times 'peak') in its own mean colour (or 'color' [r, g, b]). The anchors move with the piece, and the
    piece's rectangle becomes the "inner" anchor."""
    m = int(spec.get("margin", 12))
    sigma = float(spec.get("sigma", 5.0))
    peak = float(spec.get("peak", 0.8))
    color = spec.get("color")
    if color is None:
        a = im.getchannel("A")
        n = sum(a.histogram()[128:])
        rgb = im.convert("RGB")
        mask = a.point(lambda v: 255 if v >= 128 else 0)
        color = [int(round(sum(i * c for i, c in enumerate(rgb.getchannel(ch).histogram(mask))) / max(1, n)))
                 for ch in range(3)]
    W, H = im.width + 2 * m, im.height + 2 * m
    alpha = Image.new("L", (W, H), 0)
    alpha.paste(im.getchannel("A"), (m, m))
    alpha = alpha.filter(ImageFilter.GaussianBlur(sigma)).point(lambda v: int(round(min(255, 2 * v) * peak)))
    out = Image.new("RGBA", (W, H), tuple(color) + (0,))
    out.putalpha(alpha)
    out.alpha_composite(im, (m, m))
    moved = {}
    for k, v in anchors.items():
        moved[k] = {"left": (m + v["left"] * im.width) / W, "top": (m + v["top"] * im.height) / H,
                    "right": (m + v["right"] * im.width) / W, "bottom": (m + v["bottom"] * im.height) / H}
    moved["inner"] = {"left": m / float(W), "top": m / float(H),
                      "right": (m + im.width) / float(W), "bottom": (m + im.height) / float(H)}
    return out, moved


def process(piece, sheets, done):
    """-> (RGBA image of the piece, anchors in fractions of that image)"""
    name = piece["name"]
    anchors_in = piece.get("anchors", {})
    if "silhouette" in piece:
        base, _ = done[piece["silhouette"]["from"]]
        im, m = silhouette_glow(base, piece["silhouette"])
        W, H = im.size
        return im, {"inner": {"left": m / float(W), "top": m / float(H),
                              "right": (m + base.width) / float(W), "bottom": (m + base.height) / float(H)}}
    if "procedural" in piece:
        spec = piece["procedural"]
        if spec["kind"] == "bubble":
            return procedural_bubble(piece), {k: dict(v) for k, v in piece.get("anchors_out", {}).items()}
        im = procedural_glow(piece)
        anchors = {k: dict(v) for k, v in piece.get("anchors_out", {}).items()}
        anchors.setdefault("inner", {"left": spec["inner"][0], "top": spec["inner"][1], "right": spec["inner"][2],
                                      "bottom": spec["inner"][3]})
        return im, anchors
    if "derive" in piece:
        base, base_anchors = done[piece["derive"]["from"]]
        im = derive(piece, base)
        anchors = dict(base_anchors)
        anchors.update({k: dict(v) for k, v in anchors_in.items()})
        crop_w, crop_h, off = im.width, im.height, (0, 0)
    else:
        rgb = cut(piece, sheets)
        im = make_alpha(piece, rgb)
        crop_w, crop_h = im.size
        off = (0, 0)
        if piece.get("trim"):
            bb = alpha_bbox(im)
            pad = piece.get("trim_pad", 1)
            bb = (max(0, bb[0] - pad), max(0, bb[1] - pad), min(crop_w, bb[2] + pad), min(crop_h, bb[3] + pad))
            im = im.crop(bb)
            off = (bb[0], bb[1])
        anchors = {}
        for k, v in anchors_in.items():
            anchors[k] = {
                "left": (v["left"] * crop_w - off[0]) / im.width,
                "top": (v["top"] * crop_h - off[1]) / im.height,
                "right": (v["right"] * crop_w - off[0]) / im.width,
                "bottom": (v["bottom"] * crop_h - off[1]) / im.height,
            }
    size = piece.get("size")
    if piece.get("slice9"):
        im = nine_slice(im, piece["slice9"], tuple(size))
    elif size:
        tw, th = size
        if piece.get("stretch"):
            new = (tw, th)
        else:
            s = min(tw / float(im.width), th / float(im.height))
            new = (max(1, int(round(im.width * s))), max(1, int(round(im.height * s))))
        if new != im.size:
            im = resize_rgba(im, new)
    if piece.get("halo"):
        im, anchors = add_halo(im, anchors, piece["halo"])
    for k, v in piece.get("anchors_out", {}).items():
        anchors[k] = dict(v)
    # keep texel colours under the transparent edge sane
    return bleed_color(im), anchors


def compose_texture(im, piece):
    w, h = im.size
    tex = piece.get("tex") or [pow2(w), pow2(h)]
    tw, th = tex
    if tw > MAX_TEX or th > MAX_TEX:
        raise ValueError("%s: texture %dx%d exceeds %d" % (piece["name"], tw, th, MAX_TEX))
    if w > tw or h > th:
        raise ValueError("%s: image %dx%d does not fit %dx%d" % (piece["name"], w, h, tw, th))
    canvas = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    canvas.paste(im, (0, 0))
    return canvas


def preview(im, path, scale=1):
    """Piece over a mid-grey checkerboard (so the alpha is visible)."""
    w, h = im.size
    bg = Image.new("RGB", (w, h), (70, 70, 70))
    cell = 8
    dr = ImageDraw.Draw(bg)
    for y in range(0, h, cell):
        for x in range(0, w, cell):
            if ((x // cell) + (y // cell)) % 2:
                dr.rectangle([x, y, x + cell - 1, y + cell - 1], fill=(100, 100, 100))
    bg.paste(im, (0, 0), im.split()[3])
    if scale != 1:
        bg = bg.resize((int(w * scale), int(h * scale)), Image.NEAREST)
    bg.save(path)
    return bg


def contact_sheet(items, path):
    """items: list of (name, preview RGB image); simple grid, cells scaled down to fit 200x200."""
    cell = 210
    cols = 6
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell, rows * (cell + 14)), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for i, (name, im) in enumerate(items):
        t = im.copy()
        t.thumbnail((cell - 10, cell - 10), Image.LANCZOS)
        cx, cy = (i % cols) * cell, (i // cols) * (cell + 14)
        sheet.paste(t, (cx + 5, cy + 2))
        dr.text((cx + 5, cy + cell - 2), name, fill=(230, 230, 230))
    sheet.save(path)


# ---------------------------------------------------------------- driver

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", nargs="*", help="process only these piece names (derived pieces pull their base)")
    ap.add_argument("--no-blp", action="store_true", help="previews and manifest only")
    ap.add_argument("--verify", action="store_true", help="decode each BLP with Pillow and print the error")
    ap.add_argument("--dump", nargs="+", metavar="PIECE", help="only write the raw crops of these pieces (4x, grid)")
    args = ap.parse_args()

    with open(PIECES, encoding="utf-8") as f:
        spec = json.load(f)
    sheets = {}
    for key, fname in spec["sheets"].items():
        # a sheet is a file name (RGB, on black) or {"file": name, "alpha": true} (RGBA, keeps its own alpha)
        keep_alpha = isinstance(fname, dict) and fname.get("alpha")
        fname = fname["file"] if isinstance(fname, dict) else fname
        p = os.path.join(SHEET_DIR, fname)
        if not os.path.exists(p):
            sys.exit("sheet not found: %s (build/assets is gitignored: copy the owner's sheets there)" % p)
        sheets[key] = Image.open(p).convert("RGBA" if keep_alpha else "RGB")

    if args.dump:
        os.makedirs(REVIEW, exist_ok=True)
        by_name = {p["name"]: p for p in spec["pieces"]}
        for n in args.dump:
            piece = by_name.get(n)
            if not piece or "rect" not in piece:
                sys.exit("unknown piece (or a derived one) " + n)
            im = cut(piece, sheets).resize((piece["rect"][2] * 4, piece["rect"][3] * 4), Image.NEAREST)
            dr = ImageDraw.Draw(im)
            for gx in range(0, im.width, 40):
                dr.line([(gx, 0), (gx, im.height)], fill=(0, 255, 255) if gx % 200 == 0 else (0, 120, 120))
            for gy in range(0, im.height, 40):
                dr.line([(0, gy), (im.width, gy)], fill=(0, 255, 255) if gy % 200 == 0 else (0, 120, 120))
            out = os.path.join(REVIEW, n + "_raw.png")
            im.save(out)
            print("%s: crop %r -> %s (4x; grid lines every 10 crop pixels, bright every 50)" % (n, piece["rect"], out))
        return

    wanted = None
    if args.only:
        by_name = {p["name"]: p for p in spec["pieces"]}
        wanted = set()
        todo = list(args.only)
        while todo:
            n = todo.pop()
            if n not in by_name:
                sys.exit("unknown piece " + n)
            wanted.add(n)
            if "derive" in by_name[n]:
                todo.append(by_name[n]["derive"]["from"])
            if "silhouette" in by_name[n]:
                todo.append(by_name[n]["silhouette"]["from"])

    os.makedirs(OUT, exist_ok=True)
    os.makedirs(REVIEW, exist_ok=True)
    prev = {}
    if os.path.exists(MANIFEST):
        with open(MANIFEST, encoding="utf-8") as f:
            prev = json.load(f).get("pieces", {})
    entries = dict(prev)
    done = {}
    contact = []
    for piece in spec["pieces"]:
        name = piece["name"]
        if wanted is not None and name not in wanted:
            continue
        im, anchors = process(piece, sheets, done)
        done[name] = (im, anchors)
        tex = compose_texture(im, piece)
        w, h = im.size
        entry = {
            "w": w,
            "h": h,
            "texture_size": [tex.width, tex.height],
            "right": w / float(tex.width),
            "bottom": h / float(tex.height),
            "mode": "procedural" if "procedural" in piece else ("silhouette" if "silhouette" in piece else (
                piece.get("mode", "flood") if "derive" not in piece else "derive")),
            "source": {"procedural": piece["procedural"]["kind"]} if "procedural" in piece else (
                {"silhouette_of": piece["silhouette"]["from"]} if "silhouette" in piece else (
                {"sheet": (lambda v: v["file"] if isinstance(v, dict) else v)(spec["sheets"].get(piece.get("sheet", ""), None)),
                 "rect": piece.get("rect")}
                if "derive" not in piece else {"derived_from": piece["derive"]["from"]})),
        }
        if piece.get("stretch") and piece.get("rect"):
            entry["aspect"] = round(piece["rect"][2] / float(piece["rect"][3]), 5)   # native w/h of a stretched piece
        if anchors:
            entry["anchors"] = {k: {kk: round(vv, 5) for kk, vv in v.items()} for k, v in sorted(anchors.items())}
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
        contact.append((name, preview(im, os.path.join(REVIEW, name + ".png"))))
        line = "%-22s %4dx%-4d tex %3dx%-3d right=%.5f bottom=%.5f  %s" % (
            name, w, h, tex.width, tex.height, entry["right"], entry["bottom"], entry["mode"])
        if args.verify and not args.no_blp:
            dec = Image.open(os.path.join(OUT, name + ".blp")).convert("RGBA")
            ref = tex
            rp, dp = ref.load(), dec.load()
            err = n = 0
            for y in range(ref.height):
                for x in range(ref.width):
                    r, d = rp[x, y], dp[x, y]
                    if r[3] > 0:
                        err += abs(r[0] - d[0]) + abs(r[1] - d[1]) + abs(r[2] - d[2])
                        n += 3
            line += "  mean|dRGB|=%.2f" % (err / max(1, n))
        print(line)
    # entries not in the spec any more are dropped from the manifest
    names = set(p["name"] for p in spec["pieces"])
    entries = {k: v for k, v in entries.items() if k in names}
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        json.dump({
            "schema": 1,
            "note": "w/h = used pixels of the texture, right = w / texture width, bottom = h / texture height "
                    "(SetTexCoord(0, right, 0, bottom)); anchors are fractions of the used image. "
                    "Written by tools/art/slice_board.py, read by tools/deploy/deploy-addon.ps1.",
            "pieces": entries,
        }, f, indent=2, sort_keys=True)
        f.write("\n")
    if wanted is None and contact:
        contact_sheet(contact, os.path.join(REVIEW, "_contact.png"))
    print("%d pieces -> %s" % (len(contact), OUT))


if __name__ == "__main__":
    main()
