#!/usr/bin/env python3
"""Convert card renders to BLP2 (DXT5 with a full mip chain) for the vanilla-family client.

Each 256x388 (or 256x382) render is padded top-aligned onto a transparent 256x512 canvas; the
addon must therefore use texcoords (0, 1, 0, height/512), i.e. right = 1 and bottom = 388/512
= 0.7578125 for a 388-pixel render (the exact values are written to
data/art/textures.manifest.json). Output: art/out/Cards/<id>.blp (gitignored).

Why an own encoder: C:/TurtleDev/00_tools/blp-converter-win-x64/BlpConverter.exe is an Avalonia GUI
program. It ignores --help and file arguments and just opens a window, so it cannot be scripted.
The BLP2 container is trivial and DXT5 is a fixed format, so this script writes it directly
(stdlib + Pillow for image loading/resizing only) and can decode its own output with Pillow's BLP
reader to check the result.

  tools/art/.venv/Scripts/python tools/art/convert_blp.py            # the 5 prototype cards
  tools/art/.venv/Scripts/python tools/art/convert_blp.py --ids A B  # specific ids
  tools/art/.venv/Scripts/python tools/art/convert_blp.py --all      # every card with an image
  ... --jobs N (default: CPU count), --verify (decode back and print PSNR)
"""
import argparse
import multiprocessing
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image  # noqa: E402

import artlib as A  # noqa: E402

PROTOTYPES = ["EX1_116", "CS2_029", "CS2_106", "EX1_306", "CS2_034"]
HEADER_SIZE = 148 + 1024  # BLP2 header + the (unused for DXT) 256 entry palette


# ---------------------------------------------------------------- DXT5 encoder

def _c565(c):
    return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)


def _from565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def encode_block(px):
    """px: 16 (r,g,b,a) tuples in row-major order -> 16 bytes of DXT5."""
    alphas = [p[3] for p in px]
    amax, amin = max(alphas), min(alphas)
    if amax == 0:
        return b"\x00" * 8 + b"\x00\x00\x00\x00\x00\x00\x00\x00"
    # alpha block: a0 > a1 selects the 8-level interpolation
    if amax == amin:
        a0, a1 = amax, amin
        ablock = bytes([a0, a1]) + b"\x00" * 6
    else:
        a0, a1 = amax, amin
        levels = [a0, a1] + [((8 - k) * a0 + (k - 1) * a1) // 7 for k in range(2, 8)]
        bits = 0
        for i, a in enumerate(alphas):
            best, bi = 999, 0
            for k, lv in enumerate(levels):
                d = abs(a - lv)
                if d < best:
                    best, bi = d, k
                    if d == 0:
                        break
            bits |= bi << (3 * i)
        ablock = bytes([a0, a1]) + bits.to_bytes(6, "little")
    # colour block from the visible pixels
    vis = [p for p in px if p[3] > 0]
    n = len(vis)
    mr = sum(p[0] for p in vis) / n
    mg = sum(p[1] for p in vis) / n
    mb = sum(p[2] for p in vis) / n
    lo = (min(p[0] for p in vis), min(p[1] for p in vis), min(p[2] for p in vis))
    hi = (max(p[0] for p in vis), max(p[1] for p in vis), max(p[2] for p in vis))
    if lo == hi:
        c0 = _c565(hi)
        c1 = c0
        cblock = struct.pack("<HHI", c0, c1, 0)
        return ablock + cblock
    # principal axis by power iteration on the covariance matrix
    cxx = cxy = cxz = cyy = cyz = czz = 0.0
    for p in vis:
        dr, dg, db = p[0] - mr, p[1] - mg, p[2] - mb
        cxx += dr * dr
        cxy += dr * dg
        cxz += dr * db
        cyy += dg * dg
        cyz += dg * db
        czz += db * db
    ax = [hi[0] - lo[0] + 1.0, hi[1] - lo[1] + 0.5, hi[2] - lo[2] + 0.25]
    for _ in range(6):
        x = cxx * ax[0] + cxy * ax[1] + cxz * ax[2]
        y = cxy * ax[0] + cyy * ax[1] + cyz * ax[2]
        z = cxz * ax[0] + cyz * ax[1] + czz * ax[2]
        m = max(abs(x), abs(y), abs(z))
        if m == 0:
            break
        ax = [x / m, y / m, z / m]
    tmin, tmax = 1e9, -1e9
    for p in vis:
        t = (p[0] - mr) * ax[0] + (p[1] - mg) * ax[1] + (p[2] - mb) * ax[2]
        if t < tmin:
            tmin = t
        if t > tmax:
            tmax = t
    norm = ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]

    def endpoint(t):
        k = t / norm
        return tuple(max(0, min(255, int(round(m_ + k * a_)))) for m_, a_ in zip((mr, mg, mb), ax))

    e0, e1 = endpoint(tmax), endpoint(tmin)
    c0, c1 = _c565(e0), _c565(e1)
    if c0 < c1:
        c0, c1 = c1, c0
    if c0 == c1:
        return ablock + struct.pack("<HHI", c0, c1, 0)
    q0, q1 = _from565(c0), _from565(c1)
    pal = [q0, q1,
           tuple((2 * q0[i] + q1[i]) // 3 for i in range(3)),
           tuple((q0[i] + 2 * q1[i]) // 3 for i in range(3))]
    idx = 0
    for i, p in enumerate(px):
        best, bi = 1 << 30, 0
        for k in range(4):
            q = pal[k]
            d = (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2
            if d < best:
                best, bi = d, k
        idx |= bi << (2 * i)
    return ablock + struct.pack("<HHI", c0, c1, idx)


def encode_level(im):
    w, h = im.size
    data = im.tobytes()
    out = bytearray()
    cache = {}
    for by in range(0, max(h, 4), 4):
        for bx in range(0, max(w, 4), 4):
            block = []
            for y in range(4):
                yy = min(by + y, h - 1)
                base = (yy * w) * 4
                for x in range(4):
                    xx = min(bx + x, w - 1)
                    o = base + xx * 4
                    block.append((data[o], data[o + 1], data[o + 2], data[o + 3]))
            key = tuple(block)
            enc = cache.get(key)
            if enc is None:
                enc = cache[key] = encode_block(block)
            out += enc
    return bytes(out)


def mip_chain(im):
    """Full mip chain; RGB is averaged with alpha weights so padding does not darken the edges."""
    levels = [im]
    cur = im
    while cur.width > 1 or cur.height > 1:
        r, g, b, a = cur.split()
        w, h = max(1, cur.width // 2), max(1, cur.height // 2)
        from PIL import ImageChops
        pm = Image.merge("RGBA", (ImageChops.multiply(r, a), ImageChops.multiply(g, a),
                                  ImageChops.multiply(b, a), a)).resize((w, h), Image.BOX)
        pr, pg, pb, pa = pm.split()
        px_a = pa.load()
        out = Image.new("RGBA", (w, h))
        op = out.load()
        ppr, ppg, ppb = pr.load(), pg.load(), pb.load()
        for y in range(h):
            for x in range(w):
                av = px_a[x, y]
                if av == 0:
                    op[x, y] = (0, 0, 0, 0)
                else:
                    k = 255.0 / av
                    op[x, y] = (min(255, int(ppr[x, y] * k + .5)), min(255, int(ppg[x, y] * k + .5)),
                                min(255, int(ppb[x, y] * k + .5)), av)
        levels.append(out)
        cur = out
    return levels


def build_blp(canvas):
    levels = mip_chain(canvas)
    datas = [encode_level(l) for l in levels]
    offsets = [0] * 16
    sizes = [0] * 16
    pos = HEADER_SIZE
    for i, d in enumerate(datas):
        offsets[i] = pos
        sizes[i] = len(d)
        pos += len(d)
    head = b"BLP2" + struct.pack("<IBBBB", 1, 2, 8, 7, 1)  # type 1, DXT, alpha depth 8, DXT5, has mips
    head += struct.pack("<II", canvas.width, canvas.height)
    head += struct.pack("<16I", *offsets) + struct.pack("<16I", *sizes)
    head += b"\x00" * 1024
    assert len(head) == HEADER_SIZE
    return head + b"".join(datas), len(levels)


# ---------------------------------------------------------------- driver

def source_path(cid):
    p = os.path.join(A.PATCHED, cid + ".png")
    if os.path.exists(p):
        return p, "patched"
    return os.path.join(A.CACHE, cid + ".png"), "render"


def convert_one(cid):
    src, kind = source_path(cid)
    im = Image.open(src).convert("RGBA")
    assert im.width == A.TEX_W and im.height <= A.TEX_H, (cid, im.size)
    canvas = Image.new("RGBA", (A.TEX_W, A.TEX_H), (0, 0, 0, 0))
    canvas.paste(im, (0, 0))
    blp, nlev = build_blp(canvas)
    os.makedirs(A.OUT, exist_ok=True)
    out = os.path.join(A.OUT, cid + ".blp")
    with open(out, "wb") as f:
        f.write(blp)
    return {
        "id": cid,
        "source": kind,
        "source_size": [im.width, im.height],
        "texture_size": [A.TEX_W, A.TEX_H],
        "texcoord": {"left": 0.0, "right": 1.0, "top": 0.0, "bottom": im.height / float(A.TEX_H)},
        "format": "BLP2 DXT5 alpha8 mips=%d" % nlev,
        "blp_bytes": len(blp),
        "blp_sha256": A.sha256_bytes(blp),
    }


def verify(cid):
    """Decode the BLP with Pillow and compare with the padded source: mean abs error on visible pixels."""
    src, _ = source_path(cid)
    ref = Image.new("RGBA", (A.TEX_W, A.TEX_H), (0, 0, 0, 0))
    ref.paste(Image.open(src).convert("RGBA"), (0, 0))
    dec = Image.open(os.path.join(A.OUT, cid + ".blp")).convert("RGBA")
    assert dec.size == ref.size
    rp, dp = ref.load(), dec.load()
    err = n = 0
    aerr = 0
    for y in range(ref.height):
        for x in range(ref.width):
            r, d = rp[x, y], dp[x, y]
            aerr += abs(r[3] - d[3])
            if r[3] > 0:
                err += abs(r[0] - d[0]) + abs(r[1] - d[1]) + abs(r[2] - d[2])
                n += 3
    return err / max(1, n), aerr / float(ref.width * ref.height)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ids", nargs="*")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--verify", action="store_true")
    args = ap.parse_args()
    man = A.load_json(A.MANIFEST)["entries"]
    if args.all:
        ids = [cid for cid, e in sorted(man.items()) if e["status"] != "missing"]
    else:
        ids = args.ids or PROTOTYPES
    t0 = time.time()
    if args.jobs > 1 and len(ids) > 1:
        with multiprocessing.Pool(args.jobs) as pool:
            recs = pool.map(convert_one, ids, chunksize=1)
    else:
        recs = [convert_one(i) for i in ids]
    dt = time.time() - t0
    prev = A.load_json(A.CONVERT_MANIFEST, {"entries": {}})
    entries = prev.get("entries", {})
    for r in recs:
        entries[r["id"]] = r
    A.dump_json(A.CONVERT_MANIFEST, {
        "schema": 1,
        "note": "texcoord = SetTexCoord(left, right, top, bottom) for the 256x512 BLP that holds a top-aligned card image",
        "entries": entries,
    })
    for r in recs:
        line = "%-10s %s %d bytes  bottom=%.7f" % (r["id"], r["format"], r["blp_bytes"], r["texcoord"]["bottom"])
        if args.verify:
            e, a = verify(r["id"])
            line += "  mean|dRGB|=%.2f mean|dA|=%.2f" % (e, a)
        print(line)
    print("%d files in %.1fs (%.2fs/file wall, %d jobs)" % (len(recs), dt, dt / len(recs), args.jobs))


if __name__ == "__main__":
    multiprocessing.freeze_support()
    main()
