"""Banner title editing (hi-res, Pillow only).

Letters are cut out of other cards' titles, un-rotated with the banner baseline curve of that card
type, re-set with the median letter spacing and rotated along the curve at the new position.
"""
import math
import re
import statistics

from PIL import Image, ImageChops, ImageFilter

import artlib as A
import cardimg as C

HI = C.HI
NAME_ROI = (90, 352, 432, 446)  # hi-res search box for banner titles
NAME_OUTLINE = 3  # outline thickness (hi-res px) of the title font
DESCENDERS = set("gjpqy")
OUTLINE_RGB = (22, 18, 16)


def fill_clusters(im, roi=NAME_ROI, thr=232, gap=3, minarea=3):
    """White letter fills of the title; fragments of one letter merged by x proximity.
    Returns [{'bbox':[x0,y0,x1,y1] inclusive, 'pix': set}] sorted by x."""
    px = im.load()
    x0, y0, x1, y1 = roi
    W, H = x1 - x0, y1 - y0
    m = bytearray(W * H)
    for j in range(H):
        for i in range(W):
            p = px[x0 + i, y0 + j]
            if p[3] > 250 and p[0] >= thr and p[1] >= thr and p[2] >= thr:
                m[j * W + i] = 1
    lab = bytearray(W * H)
    frags = []
    for s in range(W * H):
        if m[s] and not lab[s]:
            st = [s]
            lab[s] = 1
            pts = []
            while st:
                q = st.pop()
                pts.append(q)
                qx, qy = q % W, q // W
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        xx, yy = qx + dx, qy + dy
                        if 0 <= xx < W and 0 <= yy < H:
                            r = yy * W + xx
                            if m[r] and not lab[r]:
                                lab[r] = 1
                                st.append(r)
            if len(pts) >= minarea:
                xs = [p % W + x0 for p in pts]
                ys = [p // W + y0 for p in pts]
                frags.append({"bbox": [min(xs), min(ys), max(xs), max(ys)],
                              "pix": {(p % W + x0, p // W + y0) for p in pts}})
    frags.sort(key=lambda f: f["bbox"][0])
    out = []
    for f in frags:
        if out and f["bbox"][0] <= out[-1]["bbox"][2] + gap:
            o = out[-1]
            b = o["bbox"]
            b[0] = min(b[0], f["bbox"][0])
            b[1] = min(b[1], f["bbox"][1])
            b[2] = max(b[2], f["bbox"][2])
            b[3] = max(b[3], f["bbox"][3])
            o["pix"] |= f["pix"]
        else:
            out.append(f)
    return out


class Curve:
    """Baseline y(x) of the title (hi-res) for one card type: polynomial in (x-200)."""

    def __init__(self, coefs, center_x, letter_gap=5.0, space_gap=14.0):
        self.c = list(coefs)
        self.center_x = center_x
        self.letter_gap = letter_gap
        self.space_gap = space_gap

    def y(self, x):
        d = x - 200.0
        return sum(c * d ** i for i, c in enumerate(self.c))

    def slope(self, x):
        d = x - 200.0
        return sum(i * c * d ** (i - 1) for i, c in enumerate(self.c) if i > 0)

    def ccw(self, x):
        """Counter-clockwise tilt in degrees (baseline rising to the right is positive)."""
        return math.degrees(math.atan(-self.slope(x)))

    def to_json(self):
        return {"coefs": self.c, "center_x": self.center_x, "letter_gap": self.letter_gap,
                "space_gap": self.space_gap}

    @staticmethod
    def from_json(d):
        return Curve(d["coefs"], d["center_x"], d["letter_gap"], d["space_gap"])


def _polyfit(xs, ys, deg):
    n = deg + 1
    Am = [[sum(x ** (i + j) for x in xs) for j in range(n)] for i in range(n)]
    b = [sum(y * x ** i for x, y in zip(xs, ys)) for i in range(n)]
    for i in range(n):
        p = max(range(i, n), key=lambda r: abs(Am[r][i]))
        Am[i], Am[p] = Am[p], Am[i]
        b[i], b[p] = b[p], b[i]
        for r in range(i + 1, n):
            f = Am[r][i] / Am[i][i]
            for c in range(i, n):
                Am[r][c] -= f * Am[i][c]
            b[r] -= f * b[i]
    co = [0.0] * n
    for i in reversed(range(n)):
        co[i] = (b[i] - sum(Am[i][j] * co[j] for j in range(i + 1, n))) / Am[i][i]
    return co


def aligned_title(cid, name, roi=NAME_ROI):
    """(clusters, letters) if the fill clusters match the letters of `name` one to one."""
    if not re.fullmatch(r"[A-Za-z ]+", name):
        return None
    letters = [ch for ch in name if ch != " "]
    cl = fill_clusters(C.load_hi(cid), roi)
    if len(cl) != len(letters):
        return None
    return cl, letters


def fit_curve(entries, deg):
    """entries: [(card id, name)] of one card type. Fits the baseline on the bottoms of the
    non-descender letters of all titles that align, robustly."""
    samples = []
    centers = []
    lgaps = []
    sgaps = []
    used = 0
    for cid, name in entries:
        at = aligned_title(cid, name)
        if at is None:
            continue
        used += 1
        cl, letters = at
        centers.append((cl[0]["bbox"][0] + cl[-1]["bbox"][2]) / 2.0)
        for c, ch in zip(cl, letters):
            if ch not in DESCENDERS:
                samples.append(((c["bbox"][0] + c["bbox"][2]) / 2.0 - 200.0, c["bbox"][3]))
        # gaps: map letters to positions in the name to tell letter gaps from word gaps
        li = 0
        seq = []
        for ch in name:
            if ch == " ":
                seq.append(None)
            else:
                seq.append(li)
                li += 1
        for a, b in zip(seq, seq[1:]):
            pass
        li = 0
        prev = None
        space_pending = False
        for ch in name:
            if ch == " ":
                space_pending = True
                continue
            cur = cl[li]
            if prev is not None:
                g = cur["bbox"][0] - prev["bbox"][2]
                (sgaps if space_pending else lgaps).append(g)
            prev = cur
            space_pending = False
            li += 1
    xs = [x for x, _ in samples]
    ys = [y for _, y in samples]
    keep = list(range(len(xs)))
    sd = 0.0
    co = None
    for _ in range(4):
        co = _polyfit([xs[i] for i in keep], [ys[i] for i in keep], deg)
        res = [ys[i] - sum(c * xs[i] ** j for j, c in enumerate(co)) for i in range(len(xs))]
        sd = statistics.pstdev([res[i] for i in keep])
        keep = [i for i in range(len(xs)) if abs(res[i]) < 2.5 * sd + 0.1]
    cur = Curve(co, statistics.median(centers), statistics.median(lgaps), statistics.median(sgaps) if sgaps else 14.0)
    return cur, {"titles": used, "samples": len(xs), "kept": len(keep), "sd": round(sd, 3)}


class Glyph:
    pass


def _premult(im):
    r, g, b, a = im.split()
    return Image.merge("RGBA", (ImageChops.multiply(r, a), ImageChops.multiply(g, a),
                                ImageChops.multiply(b, a), a))


def _unpremult(im):
    px = im.load()
    out = Image.new("RGBA", im.size)
    op = out.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a == 0:
                continue
            k = 255.0 / a
            op[x, y] = (min(255, int(r * k + 0.5)), min(255, int(g * k + 0.5)),
                        min(255, int(b * k + 0.5)), a)
    return out


def _rotate(patch, fillmask, angle, pivot, translate=(0.0, 0.0)):
    """Rotate RGBA patch and its fill mask counter-clockwise about pivot (premultiplied resampling)."""
    rot = _premult(patch).rotate(angle, resample=Image.BICUBIC, center=pivot, translate=translate)
    fm = fillmask.rotate(angle, resample=Image.BILINEAR, center=pivot, translate=translate)
    bb = fm.point(lambda v: 255 if v >= 128 else 0).getbbox()
    return _unpremult(rot), bb


def harvest_glyphs(cid, name, curve):
    """Upright glyphs (letter fill plus outline, RGBA) of a title, or [] if it does not align."""
    at = aligned_title(cid, name)
    if at is None:
        return []
    clusters, letters = at
    im = C.load_hi(cid)
    px = im.load()
    all_fill = set()
    for c in clusters:
        all_fill |= c["pix"]
    out = []
    for c, ch in zip(clusters, letters):
        x0, y0, x1, y1 = c["bbox"]
        pad = NAME_OUTLINE + 9
        X0, Y0, X1, Y1 = x0 - pad, y0 - pad, x1 + pad + 1, y1 + pad + 1
        w, h = X1 - X0, Y1 - Y0
        fm = Image.new("L", (w, h), 0)
        fp = fm.load()
        for (x, y) in c["pix"]:
            fp[x - X0, y - Y0] = 255
        closed = fm.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))
        forb = Image.new("L", (w, h), 0)
        bp = forb.load()
        for (x, y) in all_fill - c["pix"]:
            if X0 <= x < X1 and Y0 <= y < Y1:
                bp[x - X0, y - Y0] = 255
        forb = forb.filter(ImageFilter.MaxFilter(5))
        near = closed.filter(ImageFilter.MaxFilter(2 * (NAME_OUTLINE + 2) + 1))
        cp, npx, fbp = closed.load(), near.load(), forb.load()
        ring = []
        for y in range(h):
            for x in range(w):
                if npx[x, y] < 128 and cp[x, y] < 128 and fbp[x, y] < 128:
                    ring.append(C.lum(px[X0 + x, Y0 + y]))
        bgL = statistics.median(ring) if ring else 170.0
        patch = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        pp = patch.load()
        for y in range(h):
            for x in range(w):
                if fbp[x, y] > 128 and cp[x, y] < 128:
                    continue
                p = px[X0 + x, Y0 + y]
                if cp[x, y] >= 128:
                    pp[x, y] = (p[0], p[1], p[2], 255)
                elif npx[x, y] >= 128:
                    a = C.clamp01((bgL - C.lum(p)) / (bgL - 30.0))
                    if a > 0.05:
                        pp[x, y] = OUTLINE_RGB + (int(a * 255 + 0.5),)
        gx = (x0 + x1) / 2.0
        pivot = (gx - X0 + 0.5, curve.y(gx) - Y0 + 0.5)
        G = Glyph()
        G.ch, G.src = ch, cid
        G.img, G.fill = _rotate(patch, closed, -curve.ccw(gx), pivot)
        G.pivot = pivot
        G.fill_h = G.fill[3] - G.fill[1]
        G.area = sum(1 for v in closed.getdata() if v >= 128)
        out.append(G)
    return out


def _erase_title(im, clusters, roi=NAME_ROI):
    """Remove the old title: dilate fills + outline, then interpolate the banner vertically."""
    x0, y0, x1, y1 = roi
    w, h = x1 - x0, y1 - y0
    px = im.load()
    fill = Image.new("L", (w, h), 0)
    fp = fill.load()
    for c in clusters:
        for (x, y) in c["pix"]:
            fp[x - x0, y - y0] = 255
    near = fill.filter(ImageFilter.MaxFilter(2 * (NAME_OUTLINE + 4) + 1))
    npx = near.load()
    ink = Image.new("L", (w, h), 0)
    ip = ink.load()
    for y in range(h):
        for x in range(w):
            if npx[x, y] > 128:
                p = px[x0 + x, y0 + y]
                if C.lum(p) < 105 or min(p[:3]) >= 205:
                    ip[x, y] = 255
    mask = ink.filter(ImageFilter.MaxFilter(5))
    mp = mask.load()
    for x in range(w):
        y = 0
        while y < h:
            if mp[x, y] > 128:
                ys = y
                while y < h and mp[x, y] > 128:
                    y += 1
                ye = y
                a = ys - 1
                b = ye
                if a < 0 or b >= h:
                    continue
                ca = [px[x0 + x, y0 + a - k][:3] for k in range(2) if a - k >= 0]
                cb = [px[x0 + x, y0 + b + k][:3] for k in range(2) if b + k < h]
                A_ = tuple(sum(c[i] for c in ca) / len(ca) for i in range(3))
                B_ = tuple(sum(c[i] for c in cb) / len(cb) for i in range(3))
                for yy in range(ys, ye):
                    t = (yy - a) / float(b - a)
                    px[x0 + x, y0 + yy] = (int(A_[0] * (1 - t) + B_[0] * t + 0.5),
                                           int(A_[1] * (1 - t) + B_[1] * t + 0.5),
                                           int(A_[2] * (1 - t) + B_[2] * t + 0.5), 255)
            else:
                y += 1


def render_title(cid, new_name, curves, donors, cards, pick=None):
    """Return (edited hi-res image, box) with the banner title of `cid` replaced by `new_name`.
    donors: card ids whose titles supply letters; cards: id -> dataset card (name, type);
    curves: card type -> Curve (the type of `cid` selects the target curve)."""
    im = C.load_hi(cid)
    curve = curves[cards[cid]["type"]]
    bank = {}
    for d in donors:
        dc = cards[d]
        for g in harvest_glyphs(d, dc["name"], curves[dc["type"]]):
            bank.setdefault(g.ch, []).append(g)
    old = [c for c in fill_clusters(im) if abs(c["bbox"][3] - curve.y((c["bbox"][0] + c["bbox"][2]) / 2)) < 40]
    _erase_title(im, old)
    seq = []
    used = {}
    for ch in new_name:
        if ch == " ":
            seq.append(None)
            continue
        cand = bank.get(ch)
        if not cand:
            raise RuntimeError("no glyph for %r; add a donor title containing it" % ch)
        hs = sorted(cand, key=lambda g: (g.area, g.src))
        chosen = hs[len(hs) // 2]
        if pick and ch in pick:
            chosen = [g for g in hs if g.src == pick[ch]][0]
        used.setdefault(ch, chosen.src)
        seq.append(chosen)
    # layout on a straight baseline: fill boxes separated by the median gaps
    xs = []
    x = 0.0
    prev = False
    for g in seq:
        if g is None:
            x += curve.space_gap - curve.letter_gap
            continue
        fw = g.fill[2] - g.fill[0]
        xs.append((g, x, x + fw))
        x += fw + curve.letter_gap
    total = x - curve.letter_gap
    x_start = curve.center_x - total / 2.0
    for g, a, b in xs:
        # pivot column of the glyph inside its fill box
        px_left = x_start + a
        gx = px_left + (g.pivot[0] - g.fill[0])  # x where the pivot lands
        gy = curve.y(gx)
        ang = curve.ccw(gx)
        # rotate upright glyph to the new tilt and translate so that the pivot lands on (gx, gy)
        big = Image.new("RGBA", (g.img.width + 40, g.img.height + 40), (0, 0, 0, 0))
        big.paste(g.img, (20, 20))
        pv = (g.pivot[0] + 20, g.pivot[1] + 20)
        ix, iy = int(math.floor(gx)), int(math.floor(gy))
        fx, fy = gx - ix, gy - iy
        rot = _premult(big).rotate(ang, resample=Image.BICUBIC, center=pv, translate=(fx, fy))
        rot = _unpremult(rot)
        # pivot now sits at pv + frac; paste so that pv maps to (ix, iy)
        im.alpha_composite(rot, (ix - int(pv[0]), iy - int(pv[1])))
    box = (NAME_ROI[0], NAME_ROI[1], NAME_ROI[2], NAME_ROI[3])
    return im, box, used
