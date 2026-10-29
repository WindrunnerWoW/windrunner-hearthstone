#!/usr/bin/env python3
"""Oval portraits for the Warsong Battlegrounds minions.

The Battlegrounds tiles (addon/Hearthstone/Battlegrounds/BgShopUI.lua) show a minion inside the same oval the match
board uses, so the portraits are cut exactly like the constructed set (tools/art/minion_portraits.py): the art oval of
the 256x388 render cropped to the ring's aspect, 128x128, BLP2 DXT5 with a soft elliptic alpha. The crop, the aspect
and the feather come from that script (imported, not copied), so a change there changes both sets together.

The ids are the engine's own table, src/battlegrounds/HsBgCardData.inc (167 pool minions + 16 tokens, in engine
order): the same selection gen_bg_cpp.py / gen_bg_lua.py write, so the portraits can never drift from the card table
the server plays. Twelve ids have no normal 256x card render of their own (nine `BG_`-prefixed Battlegrounds variants
of constructed cards and three native ids); those use the constructed twin, or the golden/"up" render, whose artwork
is the same - see RENDER_FALLBACK / render_candidates. A golden minion has no portrait of its own: the wire sends the
normal id with a golden flag, and the addon draws the gold ring (addon/Hearthstone/Textures/Bg/golden_ring.blp) over
the normal picture, so only the 183 normal cards are cut.

Full cards (the hover preview, like the match window's art/out/Cards and bg_heroes.py `powerCards`): every one of the
183 ids is also written as its whole 256x388 render (the same cached render, so the same render_candidates /
RENDER_FALLBACK choice) padded top-aligned onto a transparent 256x512 canvas, BLP2 DXT5 with mips, to
art/out/BgMinions/cards/<id>.blp. Its golden twin (the `golden=` id of addon/Hearthstone/Battlegrounds/BgCardDB.lua)
gets the same treatment from its own render (art/cache/<goldenId>.png, fetched like the others); a golden id with no
render is skipped and the addon shows the normal card. Both are listed in the manifest under `cards` (texcoord
bottom = height/512).

  tools/art/.venv/Scripts/python tools/art/bg_minions.py            # fetch what is missing, cut every portrait
  tools/art/.venv/Scripts/python tools/art/bg_minions.py --fetch    # only download the missing 256x renders, stop
  tools/art/.venv/Scripts/python tools/art/bg_minions.py --plan      # only list the ids and their source state
  tools/art/.venv/Scripts/python tools/art/bg_minions.py --ids BG19_010 BG21_003
  ... --jobs N (default: CPU count), --verify (decode back and print the error), --no-blp (sheet and manifest only),
      --no-cards (portraits only; the manifest keeps its previous `cards`)

Input : src/battlegrounds/HsBgCardData.inc (the committed engine card table), the 256x renders of HearthstoneJSON
        (art/cache/<id>.png, gitignored: Blizzard art, like the constructed renders; --fetch downloads the missing)
Output: art/out/BgMinions/<id>.blp and art/out/BgMinions/cards/<id>.blp (gitignored: Blizzard art),
        art/review/bg_minions/_contact.png (gitignored), data/art/bg_minions.manifest.json (committed: ids, crop,
        hashes; deploy-addon.ps1 copies the BLPs into the client's Textures\\BgMinions\\ and
        Textures\\BgMinions\\Cards\\ and lists the ids in HS_BgTextures.set / .cards of BgTextures.lua)

Standard library + Pillow only. Deterministic: same input, same bytes.
"""
import argparse
import multiprocessing
import os
import re
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw, ImageFilter  # noqa: E402

import artlib as A  # noqa: E402
import convert_blp as C  # noqa: E402
import cardimg as CI  # noqa: E402
import minion_portraits as M  # noqa: E402

TABLE = os.path.join(A.ROOT, "src", "battlegrounds", "HsBgCardData.inc")
OUT = os.path.join(A.ART, "out", "BgMinions")
CARDS_OUT = os.path.join(OUT, "cards")
CARD_DB = os.path.join(A.ROOT, "addon", "Hearthstone", "Battlegrounds", "BgCardDB.lua")
REVIEW = os.path.join(A.ART, "review", "bg_minions")
MANIFEST = os.path.join(A.ART_DATA, "bg_minions.manifest.json")

UA = "mod-hearthstone-art-pipeline/1.0 (private local cache)"
MIN_INTERVAL = 0.22  # seconds between requests, about 4.5 requests/s (keeps the CDN happy)
ROW_ID = re.compile(r'^\s*\{"([A-Za-z0-9_]+)",')
ROW_SPELL = re.compile(r'CardRole::Spell\},$')
DB_GOLDEN = re.compile(r'^\s*\["([A-Za-z0-9_]+)"\]\s*=\s*\{.*\bgolden="([A-Za-z0-9_]+)"')

# HearthstoneJSON has no normal 256x card render for three pool minions; their artwork is the same in the golden
# (or, for Bronze Warden, the "up") render - only the frame differs, and the portrait crops the art oval. The three
# ids come from api.hearthstonejson.com/v1/148526/enUS/cards.json matched by name.
RENDER_FALLBACK = {
    "BG23_357": "BG23_357_G",      # Mind Muck
    "BG24_009": "BG24_009_G",      # Picky Eater
    "BGS_034": "TB_BaconUps_149",  # Bronze Warden
}

# A spell frame's art window sits a little lower and wider than a minion's (checked on the Blood Gem, Wild Banana,
# Banana Bunch and Gold Coin renders): centred at x = 128, y = 52 .. 178 (the name banner covers its lower end).
SPELL_TOP_Y, SPELL_BOTTOM_Y = 52.0, 178.0

_last = [0.0]


def render_candidates(cid):
    """The render ids to try for a card id, best first.

    A `BG_`-prefixed id is a Battlegrounds variant of a constructed card (Strongshell Scavenger, Tabbycat, ...): the
    renderer only knows the constructed id, whose artwork is the same. RENDER_FALLBACK covers the three ids with no
    normal render at all.
    """
    out = [cid]
    if cid in RENDER_FALLBACK:
        out.append(RENDER_FALLBACK[cid])
    if cid.startswith("BG_") and len(cid) > 3:
        out.append(cid[3:])
    return out


# ---------------------------------------------------------------- the id list

def table_ids(path=TABLE):
    """The normal MINION card ids of the engine's table, in engine order (the first field of every row). 15E batch 3
    spells are rows too, but they get only full cards, never oval portraits."""
    ids = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = ROW_ID.match(line)
            if m and not ROW_SPELL.search(line.rstrip()):
                ids.append(m.group(1))
    return ids


def all_table_ids(path=TABLE):
    """Every normal card id of the engine table, spells included (the full-card renders)."""
    ids = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = ROW_ID.match(line)
            if m:
                ids.append(m.group(1))
    return ids


def golden_ids(path=CARD_DB):
    """{normal id: golden id} from the addon's card DB (the `golden=` field of every normal card row)."""
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = DB_GOLDEN.match(line)
            if m:
                out[m.group(1)] = m.group(2)
    return out


# ---------------------------------------------------------------- fetch

def http_get(url, tries=5):
    """(status, bytes); 404 is returned, other failures are retried. Same shape as fetch_renders.http_get."""
    delay = 1.0
    for attempt in range(tries):
        dt = time.monotonic() - _last[0]
        if dt < MIN_INTERVAL:
            time.sleep(MIN_INTERVAL - dt)
        _last[0] = time.monotonic()
        req = urllib.request.Request(url, headers={"User-Agent": UA})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.status, r.read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return 404, b""
            err = "HTTP %d" % e.code
        except Exception as e:  # network hiccup
            err = repr(e)[:80]
        print("  retry %d/%d %s: %s" % (attempt + 1, tries, url, err))
        time.sleep(delay)
        delay *= 1.7
    return 0, b""


def cached(cid):
    return os.path.join(A.CACHE, cid + ".png")


def fetch(ids):
    """Downloads the missing 256x renders (through render_candidates). Returns (fetched, missing)."""
    os.makedirs(A.CACHE, exist_ok=True)
    fetched, missing = [], []
    for cid in ids:
        path = cached(cid)
        if os.path.exists(path) and os.path.getsize(path) > 0:
            continue
        data = None
        for rid in render_candidates(cid):
            status, body = http_get(A.RENDER_BASE + rid + ".png")
            if status == 200 and body:
                data = body
                break
        if not data:
            missing.append(cid)
            print("  no render: %s (tried %s)" % (cid, ", ".join(render_candidates(cid))))
            continue
        with open(path, "wb") as f:
            f.write(data)
        fetched.append(cid)
    return fetched, missing


def fetch_golden(gids):
    """Downloads the missing golden 256x renders (the golden id itself, no fallback). Returns the ids without one."""
    os.makedirs(A.CACHE, exist_ok=True)
    missing = []
    for gid in gids:
        path = cached(gid)
        if os.path.exists(path) and os.path.getsize(path) > 0:
            continue
        status, body = http_get(A.RENDER_BASE + gid + ".png")
        if status != 200 or not body:
            missing.append(gid)
            continue
        with open(path, "wb") as f:
            f.write(body)
    return missing


# ---------------------------------------------------------------- full cards

def card_canvas(path):
    """The render padded top-aligned onto a transparent 256x512 canvas (as bg_heroes.card_canvas; bottom = h/512)."""
    im = Image.open(path).convert("RGBA")
    canvas = Image.new("RGBA", (A.TEX_W, A.TEX_H), (0, 0, 0, 0))
    canvas.paste(im, (0, 0))
    return canvas, im.height


BALANCE = os.path.join(A.ROOT, "data", "battlegrounds", "balance_overrides.json")
NORMALIZED = os.path.join(A.ROOT, "data", "battlegrounds", "bg.normalized.json")
# The whole stat gem with room for two digits (native 256x388; patch_renders.GEM_BOXES only fits one): the attack gem
# ends before the tribe banner (x 76), the health drop starts after it (x 180).
BALANCE_GEM_BOXES = {"attack": (18, 302, 73, 342), "health": (186, 302, 244, 342)}   # y 302: below the rules text
DIGIT_SHIFT = 26   # px between the two digit cells of a two-digit attack number (measured on "12" / "16")
GEM_FRAME_X = 21   # the attack gem's dark frame ends here; a moved digit cell never paints left of it
ONE_LEFT_X = 23    # the left edge of the "1" of a two-digit attack (only that much of the base is erased)


def balance_ids():
    """The normal ids of balance_overrides.json (empty without the file)."""
    if not os.path.exists(BALANCE):
        return set()
    import json
    return set(json.load(open(BALANCE, encoding="utf-8"))["cards"])


def balance_targets():
    """{render id: {"attack": a, "health": h}} for the cards of balance_overrides.json and their golden twins."""
    if not os.path.exists(BALANCE):
        return {}
    import json
    bal = json.load(open(BALANCE, encoding="utf-8"))["cards"]
    data = {r["id"]: r for r in json.load(open(NORMALIZED, encoding="utf-8"))["cards"]}
    out = {}
    for cid, ov in bal.items():
        out[cid] = {"attack": ov["attack"], "health": ov["health"]}
        g = (data.get(cid) or {}).get("golden")
        if g and ov.get("golden"):
            out[g["id"]] = {"attack": ov["golden"]["attack"], "health": ov["golden"]["health"]}
    return out


def stat_donors(skip):
    """{"attack"/"health": {value: render id}}: a cached Battlegrounds render printing that value in that gem. The
    rebalanced cards are no donors (their renders print the old numbers)."""
    import json
    donors = {"attack": {}, "health": {}}
    for r in sorted(json.load(open(NORMALIZED, encoding="utf-8"))["cards"], key=lambda r: r["id"]):
        if r.get("type") != "MINION" or r["id"] in skip:
            continue
        for rec in (r, r.get("golden")):
            if rec and os.path.exists(cached(rec["id"])):
                for gem in donors:
                    donors[gem].setdefault(rec[gem], rec["id"])
    return donors


def repaint_stats(im, want, donors):
    """Repaints the attack / health gems of a render to `want` from donor renders. A two-digit attack nobody prints
    (golden Young Murk-Eye's 20) is built from the donor of "1<units>" with the tens digit taken from the units cell
    of "1<tens>". Raises if a value cannot be made."""
    for gem, value in want.items():
        box = BALANCE_GEM_BOXES[gem]
        donor = donors[gem].get(value)
        if donor:
            CI.gem_swap(im, CI.load_lo(donor), box)
            continue
        tens, units = divmod(value, 10)
        base, glyph = donors[gem].get(10 + units), donors[gem].get(10 + tens)
        if gem != "attack" or not (1 < tens < 10) or not base or not glyph:
            raise SystemExit("no donor for %s %d" % (gem, value))
        CI.gem_swap(im, CI.load_lo(base), box)
        x0, y0, x1, y1 = box
        mid = (x0 + x1) // 2
        # the units cell of "1<tens>", moved onto the tens cell; only digit ink (white fill, black outline) and the
        # base's own "1" are replaced, so the gem around them stays the base's
        cell = CI.load_lo(glyph).crop((mid, y0, x1, y1))
        left = im.crop((mid - DIGIT_SHIFT, y0, x1 - DIGIT_SHIFT, y1))
        ox = mid - DIGIT_SHIFT
        glyph_mask = digit_mask(cell).filter(ImageFilter.MaxFilter(3))
        one_mask = digit_mask(left).filter(ImageFilter.MaxFilter(3))
        one_mask.paste(0, (0, 0, ONE_LEFT_X - ox, one_mask.height))   # the ring left of the "1" is no digit
        mask = Image.new("L", cell.size, 0)
        mask.paste(255, None, glyph_mask)
        mask.paste(255, None, one_mask)
        mask = mask.filter(ImageFilter.GaussianBlur(0.6))
        mask.paste(0, (0, 0, GEM_FRAME_X - ox, mask.height))   # the gem's frame left of x 21 stays
        im.paste(cell, (ox, y0), mask)


def digit_mask(cell):
    """255 on the pixels of a stat digit: near-white unsaturated fill or near-black outline."""
    out = Image.new("L", cell.size, 0)
    src, dst = cell.load(), out.load()
    for y in range(cell.height):
        for x in range(cell.width):
            r, g, b, a = src[x, y]
            hi, lo = max(r, g, b), min(r, g, b)
            if a > 128 and ((hi > 205 and hi - lo < 45) or hi < 70):
                dst[x, y] = 255
    return out


# Renders the picture service got wrong: the normal Picky Eater (BG24_009) and Mind Muck (BG23_357) are their golden
# cards (double stats, "gain double its stats"), while the data is the normal card. The stat gems are repainted from
# donors and "double " is cut out of the last rules line (the words on either side move together, the line stays centred
# on its old middle). RENDER_FIXES: id -> its gems; DOUBLE_CUT: id -> (line rows, line x range, cut columns, rows of
# clean parchment to paint the line out with), all on the 256x388 render.
RENDER_FIXES = {"BG24_009": {"attack": 1, "health": 1}, "BG23_357": {"attack": 3, "health": 2}}
DOUBLE_CUT = {
    "BG24_009": ((284, 299), (70, 183), (93, 137), (300, 310)),
    "BG23_357": ((294, 308), (65, 197), (103, 149), (289, 294)),
}


def cut_double(im, key, orig):
    (y0, y1), (lx, rx), (c0, c1), (f0, f1) = DOUBLE_CUT[key]
    gap = 3
    left = orig.crop((lx, y0, c0, y1))      # the words come from the render before the gem repaint (its boxes
    right = orig.crop((c1 - 1, y0, rx, y1))     # reach into the last rules line of a four-line text)
    band = orig.crop((lx - 3, f0, rx + 4, f1))
    flipped = band.transpose(Image.FLIP_TOP_BOTTOM)
    for k in range(0, y1 - y0, band.height):
        im.paste(band if (k // band.height) % 2 == 0 else flipped, (lx - 3, y0 + k))
    total = left.width + gap + right.width
    start = int(round((lx + rx) / 2.0 - total / 2.0))
    im.paste(left, (start, y0), left)
    im.paste(right, (start + left.width + gap, y0), right)


def one_card(args):
    """Writes art/out/BgMinions/cards/<key>.blp from art/cache/<key>.png; returns its manifest record. A card of
    balance_overrides.json gets its stat gems repainted first (`want`)."""
    key, extra = args[0], args[1]
    want = args[2] if len(args) > 2 else None
    if want is None and key in RENDER_FIXES:
        want = RENDER_FIXES[key]
    if want:
        im = Image.open(cached(key)).convert("RGBA")
        orig = im.copy()
        repaint_stats(im, want, stat_donors(balance_ids() | set(RENDER_FIXES)))
        if key in DOUBLE_CUT:
            cut_double(im, key, orig)
        os.makedirs(os.path.join(A.ART, "review", "bg_minions", "balance"), exist_ok=True)
        im.save(os.path.join(A.ART, "review", "bg_minions", "balance", key + ".png"))
        canvas = Image.new("RGBA", (A.TEX_W, A.TEX_H), (0, 0, 0, 0))
        canvas.paste(im, (0, 0))
        height = im.height
    else:
        canvas, height = card_canvas(cached(key))
    blp, mips = C.build_blp(canvas)
    os.makedirs(CARDS_OUT, exist_ok=True)
    with open(os.path.join(CARDS_OUT, key + ".blp"), "wb") as f:
        f.write(blp)
    rec = {"card": key, "size": list(canvas.size), "format": "BLP2 DXT5 alpha8 mips=%d" % mips, "bytes": len(blp),
           "sha256": A.sha256_bytes(blp), "bottom": height / float(A.TEX_H)}
    rec.update(extra)
    return rec


# ---------------------------------------------------------------- portraits

def spell_box(box):
    """The minion crop's width/height ratio on the spell frame's art window."""
    h = SPELL_BOTTOM_Y - SPELL_TOP_Y
    w = h * (box[2] - box[0]) / (box[3] - box[1])
    return (128.0 - w / 2.0, SPELL_TOP_Y, 128.0 + w / 2.0, SPELL_BOTTOM_Y)


def one(args):
    cid, box, render_id, write = args
    art, kind = M.portrait(cid, box)      # the shared crop/mask of minion_portraits.py
    rec = {"id": cid, "render_id": render_id, "source": kind}
    if write:
        blp, nlev = C.build_blp(art)
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, cid + ".blp"), "wb") as f:
            f.write(blp)
        rec.update({"format": "BLP2 DXT5 alpha8 mips=%d" % nlev, "blp_bytes": len(blp),
                    "blp_sha256": A.sha256_bytes(blp)})
    return rec


def verify(cid, box):
    ref, _ = M.portrait(cid, box)
    dec = Image.open(os.path.join(OUT, cid + ".blp")).convert("RGBA")
    rp, dp = ref.load(), dec.load()
    err = n = 0
    for y in range(M.SIZE):
        for x in range(M.SIZE):
            if rp[x, y][3] > 0:
                err += sum(abs(rp[x, y][k] - dp[x, y][k]) for k in range(3))
                n += 3
    return err / max(1, n)


def contact_sheet(ids, box, sbox=None, sids=()):
    """Every portrait over a grey tile with the ring's oval drawn around it (to see the fit), 14 per row."""
    cell = 76
    cols = 14
    rows = (len(ids) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell, rows * (cell + 12)), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for i, cid in enumerate(ids):
        art, _ = M.portrait(cid, sbox if cid in sids else box)
        bg = Image.new("RGB", (M.SIZE, M.SIZE), (110, 110, 110))
        bg.paste(art, (0, 0), art.split()[3])
        bg = bg.resize((cell - 6, cell - 6), Image.LANCZOS)
        x, y = (i % cols) * cell, (i // cols) * (cell + 12)
        sheet.paste(bg, (x + 3, y + 2))
        dr.text((x + 2, y + cell - 3), cid[:12], fill=(230, 230, 230))
    os.makedirs(REVIEW, exist_ok=True)
    path = os.path.join(REVIEW, "_contact.png")
    sheet.save(path)
    return path


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ids", nargs="*", help="only these card ids")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--no-blp", action="store_true", help="contact sheet and manifest only")
    ap.add_argument("--no-cards", action="store_true", help="skip the full cards (the manifest keeps its `cards`)")
    ap.add_argument("--fetch", action="store_true", help="only download the missing 256x renders, then stop")
    ap.add_argument("--plan", action="store_true", help="only list the ids and their source state")
    args = ap.parse_args(argv)

    all_ids = all_table_ids()
    minion_ids = table_ids()
    if args.ids:
        unknown = [i for i in args.ids if i not in all_ids]
        if unknown:
            sys.exit("not 15A cards of the engine table: " + " ".join(unknown))
        all_ids = sorted(args.ids)
        minion_ids = [i for i in all_ids if i in set(minion_ids)]

    if args.fetch:
        fetched, missing = fetch(all_ids)
        print("fetched %d, missing %d%s" % (len(fetched), len(missing),
            (": " + " ".join(missing)) if missing else ""))
        return 0

    if args.plan:
        for cid in all_ids:
            state = "cached" if os.path.exists(cached(cid)) else "MISSING (run --fetch)"
            extra = "" if render_candidates(cid)[0] == cid else "  render=" + render_candidates(cid)[-1]
            print("%-14s %s%s" % (cid, state, extra))
        return 0

    fetched, missing = fetch(all_ids)          # a bare run downloads what is missing first
    if fetched:
        print("fetched %d render(s)" % len(fetched))
    ids = [cid for cid in all_ids if cid not in missing]
    mids = [cid for cid in minion_ids if cid not in missing]
    sids = [cid for cid in ids if cid not in set(minion_ids)]        # the spells: Blood Gem, Bananas, Gold Coin, ...
    if missing:
        print("%d id(s) have no render (text fallback in the window): %s" % (len(missing), " ".join(missing)))
    if not ids:
        return 0

    box = M.crop_box(M.target_aspect())
    t0 = time.time()
    work = [(cid, box, render_candidates(cid)[0], not args.no_blp) for cid in mids]
    sbox = spell_box(box)
    work += [(cid, sbox, render_candidates(cid)[0], not args.no_blp) for cid in sids]
    if args.jobs > 1 and len(work) > 1:
        with multiprocessing.Pool(args.jobs) as pool:
            recs = pool.map(one, work, chunksize=2)
    else:
        recs = [one(w) for w in work]
    dt = time.time() - t0

    prev = A.load_json(MANIFEST, {"entries": {}})
    entries = prev.get("entries", {})
    for r in recs:
        entries[r["id"]] = r
    if not args.ids:
        entries = {k: v for k, v in entries.items() if k in set(mids) | set(sids)}

    # the full cards: every normal id, plus its golden twin when HearthstoneJSON has a render for it
    goldens = golden_ids()
    cards = prev.get("cards", {})
    if not args.no_cards and not args.no_blp:
        golden_missing = fetch_golden([goldens[cid] for cid in ids if cid in goldens])
        targets = balance_targets()   # the rebalanced cards print their new stats (balance_overrides.json)
        work = [(cid, {"render_id": render_candidates(cid)[0]}, targets.get(cid)) for cid in ids]
        work += [(goldens[cid], {"golden_of": cid}, targets.get(goldens[cid])) for cid in ids
                 if cid in goldens and goldens[cid] not in golden_missing]
        if args.jobs > 1 and len(work) > 1:
            with multiprocessing.Pool(args.jobs) as pool:
                card_recs = pool.map(one_card, work, chunksize=2)
        else:
            card_recs = [one_card(w) for w in work]
        for r in card_recs:
            cards[r["card"]] = r
        for gid in golden_missing:
            cards.pop(gid, None)
        print("%d full cards (%d normal, %d golden), %d golden id(s) without a render%s" % (
            len(card_recs), len(ids), len(card_recs) - len(ids), len(golden_missing),
            (": " + " ".join(golden_missing)) if golden_missing else ""))
    if not args.ids:
        keep = set(ids) | {goldens[cid] for cid in ids if cid in goldens}
        cards = {k: v for k, v in cards.items() if k in keep}
    A.dump_json(MANIFEST, {
        "schema": 1,
        "dataset": "bg-148526",
        "note": "One 128x128 oval portrait (BLP2 DXT5 with an elliptic alpha) per Battlegrounds minion card, made "
                "by tools/art/bg_minions.py into art/out/BgMinions/<id>.blp; deploy-addon.ps1 copies them into the "
                "client's Textures\\BgMinions\\ and lists the ids in HS_BgTextures (BgTextures.lua). The ids are the "
                "engine's own table (src/battlegrounds/HsBgCardData.inc). A golden minion uses its normal twin's "
                "portrait with the gold ring (Textures\\Bg\\golden_ring.blp) drawn over it.",
        "crop": prev.get("crop") or {"center_x": M.CENTER_X, "top": M.TOP_Y, "bottom": M.BOTTOM_Y,
            "aspect": round(M.target_aspect(), 5), "box": [round(v, 3) for v in box], "feather": M.FEATHER,
            "size": M.SIZE, "shared_with": "data/art/minions.manifest.json"},
        "count": len(entries),
        "entries": entries,
        "cards_note": "The full 256x388 card render of every minion and of its golden twin (BgCardDB.lua `golden=`, "
                      "skipped when HearthstoneJSON has no render), padded top-aligned onto a transparent 256x512 "
                      "canvas (texcoord bottom = `bottom`), in art/out/BgMinions/cards/<id>.blp; deploy-addon.ps1 "
                      "copies them into Textures\\BgMinions\\Cards\\ and lists them in HS_BgTextures.cards.",
        "cards": cards,
    })

    if args.verify and not args.no_blp:
        worst = 0.0
        for r in recs:
            e = verify(r["id"], sbox if r["id"] in set(sids) else box)
            worst = max(worst, e)
            if args.ids:
                print("%-14s mean|dRGB|=%.2f" % (r["id"], e))
        print("verify: worst mean|dRGB| over %d portraits = %.2f" % (len(recs), worst))

    sheet = contact_sheet(mids + sids, box, sbox, set(sids)) if not args.ids else None
    print("%d Battlegrounds portraits%s in %.1fs (%d jobs)" % (
        len(recs), "" if not args.no_blp else " (no blp)", dt, args.jobs))
    if sheet:
        print("contact sheet: " + sheet)
    return 0


if __name__ == "__main__":
    sys.exit(main())
