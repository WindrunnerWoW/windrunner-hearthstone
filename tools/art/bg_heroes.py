#!/usr/bin/env python3
"""Hero and hero power portraits for the Warsong Battlegrounds window.

The addon looks a seat's hero up by the id the
server sends on the wire. This script cuts, for the curated heroes of data/battlegrounds/hero_armor.json and Bob:

  <heroId>  the curated heroes (TB_BaconShop_HERO_..) as SIZE_HERO (64x64) rounded-square portraits, keyed by their
            card id, so the addon can look one up by the id sent on the wire.
  bob       the bartender (card TB_BaconShopBob) as an arch: a 128x128 texture holding the cropped portrait with an
            arch-shaped alpha (round dome, straight sides); the addon shows it stretched into the tavern arch.
  <heroId>_arch  each curated hero again as a 256x256 rectangle (no alpha) with the aspect of the golden arena arch's
            opening; the hero pick shows it through the arch strips of ArenaUI (the arch is the client's UI ArenaArch texture).

and, for each curated hero, its hero power as a round 128x128 portrait (the same circle crop as power_portraits.py,
imported and reused) keyed by the power id, plus the ``extra_powers`` of hero_armor.json (Galewing's
flightpaths, Tavish's aims, Bru'kan's invocations, Ragnaros's Sulfuras) the same way:

  powers/<powerId>  written to art/out/BgHeroes/powers/<id>.blp; the addon shows it in the hero power button's ring.

Source: the bare portrait art of HearthstoneJSON (https://art.hearthstonejson.com/v1/512x/<id>.jpg, no frame), cached in
art/cache/bgheroes/<id>.jpg for heroes and art/cache/powers512/<id>.jpg for powers (the cache power_portraits.py uses).
The curated hero ids come from data/battlegrounds/hero_armor.json; the names, the hero -> power link and the power
names/cost/text come from data/battlegrounds/bg.normalized.json. Both caches are gitignored (Blizzard art).

  tools/art/.venv/Scripts/python tools/art/bg_heroes.py            # fetch what is missing, cut everything
  ... --plan     only list the heroes and their powers

Output: art/out/BgHeroes/<cardId>.blp and art/out/BgHeroes/powers/<powerId>.blp (gitignored),
        art/review/bg_heroes/_contact.png (gitignored),
        data/art/bg_heroes.manifest.json (committed: entries keyed by card id with Bob under `bob`, a `powers` map
        keyed by power id, sizes and hashes; deploy-addon.ps1 copies the BLPs into the client's Textures\\BgHeroes\\
        and Textures\\BgHeroes\\Powers\\ and lists both in HS_BgTextures).
Standard library + Pillow only. Deterministic: same input, same bytes.
"""
import hashlib
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image  # noqa: E402

import artlib as A  # noqa: E402
import bg_minions as BM  # noqa: E402
import convert_blp as C  # noqa: E402
import power_portraits as P  # noqa: E402

BOB_ID = "TB_BaconShopBob"
SIZE_HERO = 64
SIZE_BOB = 128
CACHE = os.path.join(A.CACHE, "bgheroes")
ARMOR = os.path.join(A.ROOT, "data", "battlegrounds", "hero_armor.json")
NORMALIZED = os.path.join(A.ROOT, "data", "battlegrounds", "bg.normalized.json")
OUT = os.path.join(A.ART, "out", "BgHeroes")
POWERS_OUT = os.path.join(OUT, "powers")
CARDS_OUT = os.path.join(OUT, "powercards")
CARD_CACHE = os.path.join(A.CACHE, "bgpowercards")
CARD_URL = "https://art.hearthstonejson.com/v1/render/latest/enUS/256x/"
REVIEW = os.path.join(A.ART, "review", "bg_heroes")
MANIFEST = os.path.join(A.ART_DATA, "bg_heroes.manifest.json")
ART_URL = "https://art.hearthstonejson.com/v1/512x/"

BOB_BOX = (56, 8, 456, 488)        # 400 x 480: the face and shoulders, the aspect of the arch opening (5 : 6)
ARCH_BOX = (48, 8, 464, 488)       # 416 x 480: the aspect of the arena arch's opening (0.867)
SIZE_ARCH = 256
HERO_BOX = (64, 0, 448, 384)       # a square on the upper middle, where the faces are


def smooth(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def load_bg():
    """The dataset, keyed by card id: the hero -> power link and the names live here (bg.normalized.json)."""
    with open(NORMALIZED, encoding="utf-8") as f:
        return {c["id"]: c for c in json.load(f)["cards"]}


def curated_heroes(bg):
    """The curated hero ids of hero_armor.json, in that order, with their power id and names .

    Returns a list of (hero id, hero name, power id, power name). The list order is the curated order, so the manifest
    and the contact sheet are stable.
    """
    with open(ARMOR, encoding="utf-8") as f:
        curated = json.load(f)["curated"]
    out = []
    for hid in curated:
        hero = bg.get(hid)
        if hero is None:
            raise SystemExit("curated hero not in bg.normalized.json: " + hid)
        pid = hero["hero_power"]
        power = bg.get(pid)
        if power is None:
            raise SystemExit("hero %s: power %s missing from bg.normalized.json" % (hid, pid))
        out.append((hid, hero["name"], pid, power["name"]))
    return out


def curated_powers(bg, heroes):
    """Every hero power the addon can show: the curated heroes' own powers plus ``extra_powers``.

    The extra powers are the ones a hero can take on (Galewing's flightpaths, Tavish's aims, Bru'kan's invocations,
    Ragnaros's Sulfuras); the game can make one the current power or offer it in a power choice. Returns a list of
    (power id, power name) in a stable order, without duplicates.
    """
    with open(ARMOR, encoding="utf-8") as f:
        extra = json.load(f).get("extra_powers", [])
    out, seen = [], set()
    for _, _, pid, pname in heroes:
        if pid not in seen:
            seen.add(pid)
            out.append((pid, pname))
    for pid in extra:
        if pid in seen:
            continue
        power = bg.get(pid)
        if power is None or power.get("role") != "hero_power":
            raise SystemExit("extra power %s is not a hero power of bg.normalized.json" % pid)
        seen.add(pid)
        out.append((pid, power["name"]))
    return out


def fetch(card_id):
    path = os.path.join(CACHE, card_id + ".jpg")
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return path
    os.makedirs(CACHE, exist_ok=True)
    status, body = BM.http_get(ART_URL + card_id + ".jpg")
    if status != 200 or not body:
        return None
    with open(path, "wb") as f:
        f.write(body)
    return path


def fetch_card(card_id):
    """The 256x388 render of a hero power card (the hover preview shows it like the normal game's power cards)."""
    path = os.path.join(CARD_CACHE, card_id + ".png")
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return path
    os.makedirs(CARD_CACHE, exist_ok=True)
    status, body = BM.http_get(CARD_URL + card_id + ".png")
    if status != 200 or not body:
        return None
    with open(path, "wb") as f:
        f.write(body)
    return path


def card_canvas(path):
    """The render padded top-aligned onto a transparent 256x512 canvas, like art/out/Cards (texcoord bottom 388/512)."""
    im = Image.open(path).convert("RGBA")
    canvas = Image.new("RGBA", (A.TEX_W, A.TEX_H), (0, 0, 0, 0))
    canvas.paste(im, (0, 0))
    return canvas, im.height


def rounded(im, radius):
    """The image with a rounded-square alpha (soft one-pixel edge, one transparent pixel of margin)."""
    w, h = im.size
    im = im.convert("RGBA")
    px = im.load()
    for y in range(h):
        for x in range(w):
            dx = max(radius - 0.5 - x, x - (w - 1 - radius + 0.5), 0.0)
            dy = max(radius - 0.5 - y, y - (h - 1 - radius + 0.5), 0.0)
            d = math.hypot(dx, dy) if dx > 0 and dy > 0 else max(dx, dy)
            edge = min(x, y, w - 1 - x, h - 1 - y) + 0.5
            a = (1.0 - smooth(radius - 1.5, radius + 0.5, d)) * smooth(0.5, 1.5, edge) if dx > 0 and dy > 0 else \
                smooth(0.5, 1.5, edge)
            r, g, b, _ = px[x, y]
            px[x, y] = (r, g, b, int(round(255 * a)))
    return im


def arch(im):
    """The image with an arch alpha: a dome on top (a half circle over the full width), straight sides below."""
    w, h = im.size
    im = im.convert("RGBA")
    px = im.load()
    rx = (w - 2) / 2.0
    ry = rx * 0.9
    cx = (w - 1) / 2.0
    for y in range(h):
        for x in range(w):
            if y < ry + 1:
                d = math.hypot((x - cx) / rx, (y - (ry + 1)) / ry)
                inside = 1.0 - smooth(1.0 - 1.5 / rx, 1.0 + 0.5 / rx, d)
            else:
                inside = smooth(0.5, 1.5, min(x, w - 1 - x) + 0.5)
            if y > h - 2:
                inside *= 0.0 if y > h - 1 else 0.6
            r, g, b, _ = px[x, y]
            px[x, y] = (r, g, b, int(round(255 * inside)))
    return im


def cut_bob(path):
    im = Image.open(path).convert("RGB").crop(BOB_BOX).resize((SIZE_BOB, SIZE_BOB), Image.LANCZOS)
    return arch(im)


def cut_arch(path):
    """The face and shoulders as an opaque rectangle for the arch strips (the arch itself is drawn by the addon)."""
    return Image.open(path).convert("RGB").crop(ARCH_BOX).resize((SIZE_ARCH, SIZE_ARCH), Image.LANCZOS).convert("RGBA")


def cut_hero(path):
    im = Image.open(path).convert("RGB").crop(HERO_BOX).resize((SIZE_HERO, SIZE_HERO), Image.LANCZOS)
    return rounded(im, 9)


def one_bitmap(img):
    """The generated record fields shared by a hero, Bob and a power entry (size, format, bytes, sha256)."""
    blp, mips = C.build_blp(img)
    return blp, {"size": list(img.size), "format": "BLP2 DXT5 alpha8 mips=%d" % mips,
                 "bytes": len(blp), "sha256": hashlib.sha256(blp).hexdigest()}


def sheet(images, key, cols, cell):
    os.makedirs(REVIEW, exist_ok=True)
    rows = (len(images) + cols - 1) // cols
    img = Image.new("RGB", (cols * cell, rows * cell), (110, 110, 110))
    for i, (_, art) in enumerate(images):
        img.paste(art, ((i % cols) * cell + 4, (i // cols) * cell + 4), art.split()[3])
    path = os.path.join(REVIEW, key)
    img.save(path)
    return path


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    bg = load_bg()
    heroes = curated_heroes(bg)
    powers_wanted = curated_powers(bg, heroes)
    jobs = [("bob", {"id": BOB_ID, "name": "Bartender Bob"}, cut_bob),
            ("bob_arch", {"id": BOB_ID, "name": "Bartender Bob"}, cut_arch)]    # Bob in the shop's golden arch
    for hid, name, _, _ in heroes:
        jobs.append((hid, {"id": hid, "name": name}, cut_hero))
        jobs.append((hid + "_arch", {"id": hid, "name": name}, cut_arch))    # the hero pick's arch portrait
    if "--plan" in argv:
        for key, info, _ in jobs:
            print("%-24s %-24s %s" % (key, info["id"], info["name"]))
        for hid, name, pid, pname in heroes:
            print("  %-20s -> power %-24s %s" % (name, pid, pname))
        return 0

    os.makedirs(OUT, exist_ok=True)
    records, images = {}, []
    for key, info, cutter in jobs:
        src = fetch(info["id"])
        if not src:
            print("no portrait: %s (%s)" % (info["id"], info["name"]))
            continue
        art = cutter(src)
        blp, rec = one_bitmap(art)
        with open(os.path.join(OUT, key + ".blp"), "wb") as f:
            f.write(blp)
        rec.update({"card": info["id"], "name": info["name"]})
        records[key] = rec
        images.append((key, art))

    # The curated hero powers and the extra choice powers: round 128x128 portraits through power_portraits.py.
    powers = {}
    power_images = []
    failed = P.fetch([pid for pid, _ in powers_wanted])
    if failed:
        print("no hero power art: " + " ".join(failed))
    for pid, pname in powers_wanted:
        if not os.path.exists(P.api_path(pid)):
            print("no portrait: %s (%s)" % (pid, pname))
            continue
        art, _ = P.portrait(pid)
        blp, rec = one_bitmap(art)
        os.makedirs(POWERS_OUT, exist_ok=True)
        with open(os.path.join(POWERS_OUT, pid + ".blp"), "wb") as f:
            f.write(blp)
        rec.update({"card": pid, "name": pname})
        powers[pid] = rec
        power_images.append((pid, art))

    # The same powers as full cards (256x388 render on a 256x512 canvas): the hero pick's hover shows the card, and
    # the shop's power-choice Discover offers them.
    power_cards = {}
    for pid, pname in powers_wanted:
        src = fetch_card(pid)
        if not src:
            print("no power card render: %s (%s)" % (pid, pname))
            continue
        canvas, height = card_canvas(src)
        blp, rec = one_bitmap(canvas)
        os.makedirs(CARDS_OUT, exist_ok=True)
        with open(os.path.join(CARDS_OUT, pid + ".blp"), "wb") as f:
            f.write(blp)
        rec.update({"card": pid, "name": pname, "bottom": height / float(A.TEX_H)})
        power_cards[pid] = rec

    os.makedirs(os.path.dirname(MANIFEST), exist_ok=True)
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"schema": 1, "generator": "tools/art/bg_heroes.py", "bob": BOB_ID,
                   "note": "The curated 15C/15E heroes keyed by card id, Bob under `bob`, and their hero powers "
                           "(including the batch 5b extra powers) under `powers` keyed by power id. Made by "
                           "tools/art/bg_heroes.py into art/out/BgHeroes/<cardId>.blp (rounded-square) and "
                           "art/out/BgHeroes/powers/<powerId>.blp (round, power_portraits.py); deploy-addon.ps1 copies "
                           "them into the client's Textures\\BgHeroes\\ and Textures\\BgHeroes\\Powers\\ and lists both "
                           "in HS_BgTextures.",
                   "entries": records, "powers": powers, "powerCards": power_cards},
                  f, indent=2, sort_keys=True)
        f.write("\n")
    sheet(images, "_contact.png", 7, 72)
    if power_images:
        sheet(power_images, "_powers.png", 6, 148)
    print("%d hero portraits and %d hero power portraits -> %s" % (len(records), len(powers), OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
