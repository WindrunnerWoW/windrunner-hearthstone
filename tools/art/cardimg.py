"""Image operations used by patch_renders.py (Pillow only, no numpy).

All text and banner work happens on the 512x776 renders ("hi-res", HI = 2) and is box-downscaled
back to 256x388, because the published 256x renders are exactly the 2x2 box average of the 512x ones.
Coordinates in this module are hi-res pixels unless a name says otherwise.

Three kinds of edits:
  * gem_swap    copy the inside of a stat gem from a donor render that shows the wanted number;
  * text        replace the rules text: old text is removed with a smooth background estimate,
                the new lines are assembled from word images cut out of other renders (the card
                itself first, then donors) so that the typeface and weight stay native;
  * name        replace the banner title: letters are cut out of other cards' banner titles,
                un-rotated with the banner curve, re-set and rotated along the same curve.
"""
import os
import re
import statistics

from PIL import Image, ImageChops, ImageFilter

import artlib as A

HI = 2

# --------------------------------------------------------------------------------------------
# small helpers


def lum(p):
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]


def hi_path(cid):
    return os.path.join(A.CACHE, "512", cid + ".png")


def load_hi(cid):
    return Image.open(hi_path(cid)).convert("RGBA")


def load_lo(cid, folder=None):
    return Image.open(os.path.join(folder or A.CACHE, cid + ".png")).convert("RGBA")


def box_down(im):
    """Hi-res -> native size: 2x2 box average (premultiplied so transparent pixels do not bleed)."""
    r, g, b, a = im.split()
    pm = Image.merge("RGBA", (ImageChops.multiply(r, a), ImageChops.multiply(g, a), ImageChops.multiply(b, a), a))
    small = pm.resize((im.width // HI, im.height // HI), Image.BOX)
    sr, sg, sb, sa = small.split()
    px_a = sa.load()
    out = Image.new("RGBA", small.size)
    op = out.load()
    pr, pg, pb = sr.load(), sg.load(), sb.load()
    for y in range(small.height):
        for x in range(small.width):
            av = px_a[x, y]
            if av == 0:
                op[x, y] = (0, 0, 0, 0)
            else:
                k = 255.0 / av
                op[x, y] = (min(255, int(pr[x, y] * k + 0.5)), min(255, int(pg[x, y] * k + 0.5)),
                            min(255, int(pb[x, y] * k + 0.5)), av)
    return out


def paste_region_lo(lo, hi_edited, box_hi):
    """Replace the region box_hi (hi-res coords, even numbers) of the native image with the
    box-downscaled hi-res edited pixels."""
    x0, y0, x1, y1 = box_hi
    assert x0 % HI == 0 and y0 % HI == 0 and x1 % HI == 0 and y1 % HI == 0
    part = box_down(hi_edited.crop(box_hi))
    lo.paste(part, (x0 // HI, y0 // HI))


def clamp01(v):
    return 0.0 if v < 0 else (1.0 if v > 1 else v)


# --------------------------------------------------------------------------------------------
# gem swap (native resolution, exact pixel copy)


def gem_swap(lo, donor_lo, box, feather=1):
    """Copy the rectangle box=(x0,y0,x1,y1) from donor_lo into lo. The stat gems are identical
    apart from the digit, so a plain copy (with a 1 px soft edge) is seamless."""
    x0, y0, x1, y1 = box
    patch = donor_lo.crop(box)
    mask = Image.new("L", patch.size, 255)
    if feather:
        m = mask.load()
        for y in range(patch.height):
            for x in range(patch.width):
                d = min(x, y, patch.width - 1 - x, patch.height - 1 - y)
                if d < feather + 1:
                    m[x, y] = int(255 * (d + 0.5) / (feather + 1))
    lo.paste(patch, (x0, y0), mask)


# --------------------------------------------------------------------------------------------
# body text


class TextBox:
    """Text analysis of one render: smooth background, ink colour and per-pixel coverage."""

    def __init__(self, im, roi, exclude=()):
        self.im = im
        self.roi = roi
        x0, y0, x1, y1 = roi
        self.W, self.H = x1 - x0, y1 - y0
        crop = im.crop(roi).convert("RGB")
        px = crop.load()
        excl = Image.new("L", crop.size, 0)
        for (a, b, c, d) in exclude:
            for yy in range(max(b, y0) - y0, min(d, y1) - y0):
                for xx in range(max(a, x0) - x0, min(c, x1) - x0):
                    excl.putpixel((xx, yy), 255)
        self.excl = excl
        ex = excl.load()
        lums = sorted(lum(px[x, y]) for y in range(self.H) for x in range(self.W) if not ex[x, y])
        n = len(lums)
        med = lums[n // 2]
        lo_p, hi_p = lums[int(n * 0.012)], lums[min(n - 1, int(n * 0.988))]
        self.dark_text = (med - lo_p) >= (hi_p - med)
        # ink colour: mean of the most extreme pixels
        sel = []
        cut = lo_p if self.dark_text else hi_p
        for y in range(self.H):
            for x in range(self.W):
                if ex[x, y]:
                    continue
                v = lum(px[x, y])
                if (v <= cut) if self.dark_text else (v >= cut):
                    sel.append(px[x, y])
        self.T = tuple(sum(p[i] for p in sel) / len(sel) for i in range(3))
        # background: remove strokes with a rank filter, then smooth
        filt = ImageFilter.MaxFilter(11) if self.dark_text else ImageFilter.MinFilter(11)
        bg = crop.filter(filt).filter(ImageFilter.GaussianBlur(4))
        self.bg = bg
        bp = bg.load()
        # coverage
        self.alpha = [[0.0] * self.W for _ in range(self.H)]
        for y in range(self.H):
            row = self.alpha[y]
            for x in range(self.W):
                if ex[x, y]:
                    continue
                b = bp[x, y]
                p = px[x, y]
                dx = (self.T[0] - b[0], self.T[1] - b[1], self.T[2] - b[2])
                den = dx[0] * dx[0] + dx[1] * dx[1] + dx[2] * dx[2]
                if den < 900:
                    continue
                t = ((p[0] - b[0]) * dx[0] + (p[1] - b[1]) * dx[1] + (p[2] - b[2]) * dx[2]) / den
                t = clamp01(t)
                row[x] = t if t > 0.08 else 0.0
        self._drop_frame()

    def _drop_frame(self):
        """Keep only text-like connected areas of the coverage map: frame edges and watermark
        contours are long, thin and sparse; letters are compact and dense."""
        W, H = self.W, self.H
        seen = bytearray(W * H)
        keep = Image.new("L", (W, H), 0)
        kp = keep.load()
        for sy in range(H):
            for sx in range(W):
                if seen[sy * W + sx] or self.alpha[sy][sx] <= 0.3:
                    continue
                st = [(sx, sy)]
                seen[sy * W + sx] = 1
                pts = []
                while st:
                    x, y = st.pop()
                    pts.append((x, y))
                    for dx in (-1, 0, 1):
                        for dy in (-1, 0, 1):
                            xx, yy = x + dx, y + dy
                            if 0 <= xx < W and 0 <= yy < H and not seen[yy * W + xx] and self.alpha[yy][xx] > 0.3:
                                seen[yy * W + xx] = 1
                                st.append((xx, yy))
                xs = [p[0] for p in pts]
                ys = [p[1] for p in pts]
                bw, bh = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
                ratio = len(pts) / float(bw * bh)
                junk = (min(xs) < 8 or min(ys) < 4 or max(xs) > W - 9 or max(ys) > H - 5) or (len(pts) < 6) or (ratio < 0.18 and (bw > 40 or bh > 40)) or bh > 60
                if not junk:
                    for (x, y) in pts:
                        kp[x, y] = 255
        near = keep.filter(ImageFilter.MaxFilter(7)).load()
        for y in range(H):
            row = self.alpha[y]
            for x in range(W):
                if near[x, y] == 0:
                    row[x] = 0.0

    def mask(self, thr=0.45):
        m = [[1 if v > thr else 0 for v in row] for row in self.alpha]
        # frame edges inside the search box show up as long straight lines: drop them
        for x in range(self.W):
            if sum(m[y][x] for y in range(self.H)) > 0.55 * self.H:
                for y in range(self.H):
                    m[y][x] = 0
        for y in range(self.H):
            if sum(m[y]) > 0.8 * self.W:
                m[y] = [0] * self.W
        return m


def _runs(flags, min_gap=1):
    """[(start, end_exclusive)] of True runs, merging runs separated by < min_gap+1 falses."""
    runs = []
    start = None
    last_true = None
    for i, f in enumerate(flags):
        if f:
            if start is None:
                start = i
            last_true = i
        elif start is not None and i - last_true > min_gap:
            runs.append((start, last_true + 1))
            start = None
    if start is not None:
        runs.append((start, last_true + 1))
    return runs


class Word:
    def __init__(self, token, bold, alpha_img, baseline, src):
        self.token = token
        self.bold = bold
        self.alpha = alpha_img  # 'L' image, cropped to the word
        self.baseline = baseline  # baseline row inside the crop
        self.src = src


def segment(tb, thr=0.35, gap_options=(6, 5, 7, 4, 8)):
    """Lines and words of a TextBox: list of lines, each {'top','bottom','baseline','words':[(x0,x1)]}.
    Returns for each word-gap option the result; caller picks the one with the expected word count."""
    m = tb.mask(thr)
    rowcnt = [sum(r) for r in m]
    line_runs = _runs([c >= 2 for c in rowcnt], min_gap=1)
    lines = []
    for (t, b) in line_runs:
        if b - t < 8 or max(rowcnt[t:b]) < 12:
            continue
        cols = [sum(m[y][x] for y in range(t, b)) > 0 for x in range(tb.W)]
        lines.append((t, b, cols))
    out = {}
    for g in gap_options:
        res = []
        for (t, b, cols) in lines:
            ws = [(a, b) for (a, b) in _runs(cols, min_gap=g - 1)
                  if not ((a < 22 or b > tb.W - 22) and b - a < 24)]  # specks of the frame at the edges
            if not ws:
                continue
            dens = rowcnt[t:b]
            mx = max(dens)
            base = t
            for i, d in enumerate(dens):
                if d >= 0.3 * mx:
                    base = t + i
            res.append({"top": t, "bottom": b, "baseline": base + 1, "words": ws})
        out[g] = res
    return out


_TAG = re.compile(r"(</?b>|</?i>)")


def tokens_from_markup(text):
    """[(token, bold)] from card text with <b>/<i> markup ('$'/'#'/[x] removed, whitespace split)."""
    text = (text or "").replace("[x]", "").replace("$", "").replace("#", "").replace(" ", " ")
    text = text.replace("\n", " ")
    bold = False
    chars = []
    for part in _TAG.split(text):
        if part == "<b>":
            bold = True
        elif part == "</b>":
            bold = False
        elif part in ("<i>", "</i>"):
            pass
        else:
            for ch in part:
                chars.append((ch, bold))
    toks = []
    cur = []
    for ch, bd in chars:
        if ch.isspace():
            if cur:
                toks.append(cur)
                cur = []
        else:
            cur.append((ch, bd))
    if cur:
        toks.append(cur)
    out = []
    for t in toks:
        s = "".join(c for c, _ in t)
        flags = [b for _, b in t]
        # bold if the alphanumeric characters are bold
        core = [b for (c, b) in t if c.isalnum()] or flags
        out.append((s, all(core)))
    return out


def harvest_words(cid, text_markup, roi, exclude, want_tokens=None, verbose=False):
    """Cut the words out of a hi-res render. Returns (list[Word] aligned with tokens, TextBox, lines)
    or (None, None, None) if the segmentation does not match the expected token count."""
    im = load_hi(cid)
    tb = TextBox(im, roi, exclude)
    toks = tokens_from_markup(text_markup)
    for thr in (0.35, 0.45, 0.55, 0.28):
        seg = segment(tb, thr)
        for g, lines in seg.items():
            nwords = sum(len(l["words"]) for l in lines)
            if nwords == len(toks):
                flat = [(a, b) for l in lines for (a, b) in l["words"]]
                if any(not (4.0 <= (b - a - 4) / float(len(tk)) <= 18.5) for (a, b), (tk, _) in zip(flat, toks)):
                    continue  # right count, implausible widths: words were merged/split elsewhere
                words = []
                i = 0
                for l in lines:
                    for (a, b) in l["words"]:
                        a2, b2 = max(0, a - 2), min(tb.W, b + 2)
                        t, bt = l["top"] - 1, l["bottom"] + 1
                        crop = Image.new("L", (b2 - a2, bt - t), 0)
                        cp = crop.load()
                        for y in range(t, bt):
                            if y < 0 or y >= tb.H:
                                continue
                            for x in range(a2, b2):
                                v = tb.alpha[y][x]
                                if v > 0:
                                    cp[x - a2, y - t] = int(v * 255 + 0.5)
                        tok, bold = toks[i]
                        words.append(Word(tok, bold, crop, l["baseline"] - t, cid))
                        i += 1
                return words, tb, seg[g], g
    if verbose:
        print("  %s: token count %d does not match segments %s" % (
            cid, len(toks), {g: sum(len(l["words"]) for l in ls) for g, ls in seg.items()}))
    return None, tb, None, None


def erase_text(im, tb, pad=3):
    """Paint the estimated background over the text of the box (in place on im)."""
    x0, y0, x1, y1 = tb.roi
    m = Image.new("L", (tb.W, tb.H), 0)
    mp = m.load()
    for y in range(tb.H):
        for x in range(tb.W):
            if tb.alpha[y][x] > 0.04:
                mp[x, y] = 255
    m = m.filter(ImageFilter.MaxFilter(2 * pad + 1)).filter(ImageFilter.GaussianBlur(1.2))
    crop = im.crop(tb.roi).convert("RGB")
    out = Image.composite(tb.bg, crop, m)
    im.paste(out.convert("RGBA"), (x0, y0))


def draw_word(im, tb, word, x, y_base):
    """Composite a word image (coverage) in the box's ink colour; x, y_base in absolute hi-res coords."""
    ap = word.alpha.load()
    T = tb.T
    px = im.load()
    top = y_base - word.baseline
    for yy in range(word.alpha.height):
        Y = top + yy
        if Y < 0 or Y >= im.height:
            continue
        for xx in range(word.alpha.width):
            a = ap[xx, yy]
            if a < 6:
                continue
            X = x + xx
            if X < 0 or X >= im.width:
                continue
            k = a / 255.0
            p = px[X, Y]
            px[X, Y] = (int(p[0] * (1 - k) + T[0] * k + 0.5), int(p[1] * (1 - k) + T[1] * k + 0.5),
                        int(p[2] * (1 - k) + T[2] * k + 0.5), p[3])


def _valley_runs(alpha, thr=3):
    """Runs (a, b) of columns whose summed coverage is <= thr, strictly between the first and last inked column."""
    px = alpha.load()
    sums = [sum(px[x, y] for y in range(alpha.height)) / 255.0 for x in range(alpha.width)]
    ink = [i for i, v in enumerate(sums) if v > thr]
    if not ink:
        return [], sums
    runs = _runs([sums[i] <= thr for i in range(ink[0], ink[-1] + 1)], min_gap=0)
    return [(a + ink[0], b + ink[0]) for a, b in runs], sums


def compose_word(tok, bold, parts, lookup, gap=2):
    """Build a word image that exists on no render from pieces of other words.

    parts: list of {"token": str, "bold": bool, "first": n | "last": n, "gap": px}; a part is a whole word,
           the left piece up to the n-th gap between letters ("first") or the right piece after the
           n-th gap counted from the right ("last"). "gap" is the blank space before the part
           (default 2 hi-res px).
    """
    pieces = []
    for p in parts:
        w = lookup(p["token"], p.get("bold", False))
        a = w.alpha
        lo, hi = 0, a.width
        runs, sums = _valley_runs(a)
        if "first" in p:
            r = runs[p["first"] - 1]
            hi = (r[0] + r[1]) // 2
        elif "last" in p:
            r = runs[len(runs) - p["last"]]
            lo = (r[0] + r[1]) // 2
        inked = [i for i in range(lo, hi) if sums[i] > 0]
        lo, hi = inked[0], inked[-1] + 1
        pieces.append((a.crop((lo, 0, hi, a.height)), w.baseline, p.get("gap", gap)))
    above = max(b for _, b, _ in pieces)
    below = max(im.height - b for im, b, _ in pieces)
    width = 4 + sum(im.width for im, _, _ in pieces) + sum(g for _, _, g in pieces[1:])
    out = Image.new("L", (width, above + below), 0)
    x = 2
    for i, (im, b, g) in enumerate(pieces):
        if i:
            x += g
        out.paste(im, (x, above - b))
        x += im.width
    return Word(tok, bold, out, above, "+".join(p["token"] for p in parts))


def render_text(cid, new_lines, sources, roi, exclude, cards_text, max_width=None, compose=None,
                text_overrides=None):
    """Return (edited hi-res image, region box) with the rules text replaced.

    new_lines: list of markup strings, one per line (explicit breaks), or a single string that is
               wrapped greedily to max_width hi-res pixels (default: the widest line of the old text).
    sources:   [card ids] words may be taken from, in priority order; the card itself is always first.
    cards_text: id -> markup text (current data) used to label the words of a render.
    compose:   {"token": [part, ...]} words that exist on no render are assembled from parts, see compose_word.
    text_overrides: {id: markup} replaces the label text of a render whose segmentation differs from the
               data text (for example a closing quote mark that stands apart from the word before it).
    """
    if text_overrides:
        cards_text = dict(cards_text)
        cards_text.update(text_overrides)
    im = load_hi(cid)
    words_self, tb, lines_self, gopt = harvest_words(cid, cards_text[cid], roi, exclude, verbose=True)
    if words_self is None:
        raise RuntimeError("cannot segment the text of " + cid)
    bank = {}

    def add(words):
        for w in words:
            bank.setdefault((w.token, w.bold), []).append(w)

    add(words_self)
    for d in sources:
        ws, _, _, _ = harvest_words(d, cards_text[d], roi, exclude, verbose=True)
        if ws is not None:
            add(ws)
    # geometry of the original layout
    base_old = [l["baseline"] for l in lines_self]
    gaps = []
    for l in lines_self:
        for (a, b), (c, d) in zip(l["words"], l["words"][1:]):
            gaps.append(c - b)
    space = int(round(statistics.median(gaps))) if gaps else 8
    centers = [(l["words"][0][0] + l["words"][-1][1]) / 2 for l in lines_self]
    cx = statistics.median(centers) + tb.roi[0]
    pitch = statistics.median([b - a for a, b in zip(base_old, base_old[1:])]) if len(base_old) > 1 else 32
    blk_center = (base_old[0] + base_old[-1]) / 2
    def lookup(tok, bold):
        cand = bank.get((tok, bold))
        if not cand and compose and tok in compose:
            w = compose_word(tok, bold, compose[tok], lookup)
            bank[(tok, bold)] = [w]
            return w
        if not cand:
            raise RuntimeError("no word image for %r (bold=%s) in %s; add a donor that contains it" % (tok, bold, cid))
        return cand[0]

    if isinstance(new_lines, str):
        widest = max(l["words"][-1][1] - l["words"][0][0] for l in lines_self)
        limit = max_width or widest
        wrapped, cur, curw = [], [], 0
        for (tok, bold) in tokens_from_markup(new_lines):
            w = lookup(tok, bold).alpha.width - 4
            add_w = w if not cur else space + w
            if cur and curw + add_w > limit:
                wrapped.append(cur)
                cur, curw = [], 0
                add_w = w
            cur.append((tok, bold))
            curw += add_w
        wrapped.append(cur)
        line_tokens = wrapped
    else:
        line_tokens = [tokens_from_markup(l) for l in new_lines]
    n = len(line_tokens)
    if n == len(base_old):
        bases = list(base_old)
    else:
        bases = [blk_center + (i - (n - 1) / 2.0) * pitch for i in range(n)]
    bases = [int(round(b)) + tb.roi[1] for b in bases]

    # assemble
    erase_text(im, tb)
    for toks, yb in zip(line_tokens, bases):
        ws = [lookup(tok, bold) for (tok, bold) in toks]
        total = sum(w.alpha.width - 4 for w in ws) + space * (len(ws) - 1)
        x = int(round(cx - total / 2.0))
        for w in ws:
            draw_word(im, tb, w, x - 2, yb)
            x += w.alpha.width - 4 + space
    box = (roi[0] - roi[0] % HI, roi[1] - roi[1] % HI, roi[2] + roi[2] % HI, roi[3] + roi[3] % HI)
    return im, box, bank
