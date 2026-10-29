#!/usr/bin/env python3
"""Fetch pre-rendered Classic-format card images and record a manifest.

For every visually relevant entity of the dataset (acquisition standard, reward_only and quest
collectibles, plus non-collectible minions/spells/weapons/hero powers/heroes; enchantments and the
Heroic-only Naxxramas entities are skipped, see artlib.wanted_cards) this maps our id to a
source render and downloads it into art/cache/<id>.png.

Source rule (owner decision):
  1. https://art.hearthstonejson.com/v1/render/latest/enUS/256x/VAN_<id>.png  (Classic format)
  2. else the plain id render, but ONLY if the current HearthstoneJSON data for that id has
     the same name/cost/attack/health as our data (or fixes.json lists the id under "fixes" or
     "plain_render_ok"). Entities that are absent from the
     current data (hero powers, heroes, a few tokens) cannot be checked by data and are
     accepted with a note (they have no balance history).
  3. else status "missing".

Status values: ok (unmodified render), patched (fixes.json says patch_renders.py edits it),
generated (fixes.json says patch_renders.py builds it), missing.

Politeness: at most 5 requests/s, retries with backoff, resumable (existing PNGs in the cache
are kept; HTTP 404 answers are remembered in art/cache/negative.json). Output is deterministic:
sorted keys, no timestamps.

Usage (run from anywhere; paths resolve from the module root):
  python tools/art/fetch_renders.py            # download what is missing, write the manifest
  python tools/art/fetch_renders.py --plan     # only print the id -> source mapping decision
  python tools/art/fetch_renders.py --refresh  # forget negative cache entries and re-check
  python tools/art/fetch_renders.py --hires    # also fetch the 512x renders that fixes.json lists in
                                               # "hires_sources" (used as glyph donors, see patch_renders.py)
  python tools/art/fetch_renders.py --hires-all  # 512x of every standard minion/spell/weapon (exploration, ~170 MB)

The 256x image is exactly a 2x2 box-filter downscale of the 512x image, so patches are painted at 512x
and box-downscaled back; that keeps them pixel-consistent with the untouched renders.
"""
import argparse
import os
import struct
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import artlib as A  # noqa: E402

UA = "mod-hearthstone-art-pipeline/1.0 (private local cache)"
MIN_INTERVAL = 0.22  # seconds between requests, about 4.5 requests/s
NEG_PATH = os.path.join(A.CACHE, "negative.json")
SRC_PATH = os.path.join(A.CACHE, "sources.json")

_last = [0.0]


def _throttle():
    dt = time.monotonic() - _last[0]
    if dt < MIN_INTERVAL:
        time.sleep(MIN_INTERVAL - dt)
    _last[0] = time.monotonic()


def http_get(url, tries=5):
    """Return (status, bytes). 404 is returned, not raised. Other failures are retried."""
    delay = 1.0
    for attempt in range(tries):
        _throttle()
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
        if attempt == tries - 1:
            raise RuntimeError("giving up on %s: %s" % (url, err))
        print("  retry %d for %s (%s)" % (attempt + 1, url, err))
        time.sleep(delay)
        delay *= 2
    raise AssertionError("unreachable")


def png_info(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    w, h = struct.unpack(">II", data[16:24])
    return w, h


def ensure_current_cards():
    if not os.path.exists(A.CARDS_LATEST):
        print("downloading current cards.json ...")
        st, data = http_get(A.CARDS_LATEST_URL)
        if st != 200:
            raise RuntimeError("cards.json HTTP %d" % st)
        os.makedirs(A.CACHE, exist_ok=True)
        with open(A.CARDS_LATEST, "wb") as f:
            f.write(data)
    cur = A.load_json(A.CARDS_LATEST)
    return {c["id"]: c for c in cur}, A.sha256_file(A.CARDS_LATEST)


def data_diff(ours, src):
    """Numeric/name mismatches between our card and a card in the current data."""
    diffs = []
    if ours["name"] != src.get("name"):
        diffs.append("name: %r vs %r" % (ours["name"], src.get("name")))
    if ours["type"] in ("HERO",):
        return diffs
    if ours["cost"] is not None and ours["cost"] != src.get("cost"):
        diffs.append("cost: %s vs %s" % (ours["cost"], src.get("cost")))
    if ours["type"] == "WEAPON":
        if ours["attack"] != src.get("attack"):
            diffs.append("attack: %s vs %s" % (ours["attack"], src.get("attack")))
        # the current JSON stores weapon durability in "health"
        d = src.get("health")
        if ours["durability"] != d:
            diffs.append("durability: %s vs %s" % (ours["durability"], d))
    elif ours["type"] == "MINION":
        if ours["attack"] != src.get("attack"):
            diffs.append("attack: %s vs %s" % (ours["attack"], src.get("attack")))
        if ours["health"] != src.get("health"):
            diffs.append("health: %s vs %s" % (ours["health"], src.get("health")))
    return diffs


def fetch_hires(cards, entries, listed, everything):
    """Download 512x versions (same source id as the 256x one) into art/cache/512/."""
    os.makedirs(os.path.join(A.CACHE, "512"), exist_ok=True)
    ids = set(listed)
    if everything:
        ids |= {c["id"] for c in cards if c["acquisition"] in ("standard", "reward_only", "quest")}
    for cid in sorted(ids):
        e = entries.get(cid)
        if not e or not e["source_id"]:
            print("hires: no source for", cid)
            continue
        path = os.path.join(A.CACHE, "512", cid + ".png")
        if os.path.exists(path) and os.path.getsize(path) > 0:
            continue
        url = A.RENDER_BASE.replace("/256x/", "/512x/") + e["source_id"] + ".png"
        st, data = http_get(url)
        if st != 200:
            print("hires: HTTP", st, url)
            continue
        png_info(data)
        with open(path, "wb") as f:
            f.write(data)
        print("hires", cid)


def fetch_art_only(ids):
    """Portrait-only art (512x jpg) for generated cards whose art exists on art.hearthstonejson.com."""
    d = os.path.join(A.CACHE, "artonly")
    os.makedirs(d, exist_ok=True)
    for cid in sorted(ids):
        path = os.path.join(d, cid + ".jpg")
        if os.path.exists(path) and os.path.getsize(path) > 0:
            continue
        st, data = http_get("https://art.hearthstonejson.com/v1/512x/" + cid + ".jpg")
        if st != 200:
            print("art-only: HTTP", st, cid)
            continue
        with open(path, "wb") as f:
            f.write(data)
        print("art-only", cid)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--plan", action="store_true", help="print the mapping decision only, no image downloads")
    ap.add_argument("--hires", action="store_true", help="also fetch 512x renders listed in fixes.json hires_sources")
    ap.add_argument("--hires-all", action="store_true", help="fetch 512x renders of all standard collectibles")
    ap.add_argument("--refresh", action="store_true", help="ignore the negative (404) cache")
    args = ap.parse_args()

    ds = A.load_dataset()
    cards = A.wanted_cards(ds)
    cur, cur_sha = ensure_current_cards()
    fixes_doc = A.load_json(A.FIXES, {"fixes": []})
    fixes = fixes_doc["fixes"]
    fix_mode = {f["id"]: f["mode"] for f in fixes}  # "patch" or "generate"
    # ids whose plain render was checked by eye although the current data says otherwise (data and
    # render of art.hearthstonejson.com are not always from the same build); see fixes.json
    plain_ok = fixes_doc.get("plain_render_ok", {})

    os.makedirs(A.CACHE, exist_ok=True)
    neg = {} if args.refresh else A.load_json(NEG_PATH, {})
    srcmap = A.load_json(SRC_PATH, {})
    old_entries = (A.load_json(A.MANIFEST, {}) or {}).get("entries", {})

    def probe(url_id):
        """Download RENDER_BASE + url_id + .png into a temp record; returns bytes or None."""
        url = A.RENDER_BASE + url_id + ".png"
        if url_id in neg:
            return None
        st, data = http_get(url)
        if st == 404:
            neg[url_id] = 404
            A.dump_json(NEG_PATH, neg)
            return None
        png_info(data)  # validates
        return data

    entries = {}
    counts = {"ok": 0, "patched": 0, "generated": 0, "missing": 0}
    for i, c in enumerate(cards):
        cid = c["id"]
        path = os.path.join(A.CACHE, cid + ".png")
        van_id = "VAN_" + cid
        e = {
            "name": c["name"],
            "type": c["type"],
            "acquisition": c["acquisition"],
            "source_id": None,
            "source_url": None,
            "sha256": None,
            "size": None,
            "width": None,
            "height": None,
            "status": "missing",
            "source_kind": None,
            "note": None,
            "data_diff": [],
            "text_differs": False,
        }
        van = cur.get(van_id)
        plain = cur.get(cid)

        # choose which source id to use
        source_id = None
        note = None
        if args.plan:
            if van:
                source_id = van_id
            elif plain:
                d = data_diff(c, plain)
                ok = not d or cid in fix_mode or cid in plain_ok
                source_id = cid if ok else None
                note = "plain render rejected: " + "; ".join(d) if not ok else "plain id (no VAN_ in data)"
            else:
                source_id = "?" + cid
                note = "not in current data: needs HTTP probe"
        else:
            data = None
            if os.path.exists(path) and os.path.getsize(path) > 0 and cid in srcmap:
                # resumable: keep the cached file
                source_id = srcmap[cid]
                with open(path, "rb") as f:
                    data = f.read()
                if source_id == cid:
                    note = "plain id (no VAN_ render)" if plain else "plain id; absent from current data, not checkable by data"
            else:
                data = probe(van_id)
                if data is not None:
                    source_id = van_id
                elif plain:
                    d = data_diff(c, plain)
                    if d and cid not in fix_mode and cid not in plain_ok:
                        note = "plain render rejected: " + "; ".join(d)
                    else:
                        # a card that fixes.json patches may keep a data difference: the fix corrects it;
                        # a plain_render_ok card keeps it because the render itself was checked by eye
                        data = probe(cid)
                        source_id = cid
                        note = "plain id (no VAN_ render)"
                        if d:
                            note += ("; data differs, corrected by fixes.json" if cid in fix_mode
                                     else "; data differs, render checked (plain_render_ok)")
                else:
                    # not in current data at all: accept the plain render if the service has one
                    data = probe(cid)
                    source_id = cid
                    note = "plain id; absent from current data, not checkable by data"
                if data is not None:
                    with open(path, "wb") as f:
                        f.write(data)
                    srcmap[cid] = source_id
                    A.dump_json(SRC_PATH, srcmap)
                    print("[%3d/%d] %-10s <- %s" % (i + 1, len(cards), cid, source_id))
                else:
                    source_id = None
                    print("[%3d/%d] %-10s MISSING %s" % (i + 1, len(cards), cid, note or ""))

            if data is not None:
                w, h = png_info(data)
                e.update(sha256=A.sha256_bytes(data), size=len(data), width=w, height=h)

        # data comparison against the source card in the current JSON
        if source_id and not source_id.startswith("?"):
            e["source_id"] = source_id
            e["source_url"] = A.RENDER_BASE + source_id + ".png"
            e["source_kind"] = "van" if source_id.startswith("VAN_") else "plain"
            src = cur.get(source_id)
            if src:
                e["data_diff"] = data_diff(c, src)
                if c["type"] not in ("HERO", "HERO_POWER"):
                    e["text_differs"] = A.norm_text(c["text"]) != A.norm_text(src.get("text"))
                    if e["text_differs"]:
                        e["our_text"] = A.plain_text(c["text"])
                        e["source_text"] = A.plain_text(src.get("text"))
            else:
                e["data_diff"] = None  # source not in current data: nothing to compare
        e["note"] = note

        have = bool(source_id) and not source_id.startswith("?")
        mode = fix_mode.get(cid)
        if mode == "generate":
            e["status"] = "generated"
        elif not have:
            e["status"] = "missing"
        elif mode == "patch":
            e["status"] = "patched"
        else:
            e["status"] = "ok"
        prev = old_entries.get(cid, {})
        for k in ("patched_sha256", "patched_size", "fix_reason", "template_id"):  # written by patch_renders.py
            if k in prev and e["status"] in ("patched", "generated"):
                e[k] = prev[k]
        counts[e["status"]] += 1
        entries[cid] = e

    if args.plan:
        for cid, e in sorted(entries.items()):
            if e["status"] == "missing" or (e["source_id"] and e["source_kind"] == "plain") or e["note"]:
                print("%-10s %-9s src=%-14s %s" % (cid, e["status"], e["source_id"], e["note"] or ""))
        print(counts)
        return 0

    if args.hires or args.hires_all:
        fetch_hires(cards, entries, fixes_doc.get("hires_sources", []), args.hires_all)
        fetch_art_only(fixes_doc.get("art_sources", []))

    manifest = {
        "schema": 1,
        "dataset_id": ds["dataset_id"],
        "dataset_content_hash": ds["content_hash"],
        "render_base": A.RENDER_BASE,
        "current_cards_json_sha256": cur_sha,
        "counts": counts,
        "entries": entries,
    }
    A.dump_json(A.MANIFEST, manifest)
    print("wrote", A.MANIFEST)
    print(counts)
    return 0


if __name__ == "__main__":
    sys.exit(main())
