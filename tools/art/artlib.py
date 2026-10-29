"""Shared helpers for the card-artwork pipeline.

Paths are resolved relative to the module root (modules/mod-hearthstone), never the cwd.
Standard library only; Pillow is imported lazily by the scripts that need it.
"""
import hashlib
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATASET = os.path.join(ROOT, "data", "cards", "cards.normalized.json")
ART_DATA = os.path.join(ROOT, "data", "art")
MANIFEST = os.path.join(ART_DATA, "renders.manifest.json")
FIXES = os.path.join(ART_DATA, "fixes.json")
CONVERT_MANIFEST = os.path.join(ART_DATA, "textures.manifest.json")
ART = os.path.join(ROOT, "art")
CACHE = os.path.join(ART, "cache")
PATCHED = os.path.join(ART, "patched")
OUT = os.path.join(ART, "out", "Cards")
REVIEW = os.path.join(ART, "review")
TOOLS_ART = os.path.join(ROOT, "tools", "art")

RENDER_BASE = "https://art.hearthstonejson.com/v1/render/latest/enUS/256x/"
CARDS_LATEST_URL = "https://api.hearthstonejson.com/v1/latest/enUS/cards.json"
CARDS_LATEST = os.path.join(CACHE, "cards_latest.json")

sys.path.insert(0, os.path.join(ROOT, "tools", "carddata"))
import cardlib  # noqa: E402

SRC_W, SRC_H = 256, 388  # size of the 256x renders
TEX_W, TEX_H = 256, 512  # padded texture size


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def load_dataset():
    with open(DATASET, encoding="utf-8") as f:
        return json.load(f)


# Heroic-only Naxxramas entities are never used (owner decision: no Heroic mode, ever), so they get no art. Their
# ids are NAX<n>h_<nn> (the Heroic Anub'Rekhan set) or NAX<n>_<nn>H (everything else, including the cards that only
# exist in Heroic such as Feugen and Stalagg, NAX13_04H / NAX13_05H).
HEROIC_ONLY_ID = re.compile(r"^NAX\d+(h_\d+|_\d+[a-z]?H)$")


def art_heroic_only(card):
    """True for a Heroic-mode Naxxramas encounter entity (see HEROIC_ONLY_ID)."""
    return card.get("set") == "NAXX" and not card.get("collectible") and bool(HEROIC_ONLY_ID.match(card["id"]))


def wanted_cards(dataset):
    """Entities that appear visually: standard/reward_only/quest collectibles plus every
    non-collectible non-enchantment entity plus the class heroes. Sorted by id.
    Entities that never appear in the released game are left out (cardlib.client_visible:
    acquisition "unreleased" (none at the moment) and the unsummoned Skeleton token)."""
    out = []
    for c in dataset["cards"]:
        if c["type"] == "ENCHANTMENT" or not cardlib.client_visible(c) or art_heroic_only(c):
            continue
        if c["acquisition"] in ("standard", "reward_only", "quest") or c["type"] in (
            "MINION", "SPELL", "WEAPON", "HERO_POWER", "HERO"
        ):
            out.append(c)
    out.sort(key=lambda c: c["id"])
    return out


def load_json(path, default=None):
    if not os.path.exists(path):
        return default
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def dump_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, indent=2, sort_keys=True, ensure_ascii=False)
        f.write("\n")


_TAG = re.compile(r"<[^>]*>")


def norm_text(t):
    """Text normalised for wording comparisons: markup, spell-damage markers,
    punctuation and case removed."""
    if not t:
        return ""
    t = _TAG.sub("", t)
    t = t.replace("[x]", "").replace("$", "").replace("#", "").replace(" ", " ")
    t = re.sub(r"[^A-Za-z0-9/+ ]+", " ", t)
    return re.sub(r"\s+", " ", t).strip().lower()


def plain_text(t):
    """Display text with markup removed, whitespace collapsed, markers kept out."""
    if not t:
        return ""
    t = _TAG.sub("", t).replace("[x]", "").replace("$", "").replace("#", "").replace(" ", " ")
    return re.sub(r"\s+", " ", t).strip()
