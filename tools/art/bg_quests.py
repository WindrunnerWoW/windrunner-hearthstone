#!/usr/bin/env python3
"""Round quest and quest reward portraits for the Warsong Battlegrounds window.

At the start of turn 4 every player picks one 24.2 quest; when its counter reaches the number the reward turns on. The
addon shows both as small round icons (the offer panel, the progress icon beside my hero, the earned reward), so this
script cuts one round 128x128 portrait per in-pool quest and reward through the same round-portrait pipeline as the
hero powers (power_portraits.py, imported and reused): the bare portrait art cropped to a circle, BLP2 DXT5 with a full
mip chain, the circle inscribed in the texture with a one pixel transparent margin.

The ids are the in-pool entries of data/battlegrounds/quest_values.json (12 quests and 23 rewards) looked up in the
git-ignored data/battlegrounds/bg.normalized.json (roles `quest` and `quest_reward`, each with its dbf_id): the same
selection the engine and gen_bg_cpp.py use, so the portraits can never drift from the cards the server plays.

  tools/art/.venv/Scripts/python tools/art/bg_quests.py            # fetch what is missing, cut everything
  ... --plan     only list the quests and rewards and their source state
  ... --fetch    only download the missing 512x portraits, then stop
  ... --ids BG24_Quest_112 BG24_Reward_130
  ... --jobs N (default: CPU count), --verify (decode back and print the error), --no-blp (sheet and manifest only)

Input : data/battlegrounds/quest_values.json + bg.normalized.json; the 512x art of HearthstoneJSON
        (art/cache/bgquests512/<id>.jpg, gitignored: Blizzard art). A card with no 512x art falls back to a closely
        related 512x render (RENDER_FALLBACK) or to its own 256x card render (convert_blp.source_path) and is marked
        `render:<id>` in the manifest; a card with neither is skipped with a logged note, never a broken file.
        The full reward cards: the 256x renders of HearthstoneJSON (art/cache/<id>.png, gitignored), shown on top of each
        offer in the quest pick.
Output: art/out/BgQuests/<id>.blp and art/out/BgQuests/cards/<id>.blp (gitignored: Blizzard art), art/review/bg_quests/_contact.png (gitignored),
        data/art/bg_quests.manifest.json (committed: `entries` keyed by card id with dbf_id, group and the output file;
        deploy-addon.ps1 copies the BLPs into the client's Textures\\BgQuests\\ and lists them in
        HS_BgTextures.quests / HS_BgTextures.rewards).
Standard library + Pillow only. Deterministic: same input, same bytes.
"""
import argparse
import json
import multiprocessing
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw, ImageFilter  # noqa: E402

import artlib as A  # noqa: E402
import bg_minions as BM  # noqa: E402
import convert_blp as C  # noqa: E402
import minion_portraits as M  # noqa: E402
import power_portraits as P  # noqa: E402

SIZE = P.SIZE                                            # 128, the round portrait size power_portraits.py uses
CACHE = os.path.join(A.CACHE, "bgquests512")             # gitignored 512x art, own folder (bgheroes.py's pattern)
QUEST_VALUES = os.path.join(A.ROOT, "data", "battlegrounds", "quest_values.json")
NORMALIZED = os.path.join(A.ROOT, "data", "battlegrounds", "bg.normalized.json")
OUT = os.path.join(A.ART, "out", "BgQuests")
CARDS_OUT = os.path.join(OUT, "cards")
CARD_URL = "https://art.hearthstonejson.com/v1/render/latest/enUS/256x/"
REVIEW = os.path.join(A.ART, "review", "bg_quests")
MANIFEST = os.path.join(A.ART_DATA, "bg_quests.manifest.json")
ART_URL = "https://art.hearthstonejson.com/v1/512x/"

# A card whose 512x art HearthstoneJSON does not publish: the closest render whose artwork is the same. Empty today
# (every in-pool 24.2 quest and reward has 512x art with no variant), kept so a future id can be mapped like
# bg_minions.RENDER_FALLBACK instead of being skipped.
RENDER_FALLBACK = {}

GROUPS = (("quest", "quests"), ("reward", "rewards"))


# ---------------------------------------------------------------- the id list

def load_bg():
    """The dataset keyed by card id; the dbf_id and the name of a quest/reward live here."""
    with open(NORMALIZED, encoding="utf-8") as f:
        return {c["id"]: c for c in json.load(f)["cards"]}


def load_selection():
    """The in-pool 24.2 quests and rewards, in a stable order: [(group, id, dbf_id, name), ...].

    The pool (in_pool) comes from data/battlegrounds/quest_values.json; the dbf_id and the name come from the
    git-ignored bg.normalized.json. A pool id missing from the dataset is an error, so the selection can never drift
    from the cards the engine plays. Groups are quests first, then rewards, each sorted by id.
    """
    with open(QUEST_VALUES, encoding="utf-8") as f:
        values = json.load(f)
    bg = load_bg()
    out = []
    for group, key in GROUPS:
        for cid in sorted(values[key]):
            if not values[key][cid].get("in_pool"):
                continue
            card = bg.get(cid)
            if card is None:
                raise SystemExit("%s in-pool id not in bg.normalized.json: %s" % (key, cid))
            out.append((group, cid, card["dbf_id"], card["name"]))
    return out


# ---------------------------------------------------------------- fetch

def cached(cid):
    return os.path.join(CACHE, cid + ".jpg")


def render_candidates(cid):
    """The 512x art ids to try for a card id, best first (its own, then RENDER_FALLBACK)."""
    out = [cid]
    if cid in RENDER_FALLBACK and RENDER_FALLBACK[cid] not in out:
        out.append(RENDER_FALLBACK[cid])
    return out


def fetch(cid):
    """The cached 512x portrait for cid, downloading it if needed; None when it is not published."""
    path = cached(cid)
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return path
    os.makedirs(CACHE, exist_ok=True)
    for rid in render_candidates(cid):
        status, body = BM.http_get(ART_URL + rid + ".jpg")
        if status == 200 and body:
            with open(path, "wb") as f:
                f.write(body)
            return path
    return None


# ---------------------------------------------------------------- portraits

def portrait(cid):
    """(art, kind): the round portrait of a card id, else (None, None).

    Best source: the 512x bare portrait of HearthstoneJSON (a circle of API_RADIUS around the middle, like
    power_portraits.portrait). Fallback: the card's own 256x render (the art oval box power_portraits.py uses), the
    same documented fallback as bg_minions.py. The circle is the shared minion_portraits.oval_mask.
    """
    src = fetch(cid)
    if src:
        im = Image.open(src).convert("RGBA")
        c = im.width / 2.0
        r = P.API_RADIUS * im.width / 512.0
        art = im.resize((SIZE, SIZE), Image.LANCZOS, box=(c - r, c - r, c + r, c + r))
        kind = "api512"
    else:
        art, kind = None, None
        for rid in render_candidates(cid):
            path, _ = C.source_path(rid)
            if os.path.exists(path):
                im = Image.open(path).convert("RGBA")
                art = im.resize((SIZE, SIZE), Image.LANCZOS, box=P.BOX)
                kind = "render:" + rid
                break
        if art is None:
            return None, None
    art.putalpha(M.oval_mask())
    return art, kind


def one(args):
    cid, group, dbf_id, name, write = args
    art, kind = portrait(cid)
    rec = {"card": cid, "dbf_id": dbf_id, "name": name, "group": group,
           "file": "art/out/BgQuests/" + cid + ".blp", "source": kind}
    if art is None:
        return rec
    if write:
        blp, nlev = C.build_blp(art)
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, cid + ".blp"), "wb") as f:
            f.write(blp)
        rec.update({"size": list(art.size), "format": "BLP2 DXT5 alpha8 mips=%d" % nlev,
                    "bytes": len(blp), "sha256": A.sha256_bytes(blp)})
    return rec


# A reward HearthstoneJSON has no card render of (Evil Twin, Theotar's Parasol, Secret Sinstone, The Friends Along the
# Way, Victim's Specter) is baked onto the owner's empty reward card: its own 512x art is painted into the card's stone
# window. The name and the text are left empty for the addon to write over the banner and the text box (`baked` in the manifest; HS_BgTextures.rewardBaked).
FRAME_SIZE = (256, 388)
# A reward whose real render exists but has an unfilled `{0}` baked into its text (Ghastly Mask: "Get '{0}'"): baked the
# same way, so the addon writes the text with the placeholder filled (gen_bg_lua.PLACEHOLDER_FALLBACK).
FORCE_BAKE = {"BG24_Reward_130"}
CARD_BOX = (16, 38, 229, 341)                                # where a real render's card stands in its 256x388
BLANK_SRC = os.path.join(A.ROOT, "tools", "art", "assets", "reward_frame_blank.png")
TEMPLATE_CACHE = os.path.join(A.CACHE, "bgquests_template3.png")
MASK_CACHE = os.path.join(A.CACHE, "bgquests_window3.png")


def build_frame():
    """(frame, window mask): the owner's empty reward card (assets/reward_frame_blank.png, the same card bg_textures.py
    draws the addon's own reward card from) scaled to 256x388, and the mask of its dark stone art window. Cached."""
    if os.path.exists(TEMPLATE_CACHE) and os.path.exists(MASK_CACHE):
        return Image.open(TEMPLATE_CACHE).convert("RGBA"), Image.open(MASK_CACHE).convert("L")
    src = Image.open(BLANK_SRC).convert("RGBA")
    src = src.crop(src.split()[3].getbbox())
    # the real renders leave a transparent margin: their card (gem included) fills (16, 38)-(229, 341) of the 256x388
    left, top, right, bottom = CARD_BOX
    k = (right - left) / float(src.width)
    frame = Image.new("RGBA", FRAME_SIZE, (0, 0, 0, 0))
    frame.paste(src.resize((right - left, int(round(src.height * k))), Image.LANCZOS), (left, top))
    flat = Image.new("RGBA", FRAME_SIZE, (255, 255, 255, 255))
    flat.alpha_composite(frame)
    dark = flat.convert("L").point(lambda v: 255 if v < 75 else 0).filter(ImageFilter.MinFilter(3))
    seed = (left + int(205 * k), top + int(140 * k))               # the middle of the window (205, 140 in the blank's own pixels)
    ImageDraw.floodfill(dark, seed, 128)                           # the window: the dark region around it
    mask = dark.point(lambda v: 255 if v == 128 else 0).filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))
    outside = mask.copy()
    ImageDraw.floodfill(outside, (0, 0), 128)                      # fill the speckles inside the window
    mask = outside.point(lambda v: 0 if v == 128 else 255)
    draw = ImageDraw.Draw(mask)
    draw.rectangle((0, 0, left + int(48 * k), FRAME_SIZE[1]), fill=0)      # not the mana gem, not the right border
    draw.rectangle((left + int(304 * k), 0, FRAME_SIZE[0], FRAME_SIZE[1]), fill=0)
    mask = mask.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(0.8))
    os.makedirs(A.CACHE, exist_ok=True)
    frame.save(TEMPLATE_CACHE)
    mask.save(MASK_CACHE)
    return frame, mask


def bake_card(cid):
    """The 256x388 card of a reward with no render: the empty frame with its art in the window, or None."""
    frame, mask = build_frame()
    src = fetch(cid)
    if frame is None or src is None:
        return None
    box = mask.getbbox()
    art = Image.open(src).convert("RGBA")
    bw, bh = box[2] - box[0], box[3] - box[1]
    k = max(bw / float(art.width), bh / float(art.height))
    art = art.resize((int(art.width * k) + 1, int(art.height * k) + 1), Image.LANCZOS)
    ox, oy = (art.width - bw) // 2, (art.height - bh) // 2
    layer = Image.new("RGBA", FRAME_SIZE, (0, 0, 0, 0))
    layer.paste(art.crop((ox, oy, ox + bw, oy + bh)), (box[0], box[1]))
    out = frame.copy()
    out.paste(layer, (0, 0), mask)
    return out


def card_blp(cid, write=True):
    """The reward's real 256x388 card render (art/cache/<id>.png, downloaded when missing) as art/out/BgQuests/cards/<id>.blp,
    padded top-aligned onto a 256x512 canvas (texcoord bottom = 388/512, like bg_minions' full cards). The manifest
    record, or None when HearthstoneJSON has no render."""
    path = os.path.join(A.CACHE, cid + ".png")
    baked = False
    if not (os.path.exists(path) and os.path.getsize(path) > 0):
        status, body = BM.http_get(CARD_URL + cid + ".png")
        if status == 200 and body:
            with open(path, "wb") as f:
                f.write(body)
    im = None
    if cid not in FORCE_BAKE and os.path.exists(path) and os.path.getsize(path) > 0:
        im = Image.open(path).convert("RGBA")
    else:
        im = bake_card(cid)
        baked = True
        if im is None:
            return None
    canvas = Image.new("RGBA", (A.TEX_W, A.TEX_H), (0, 0, 0, 0))
    canvas.paste(im, (0, 0))
    blp, nlev = C.build_blp(canvas)
    if write:
        os.makedirs(CARDS_OUT, exist_ok=True)
        with open(os.path.join(CARDS_OUT, cid + ".blp"), "wb") as f:
            f.write(blp)
    return {"card": cid, "file": "art/out/BgQuests/cards/" + cid + ".blp", "source_size": [im.width, im.height],
            "texcoord": {"left": 0.0, "right": 1.0, "top": 0.0, "bottom": im.height / float(A.TEX_H)},
            "format": "BLP2 DXT5 alpha8 mips=%d" % nlev, "bytes": len(blp), "sha256": A.sha256_bytes(blp),
            "baked": baked}


def verify(cid):
    ref, _ = portrait(cid)
    dec = Image.open(os.path.join(OUT, cid + ".blp")).convert("RGBA")
    rp, dp = ref.load(), dec.load()
    err = n = 0
    for y in range(SIZE):
        for x in range(SIZE):
            if rp[x, y][3] > 0:
                err += sum(abs(rp[x, y][k] - dp[x, y][k]) for k in range(3))
                n += 3
    return err / max(1, n)


def contact_sheet(images, name="_contact.png", cols=7, cell=92):
    """Every portrait over a grey tile with its card id under it; quests first, then rewards, 7 per row."""
    rows = (len(images) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell, rows * (cell + 12)), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for i, (cid, art) in enumerate(images):
        bg = Image.new("RGB", (SIZE, SIZE), (110, 110, 110))
        bg.paste(art, (0, 0), art.split()[3])
        bg = bg.resize((cell - 10, cell - 10), Image.LANCZOS)
        x, y = (i % cols) * cell, (i // cols) * (cell + 12)
        sheet.paste(bg, (x + 5, y + 2))
        dr.text((x + 4, y + cell - 4), cid, fill=(230, 230, 230))
    os.makedirs(REVIEW, exist_ok=True)
    path = os.path.join(REVIEW, name)
    sheet.save(path)
    return path


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ids", nargs="*", help="only these card ids")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--no-blp", action="store_true", help="contact sheet and manifest only")
    ap.add_argument("--no-cards", action="store_true", help="skip the full reward cards (the manifest keeps its `cards`)")
    ap.add_argument("--fetch", action="store_true", help="only download the missing 512x portraits, then stop")
    ap.add_argument("--plan", action="store_true", help="only list the quests and rewards and their source state")
    args = ap.parse_args(argv)

    selection = load_selection()            # (group, id, dbf_id, name)
    if args.ids:
        known = {r[1] for r in selection}
        unknown = [i for i in args.ids if i not in known]
        if unknown:
            sys.exit("not in-pool 24.2 quests/rewards: " + " ".join(unknown))
        wanted = set(args.ids)
        selection = [r for r in selection if r[1] in wanted]

    if args.plan:
        for group, cid, dbf_id, name in selection:
            state = "cached" if os.path.exists(cached(cid)) else "MISSING (run --fetch)"
            print("%-7s %-20s %-8d %-30s %s" % (group, cid, dbf_id, name, state))
        return 0

    if args.fetch:
        no_art = [cid for _, cid, _, _ in selection if not fetch(cid)]
        print("fetched %d, no 512x art for %d%s" % (len(selection) - len(no_art), len(no_art),
              (": " + " ".join(no_art)) if no_art else ""))
        return 0

    for _, cid, _, _ in selection:          # download once, serially and rate limited, like bg_heroes.py
        fetch(cid)

    t0 = time.time()
    work = [(cid, group, dbf_id, name, not args.no_blp) for group, cid, dbf_id, name in selection]
    if args.jobs > 1 and len(work) > 1:
        with multiprocessing.Pool(args.jobs) as pool:
            recs = pool.map(one, work, chunksize=1)
    else:
        recs = [one(w) for w in work]
    dt = time.time() - t0

    sel_ids = [cid for _, cid, _, _ in selection]
    prev = A.load_json(MANIFEST, {"entries": {}})
    entries = prev.get("entries", {})
    images = []
    for rec in recs:
        art, _ = portrait(rec["card"])
        if art is None:
            print("no portrait (no 512x art and no 256x render): %s (%s)" % (rec["card"], rec["name"]))
            continue
        entries[rec["card"]] = rec
        if not args.ids:
            images.append((rec["card"], art))
    if not args.ids:
        entries = {k: v for k, v in entries.items() if k in set(sel_ids)}

    cards = prev.get("cards", {})
    if not args.no_cards:
        for group, cid, _, _ in selection:
            if group != "reward":
                continue
            rec = card_blp(cid, not args.no_blp)
            if rec is None:
                print("no 256x card render: " + cid)
            else:
                cards[cid] = rec
        if not args.ids:
            cards = {k: v for k, v in cards.items() if k in set(sel_ids)}

    A.dump_json(MANIFEST, {
        "schema": 1,
        "generator": "tools/art/bg_quests.py",
        "dataset": "bg-148526",
        "note": "One 128x128 round portrait (BLP2 DXT5 with a circular alpha) per in-pool 24.2 quest and quest reward, "
                "made by tools/art/bg_quests.py into art/out/BgQuests/<id>.blp through the power_portraits.py round "
                "pipeline. The ids are the in_pool entries of data/battlegrounds/quest_values.json (12 quests, 23 "
                "rewards) looked up in bg.normalized.json, the same selection the engine plays. deploy-addon.ps1 copies "
                "them into the client's Textures\\BgQuests\\ and lists the quest ids in HS_BgTextures.quests and the "
                "reward ids in HS_BgTextures.rewards.",
        "count": len(entries),
        "entries": entries,
        "cards_note": "The real 256x388 card render of every in-pool reward, padded top-aligned onto a transparent "
                      "256x512 canvas (texcoord bottom = `bottom`), in art/out/BgQuests/cards/<id>.blp; deploy-addon.ps1 "
                      "copies them into the client's Textures\\BgQuests\\Cards\\ and lists them in HS_BgTextures.rewardCards.",
        "cards": cards,
    })

    if args.verify and not args.no_blp:
        worst = 0.0
        for rec in recs:
            if not os.path.exists(os.path.join(OUT, rec["card"] + ".blp")):
                continue
            e = verify(rec["card"])
            worst = max(worst, e)
            if args.ids:
                print("%-20s mean|dRGB|=%.2f" % (rec["card"], e))
        print("verify: worst mean|dRGB| over %d portraits = %.2f" % (len(recs), worst))

    sheet = contact_sheet(images) if images else None
    n_quest = sum(1 for r in entries.values() if r["group"] == "quest")
    n_reward = sum(1 for r in entries.values() if r["group"] == "reward")
    print("%d quest/reward portraits (%d quests, %d rewards)%s in %.1fs (%d jobs), manifest %s" % (
        len(entries), n_quest, n_reward, "" if not args.no_blp else " (no blp)", dt, args.jobs, MANIFEST))
    if sheet:
        print("contact sheet: " + sheet)
    return 0


if __name__ == "__main__":
    multiprocessing.freeze_support()
    sys.exit(main())
