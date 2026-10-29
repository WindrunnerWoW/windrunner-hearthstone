"""Art-window placeholder for generated cards (native 256x388 resolution, Pillow only).

The window of a frame is found as the set of pixels that vary between cards that share the same
frame (same class and card type, no legendary border): everything else is identical frame.
"""
import os

from PIL import Image, ImageFilter

import artlib as A
import cardimg as C


def window_mask(cards, cls, typ, limit=40):
    """Mask (L, 255 inside) of the art window for the frame of class `cls` and type `typ`."""
    cache = os.path.join(A.CACHE, "masks")
    os.makedirs(cache, exist_ok=True)
    path = os.path.join(cache, "%s_%s.png" % (cls, typ))
    if os.path.exists(path):
        return Image.open(path).convert("L")
    ids = [c["id"] for c in sorted(cards, key=lambda c: c["id"])
           if c["class"] == cls and c["type"] == typ and c["acquisition"] == "standard"
           and c["rarity"] != "LEGENDARY" and os.path.exists(os.path.join(A.CACHE, c["id"] + ".png"))]
    ids = ids[:limit]
    ims = [C.load_lo(i).convert("L") for i in ids]
    W, H = ims[0].size
    data = [im.load() for im in ims]
    n = len(ims)
    m = Image.new("L", (W, H), 0)
    mp = m.load()
    for y in range(H):
        for x in range(W):
            vals = [d[x, y] for d in data]
            mean = sum(vals) / n
            var = sum((v - mean) ** 2 for v in vals) / n
            if var > 15 * 15:
                mp[x, y] = 255
    # keep the biggest blob, close small holes
    m = m.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))
    mp = m.load()
    seen = set()
    best = []
    for y in range(H):
        for x in range(W):
            if mp[x, y] and (x, y) not in seen:
                st = [(x, y)]
                seen.add((x, y))
                comp = []
                while st:
                    a, b = st.pop()
                    comp.append((a, b))
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        q = (a + dx, b + dy)
                        if 0 <= q[0] < W and 0 <= q[1] < H and mp[q] and q not in seen:
                            seen.add(q)
                            st.append(q)
                if len(comp) > len(best):
                    best = comp
    out = Image.new("L", (W, H), 0)
    op = out.load()
    for p in best:
        op[p] = 255
    # fill holes: flood the outside, invert
    inv = Image.new("L", (W + 2, H + 2), 255)
    ip = inv.load()
    for (a, b) in best:
        ip[a + 1, b + 1] = 0
    st = [(0, 0)]
    outside = {(0, 0)}
    while st:
        a, b = st.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            q = (a + dx, b + dy)
            if 0 <= q[0] < W + 2 and 0 <= q[1] < H + 2 and ip[q] and q not in outside:
                outside.add(q)
                st.append(q)
    for y in range(H):
        for x in range(W):
            if (x + 1, y + 1) not in outside:
                op[x, y] = 255
    out = out.filter(ImageFilter.MaxFilter(3))  # cover the last art pixels under the frame rim
    out.save(path)
    return out


def placeholder(lo, mask, base=(158, 158, 164)):
    """Fill the window with a soft grey vignette (the pipeline's 'art missing' look)."""
    W, H = lo.size
    bb = mask.getbbox()
    cx, cy = (bb[0] + bb[2]) / 2.0, (bb[1] + bb[3]) / 2.0
    rx, ry = (bb[2] - bb[0]) / 2.0, (bb[3] - bb[1]) / 2.0
    fill = Image.new("RGBA", lo.size, (0, 0, 0, 0))
    fp = fill.load()
    for y in range(bb[1], bb[3]):
        for x in range(bb[0], bb[2]):
            d = (((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2) ** 0.5
            k = 1.0 - 0.28 * min(1.0, d) ** 2
            fp[x, y] = (int(base[0] * k), int(base[1] * k), int(base[2] * k), 255)
    soft = mask.filter(ImageFilter.GaussianBlur(0.7))
    lo.paste(fill, (0, 0), soft)


def paste_art(lo, mask, art, size, offset):
    """Paste real portrait art (any size) scaled to size x size at offset, clipped to the window.
    Size and offset were fitted on Magma Rager and Chillwind Yeti (native px: 178 px square at (38, 38))."""
    a = art.convert("RGBA").resize((size, size), Image.LANCZOS)
    layer = Image.new("RGBA", lo.size, (0, 0, 0, 0))
    layer.paste(a, tuple(offset))
    soft = mask.filter(ImageFilter.GaussianBlur(0.7))
    lo.paste(layer, (0, 0), soft)
