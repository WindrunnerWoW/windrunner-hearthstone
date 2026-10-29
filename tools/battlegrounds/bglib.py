"""Shared library for the Battlegrounds data pipeline.

Everything that reads or writes ``data/battlegrounds/bg.normalized.json`` goes through this module so that the
importer, the tests and any future generator (C++ card table) agree on the schema. The constructed-card pipeline
(``tools/carddata``) is not touched and not imported.

Standard library only.
"""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parents[2]
DATA_DIR = REPO_ROOT / "data" / "battlegrounds"
SOURCE_DIR = DATA_DIR / "source"
NORMALIZED_PATH = DATA_DIR / "bg.normalized.json"
RENDER_OVERRIDES_PATH = DATA_DIR / "render_overrides.json"   # the cards that follow their hover render (2026-10-19)
BALANCE_OVERRIDES_PATH = DATA_DIR / "balance_overrides.json"   # owner balance changes on top (2026-10-22)

# ---------------------------------------------------------------------------
# Dataset identity and the pinned input
# ---------------------------------------------------------------------------

DATASET_ID = "bg-148526"
SCHEMA_VERSION = 1

PINNED_SOURCE = {
    "file": "cards.enUS.148526.json",
    "url": "https://api.hearthstonejson.com/v1/148526/enUS/cards.json",
    "sha256": "8e849ff5d91b8a4218cb93363e178040e1676bdb3b9f362b2a2f08a6e42cef90",
    "size": 6016336,
}

# hsdata CardDefs.xml of the same build: the only place that links a hero to its hero power (tag 380 HERO_POWER).
# Pinned by commit (the annotated tag 148526, "Patch 24.2.0.148526", points at it).
HSDATA_COMMIT = "714a8780ea5f9c32b635d3f44f48596713b10b18"
PINNED_CARDDEFS = {
    "file": "CardDefs.148526.xml",
    "url": f"https://raw.githubusercontent.com/HearthSim/hsdata/{HSDATA_COMMIT}/CardDefs.xml",
    "sha256": "f40e46e74b20de9ca564e5d0f10914e47dcbb9180a7410b9533680d88c407cd5",
    "size": 67948443,
}
PINNED_SOURCES = (PINNED_SOURCE, PINNED_CARDDEFS)
HERO_POWER_ENUM_ID = "380"             # tag HERO_POWER (a card reference) on a hero entity

SOURCE_INFO = {
    "game_patch": "24.2",
    "game_patch_date": "2022-08-30",
    "hearthstonejson_build": 148526,
    "hsdata_tag_date": "2022-08-31",
}

# ---------------------------------------------------------------------------
# Slice rules (owner decision)
# ---------------------------------------------------------------------------

SLICE_15A = "15A"
# The tribes playable in the current slice. 15E batch 1 adds Elementals, batch 2 Pirates, batch 3 Naga and batch 4
# Quilboar; later batches append theirs. The record field `slice` keeps the "15A" value (the wire and the generated
# tables still name the first slice).
PLAYABLE_TRIBES = ("BEAST", "DEMON", "DRAGON", "ELEMENTAL", "MECHANICAL", "MURLOC", "NAGA", "PIRATE", "QUILBOAR")
SLICE_15A_TRIBES = PLAYABLE_TRIBES     # compatibility name used by the first slice's tools and tests
ALL_TRIBES = "ALL"                     # Nightmare Amalgam: every minion type

# A tribeless minion is NOT playable when its text (or the text/tribe of a token it makes) needs a system this slice
# lacks. Each entry: (reason label, regex on the plain text). The first match of each label is reported. 15E batch 4
# implements Blood Gems and Quilboar, so no label is left; the list stays as the hook for a later system.
TRIBELESS_BLOCKERS = ()
EXCLUDED_TRIBES = ()

# Owner adjustments for tribeless minions: pool id -> True (force into the slice) or False (force out).
# The Elemental enablers whose only blocker was the old tribe rule or a Gold rule the shop already has.
TRIBELESS_OVERRIDES: dict[str, bool] = {
    "BG21_036": True,   # Master of Realities
    "BGS_104": True,    # Nomi, Kitchen Nightmare
    "BGS_105": True,    # Majordomo Executus
    "BG23_352": True,   # Tavern Tipper
    "BG24_018": True,   # Tortollan Blue Shell
}

# Tokens a hero or hero power starts with or summons. HearthstoneJSON links neither, and the prefix rule of the pool
# minions does not run for heroes. The token is marked playable so the generated card tables include it.
HERO_TOKEN_LINKS = {
    "TB_BaconShop_HP_033": ("TB_BaconShop_HP_033t",),   # The Curator: Menagerist starts with a 1/1 Venomous Amalgam
    "BG22_HERO_305": ("BG22_HERO_305t",),               # Onyxia: Broodmother summons a 3/1 Whelp
    # King Mukla's Bananarama hands out Bananas (a spell, not a minion).
    "TB_BaconShop_HP_038": ("TB_BaconShop_HP_038t",),
    # N'Zoth starts with the Fish; Ozumat summons a Tentacle. (Sneed grants a Deathrattle that needs no
    # token card of its own.)
    "TB_BaconShop_HP_105": ("TB_BaconShop_HP_105t",),
    "BG23_HERO_201p": ("BG23_HERO_201pt",),
    # Bru'kan's Earth Invocation grants "Deathrattle: Summon a 1/1 Elemental" (the Stone Elemental).
    "BG22_HERO_001p": ("BG22_HERO_001p_t1et",),
    # Mr. Bigglesworth's Consolation Coin ("Gain 1 Gold this turn only", when a dead warband was empty).
    "TB_BaconShop_HP_080": ("TB_BaconShop_HP_080t",),
    # The Great Akazamzarak's Prestidigitation Discovers the 10 Bacon secrets; two of them summon tokens
    # from the classic set (Venomstrike Trap's 2/3 Poisonous Cobra, Snake Trap's three 1/1 Snakes).
    "TB_BaconShop_HP_020": (
        "TB_Bacon_Secrets_01", "TB_Bacon_Secrets_02", "TB_Bacon_Secrets_04", "TB_Bacon_Secrets_05",
        "TB_Bacon_Secrets_07", "TB_Bacon_Secrets_08", "TB_Bacon_Secrets_10", "TB_Bacon_Secrets_11",
        "TB_Bacon_Secrets_12", "TB_Bacon_Secrets_13", "EX1_170", "EX1_554t",
    ),
    # Tickatus's Prize Wall Discovers a Darkmoon Prize. These are the 29 BGS_Treasures_* spells that
    # CardDefs tags 1735 (BATTLEGROUNDS_DARKMOON_PRIZE_TURN, Prize turns 1-4). Big Banana (BGS_Treasures_000) and
    # Regular Discount (BGS_Treasures_003) carry no tag, so they are not prizes of this snapshot and stay out.
    "TB_BaconShop_HP_106": (
        # Prize turn 1
        "BGS_Treasures_001", "BGS_Treasures_004", "BGS_Treasures_007", "BGS_Treasures_013",
        "BGS_Treasures_029", "BGS_Treasures_033", "BGS_Treasures_040",
        # Prize turn 2
        "BGS_Treasures_006", "BGS_Treasures_009", "BGS_Treasures_010", "BGS_Treasures_012",
        "BGS_Treasures_014", "BGS_Treasures_018", "BGS_Treasures_026", "BGS_Treasures_030",
        "BGS_Treasures_036",
        # Prize turn 3
        "BGS_Treasures_011", "BGS_Treasures_015", "BGS_Treasures_019", "BGS_Treasures_020",
        "BGS_Treasures_034", "BGS_Treasures_037", "BGS_Treasures_039",
        # Prize turn 4
        "BGS_Treasures_016", "BGS_Treasures_022", "BGS_Treasures_023", "BGS_Treasures_025",
        "BGS_Treasures_028", "BGS_Treasures_032",
    ),
}

# System entities a minion text names without an id link in the data (resolved by exact name, checked at import).
KEYWORD_ENTITIES = {
    "Blood Gem": "BG20_GEM",
    "Gold Coin": "TB_BaconShop_HP_008a",
}

ROLES = ("pool_minion", "token", "hero", "hero_power", "quest", "quest_reward")
ROLE_RANK = {r: i for i, r in enumerate(ROLES)}

RACE_WORDS = {  # word after "A/H " in a text -> race value in the data
    "Beast": "BEAST", "Beasts": "BEAST", "Demon": "DEMON", "Demons": "DEMON", "Dragon": "DRAGON",
    "Dragons": "DRAGON", "Mech": "MECHANICAL", "Mechs": "MECHANICAL", "Murloc": "MURLOC", "Murlocs": "MURLOC",
    "Pirate": "PIRATE", "Pirates": "PIRATE", "Elemental": "ELEMENTAL", "Elementals": "ELEMENTAL",
    "Quilboar": "QUILBOAR", "Naga": "NAGA",
}


GOLDEN_BACKLINK_MISMATCH: list = []   # filled by _golden(); build_dataset resets and reports it


class BgDataError(Exception):
    """Any problem with the pinned source or the data derived from it."""


# ---------------------------------------------------------------------------
# Small helpers
# ---------------------------------------------------------------------------

def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(Path(path).read_bytes())


def canonical_json(obj) -> str:
    """Compact, key-sorted JSON used for hashing."""
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def content_hash(cards: list) -> str:
    return sha256_bytes(canonical_json(cards).encode("utf-8"))


def plain_text(text: str | None) -> str:
    """Card text without markup, for matching only (the dataset keeps the original markup)."""
    if not text:
        return ""
    t = re.sub(r"</?[bi]>", "", text).replace("[x]", "")
    return re.sub(r"\s+", " ", t.replace("\xa0", " ")).strip()


def verify_source(source_dir: Path = SOURCE_DIR, pin: dict = PINNED_SOURCE) -> Path:
    path = Path(source_dir) / pin["file"]
    if not path.is_file():
        raise BgDataError(f"{path} is missing: run python -I tools/battlegrounds/fetch_bg_source.py")
    actual = sha256_file(path)
    if actual != pin["sha256"]:
        raise BgDataError(f"{pin['file']}: sha256 MISMATCH\n  expected {pin['sha256']}\n  got      {actual}")
    return path


def read_hero_power_tags(xml_path: Path, hero_ids: set) -> dict:
    """hero card id -> (hero power card id, hero power dbfId) from tag 380 of the hero entities in CardDefs.xml."""
    import xml.etree.ElementTree as ET
    out = {}
    for _, el in ET.iterparse(xml_path):
        if el.tag != "Entity":
            continue
        if el.get("CardID") in hero_ids:
            for tag in el.findall("Tag"):
                if tag.get("enumID") == HERO_POWER_ENUM_ID:
                    out[el.get("CardID")] = (tag.get("cardID"), int(tag.get("value")))
        el.clear()                                           # 68 MB file: do not keep the tree
    return out


def dumps_dataset(dataset: dict) -> str:
    """Stable on-disk serialisation (LF endings, trailing newline, no timestamps)."""
    return json.dumps(dataset, indent=2, ensure_ascii=False) + "\n"


def write_text(path: Path, text: str) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def load_dataset(path: Path = NORMALIZED_PATH) -> dict:
    """Entry point for generators: returns the parsed bg.normalized.json."""
    return json.loads(Path(path).read_text(encoding="utf-8"))


def cards_by_id(dataset: dict) -> dict:
    return {c["id"]: c for c in dataset["cards"]}


# ---------------------------------------------------------------------------
# Record builders
# ---------------------------------------------------------------------------

def _golden(raw: dict, by_dbf: dict, by_id: dict) -> dict | None:
    gid = raw.get("battlegroundsPremiumDbfId")
    if gid is None:                                          # tokens that are spells: golden twin by id convention
        twins = [by_id[i] for i in (raw["id"] + "_G", raw["id"][:-1] + "_Gt" if raw["id"].endswith("t") else "")
                 if i in by_id]
        if not twins or twins[0]["name"] != raw["name"] and raw["type"] != "SPELL":
            return None
        gid = twins[0]["dbfId"]
    g = by_dbf.get(gid)
    if g is None or (g["name"] != raw["name"] and raw["type"] != "SPELL"):
        raise BgDataError(f"{raw['id']}: golden dbfId {gid} missing or not a golden of the same name")
    if g.get("battlegroundsNormalDbfId") not in (None, raw["dbfId"]):  # the data's back-link is stale for a few cards
        GOLDEN_BACKLINK_MISMATCH.append((raw["id"], raw["name"], g["id"]))
    return {
        "id": g["id"],
        "dbf_id": g["dbfId"],
        "attack": g.get("attack"),
        "health": g.get("health"),
        "mechanics": sorted(g.get("mechanics", [])),
        "referenced_tags": sorted(g.get("referencedTags", [])),
        "text": g.get("text"),
    }


def _record(raw: dict, role: str, by_dbf: dict, by_id: dict) -> dict:
    """One dataset card. Every record has the same keys in the same order; n/a fields are null / empty."""
    return {
        "id": raw["id"],
        "dbf_id": raw["dbfId"],
        "name": raw["name"],
        "role": role,
        "type": raw["type"],
        "source_set": raw["set"],
        "slice": None,
        "tier": raw.get("techLevel"),
        "cost": raw.get("cost"),
        "attack": raw.get("attack"),
        "health": raw.get("health"),
        "tribes": [raw["race"]] if raw.get("race") else [],
        "mechanics": sorted(raw.get("mechanics", [])),
        "referenced_tags": sorted(raw.get("referencedTags", [])),
        "text": raw.get("text"),
        "golden": _golden(raw, by_dbf, by_id),
        "tokens": [],
        "referenced_by": [],
        "hero_power": None,
        "extra_hero_powers": [],
        "hero_ids": [],
    }


# ---------------------------------------------------------------------------
# Link discovery
# ---------------------------------------------------------------------------

def _prefix_tokens(base_id: str, raws: list, by_id: dict, exclude: set) -> list:
    """Entities whose id is the card id plus a suffix ('BG19_010t', 'BG22_001t2'). Enchantments are not engine cards."""
    out = []
    for r in raws:
        i = r["id"]
        if (i != base_id and i not in exclude and i.startswith(base_id) and not i[len(base_id)].isdigit()
                and r.get("type") in ("MINION", "SPELL") and r.get("battlegroundsNormalDbfId") is None):
            normal = i[:-3] + "t" if i.endswith("_Gt") else i[:-2] if i.endswith("_G") else None
            if normal in by_id:
                continue                                     # a golden twin: reached through its normal card
            out.append(r)
    return out


def _stat_mentions(text: str):
    return re.findall(r"(\d+)/(\d+) ([A-Z][A-Za-z'-]*)", text)


RENDER_OVERRIDE_FIELDS = ("attack", "health", "text", "mechanics", "cost")


def apply_render_overrides(recs: dict, path: Path = RENDER_OVERRIDES_PATH) -> None:
    """Replaces stats, text and mechanics of the cards in render_overrides.json (and of their golden twins) with what
    their hover render shows, so the engine plays the card the player reads. After linking and the slice rule: those
    stay as the pinned source decides."""
    if not path.is_file():
        return
    data = json.loads(path.read_text(encoding="utf-8"))
    for cid, ov in sorted(data["cards"].items()):
        rec = recs.get(cid)
        if rec is None:
            raise BgDataError(f"render override for {cid}: not a card of the dataset")
        for part, target in ((ov, rec), (ov.get("golden") or {}, rec["golden"])):
            if part and target is None:
                raise BgDataError(f"render override for {cid}: golden values but the card has no golden twin")
            for k, v in part.items():
                if k == "golden":
                    continue
                if k not in RENDER_OVERRIDE_FIELDS:
                    raise BgDataError(f"render override for {cid}: unknown field {k}")
                target[k] = sorted(v) if k == "mechanics" else v


def build_dataset(source_dir: Path = SOURCE_DIR):
    """Returns (dataset, analysis). analysis holds the review lists the report prints (not part of the dataset)."""
    path = verify_source(source_dir)
    GOLDEN_BACKLINK_MISMATCH.clear()
    raws = json.loads(path.read_text(encoding="utf-8"))
    by_id = {r["id"]: r for r in raws}
    by_dbf = {r["dbfId"]: r for r in raws}

    pool = sorted((r for r in raws if r.get("isBattlegroundsPoolMinion")), key=lambda r: r["id"])
    taken = {r["id"] for r in pool} | {by_dbf[r["battlegroundsPremiumDbfId"]]["id"] for r in pool}

    recs: dict[str, dict] = {r["id"]: _record(r, "pool_minion", by_dbf, by_id) for r in pool}

    # candidates for text-based linking: Battlegrounds-flagged minions that are neither pool, golden, hero nor buddy
    cands = [r for r in raws if r.get("type") == "MINION" and r["id"] not in taken
             and r.get("battlegroundsNormalDbfId") is None and r.get("battlegroundsPremiumDbfId") is not None
             and not r.get("isBattlegroundsBuddy") and not r.get("battlegroundsHero")]

    unresolved: list[tuple] = []     # (owner id, what)
    link_log: list[tuple] = []       # (owner id, how, token id)
    queue = list(pool)
    seen: set = set()
    while queue:
        owner = queue.pop(0)
        oid = owner["id"]
        if oid in seen:
            continue
        seen.add(oid)
        text = plain_text(owner.get("text"))
        found: dict[str, dict] = {}
        for t in _prefix_tokens(oid, raws, by_id, taken):
            found[t["id"]] = t
            link_log.append((oid, "id prefix", t["id"]))
        for name, kid in KEYWORD_ENTITIES.items():
            if re.search(r"\b(gain|add|get|play|find)\b[^.]*" + name, text, re.I):   # creates it, not just mentions it
                if kid not in by_id or by_id[kid]["name"] != name:
                    raise BgDataError(f"keyword entity {kid} is not '{name}' in the source")
                found[kid] = by_id[kid]
                link_log.append((oid, f"keyword '{name}'", kid))
        covered = {(t.get("attack"), t.get("health")) for t in found.values() if t["type"] == "MINION"}
        for a, h, word in _stat_mentions(text):
            stats = (int(a), int(h))
            if stats in covered:
                continue
            same = [c for c in cands if (c["attack"], c["health"]) == stats]
            named = [c for c in same if c["name"] in (word, word.rstrip("s"))]
            how = "stats + name"
            if not named and word in RACE_WORDS:
                named = [c for c in same if c.get("race") == RACE_WORDS[word]]
                how = "stats + tribe"
            if named:
                for c in named:
                    found[c["id"]] = c
                    link_log.append((oid, how, c["id"]))
                covered.add(stats)
            else:
                unresolved.append((oid, f"'{a}/{h} {word}' in the text has no entity in the data"))
        for tid, t in found.items():
            if tid not in recs:
                recs[tid] = _record(t, "token", by_dbf, by_id)
                if t["type"] == "MINION":
                    queue.append(t)                          # tokens that make tokens
            if oid not in recs[tid]["referenced_by"]:
                recs[tid]["referenced_by"].append(oid)
            if tid not in recs[oid]["tokens"]:
                recs[oid]["tokens"].append(tid)

    # heroes and their hero powers: HearthstoneJSON has no link, CardDefs.xml carries it as tag 380 on the hero
    heroes = sorted((r for r in raws if r.get("battlegroundsHero") and r.get("battlegroundsSkinParentId") is None),
                    key=lambda r: r["id"])
    tag380 = read_hero_power_tags(verify_source(source_dir, PINNED_CARDDEFS), {h["id"] for h in heroes})
    all_powers = sorted(r["id"] for r in raws if r.get("type") == "HERO_POWER")
    heroes_without_power = []
    power_crosscheck = []        # (hero id, tag 380 power, id-convention guess or None) where they differ
    guess_matched = 0
    for h in heroes:
        hid = h["id"]
        recs[hid] = _record(h, "hero", by_dbf, by_id)
        # cross-check only: the old id-convention guess (<hero>p..., TB_BaconShop_HERO_NN -> HP_0NN...)
        m = re.match(r"TB_BaconShop_HERO_(\d\d)$", hid)
        stems = [hid + "p"] + (["TB_BaconShop_HP_0" + m.group(1)] if m else [])
        guess = sorted((p for p in all_powers if by_id[p]["set"] == "BATTLEGROUNDS" and (
                        p.startswith(stems[0])
                        or any(p.startswith(s) and (len(p) == len(s) or not p[len(s)].isdigit()) for s in stems[1:]))),
                       key=lambda p: (len(p), p))
        guess = guess[0] if guess else None
        guess_matched += guess is not None
        if hid not in tag380:
            heroes_without_power.append(hid)
            if guess:
                power_crosscheck.append((hid, None, guess))
            continue
        pid, pdbf = tag380[hid]
        if pid not in by_id or by_id[pid]["dbfId"] != pdbf or by_id[pid].get("type") != "HERO_POWER":
            raise BgDataError(f"{hid}: tag 380 points at {pid} / {pdbf}, which is not a hero power of the card data")
        if guess != pid:
            power_crosscheck.append((hid, pid, guess))
        # powers that belong to the same hero power: its id plus a suffix (ranks 'p2', 'p3', targets 'p_t1', 'HP_041a')
        extras = [p for p in all_powers if p != pid and p.startswith(pid) and by_id[p]["set"] == "BATTLEGROUNDS"
                  and (pid.endswith("p") or not p[len(pid)].isdigit())]
        recs[hid]["hero_power"] = pid
        recs[hid]["extra_hero_powers"] = sorted(extras, key=lambda p: (len(p), p))
        for p in [pid] + extras:
            if p not in recs:
                recs[p] = _record(by_id[p], "hero_power", by_dbf, by_id)
            recs[p]["hero_ids"].append(hid)

    # Hero / hero power tokens: links the data does not give (see HERO_TOKEN_LINKS). A token is a playable
    # card-table row, never a pool card. 15E batch 3 links spells too
    # (King Mukla's Wild Banana).
    for owner, token_ids in sorted(HERO_TOKEN_LINKS.items()):
        if owner not in recs:
            raise BgDataError(f"HERO_TOKEN_LINKS names an unknown owner {owner}")
        for tid in token_ids:
            raw = by_id.get(tid)
            if raw is None or raw.get("type") not in ("MINION", "SPELL"):
                raise BgDataError(f"{owner}: token {tid} is not a minion or spell of the source")
            if tid not in recs:
                recs[tid] = _record(raw, "token", by_dbf, by_id)
            if raw.get("type") == "MINION" and recs[tid]["tier"] is None:
                recs[tid]["tier"] = 1   # a minion token counts as Tier 1 (HsBgTypes.h)
            recs[tid]["slice"] = SLICE_15A
            if owner not in recs[tid]["referenced_by"]:
                recs[tid]["referenced_by"].append(owner)
            if tid not in recs[owner]["tokens"]:
                recs[owner]["tokens"].append(tid)

    for r in raws:
        if re.match(r"BG\d*_Quest_\d+$", r["id"]):
            recs[r["id"]] = _record(r, "quest", by_dbf, by_id)
        elif r.get("type") == "BATTLEGROUND_QUEST_REWARD":
            recs[r["id"]] = _record(r, "quest_reward", by_dbf, by_id)

    # ---- spell tiers -------------------------------------------
    # A Spellcraft spell takes its source minion's Tier (Vashj and Pashmar find "of your Tier or lower"); the other
    # spells (Gold Coin, Bananas, Blood Gems) are 0. The render does not carry a tier for a spell.
    for rec in recs.values():
        if rec["role"] != "pool_minion" or "Spellcraft" not in plain_text(rec["text"]):
            continue
        for tid in rec["tokens"]:
            t = recs.get(tid)
            if t and t["type"] == "SPELL" and isinstance(rec["tier"], int):
                t["tier"] = rec["tier"] if t["tier"] is None else min(t["tier"], rec["tier"])
    for rec in recs.values():
        if rec["role"] == "token" and rec["type"] == "SPELL" and rec["tier"] is None:
            rec["tier"] = 0

    # ---- ----------------------------------------------------------
    decisions = []          # tribeless review list: (id, name, in/out, reason)
    for r in pool:
        rec = recs[r["id"]]
        race = r.get("race")
        if race in PLAYABLE_TRIBES or race == ALL_TRIBES:
            rec["slice"] = SLICE_15A
        elif race is None:
            reasons = []
            texts = [(r["name"], plain_text(r.get("text")))] + [
                (recs[t]["name"], plain_text(recs[t]["text"])) for t in rec["tokens"]]
            for label, rx in TRIBELESS_BLOCKERS:
                for who, tx in texts:
                    mt = rx.search(tx)
                    if mt:
                        reasons.append(f"{label} ('{mt.group(0)}'" + ("" if who == r["name"] else f" in token {who}") + ")")
                        break
            for t in rec["tokens"]:
                if recs[t]["tribes"] and recs[t]["tribes"][0] in EXCLUDED_TRIBES:
                    reasons.append(f"token {recs[t]['name']} is {recs[t]['tribes'][0].title()}")
            if r["id"] in TRIBELESS_OVERRIDES:
                inside = TRIBELESS_OVERRIDES[r["id"]]
                reasons = ["owner override"]
            else:
                inside = not reasons
            if inside:
                rec["slice"] = SLICE_15A
            decisions.append((r["id"], r["name"], "in" if inside else "out", "; ".join(reasons) or "no missing system"))
    changed = True
    while changed:                                           # tokens follow their owners
        changed = False
        for rec in recs.values():
            if rec["role"] == "token" and rec["slice"] is None and any(recs[o]["slice"] == SLICE_15A
                                                                       for o in rec["referenced_by"]):
                rec["slice"] = SLICE_15A
                changed = True

    # informational: 15A tribed minions whose text names a system 15A lacks (they stay in 15A by the tribe rule)
    flagged = []
    for r in pool:
        if r.get("race") in PLAYABLE_TRIBES or r.get("race") == ALL_TRIBES:
            tx = plain_text(r.get("text"))
            hits = [label for label, rx in TRIBELESS_BLOCKERS if rx.search(tx)]
            if hits:
                flagged.append((r["id"], r["name"], ", ".join(hits)))

    apply_render_overrides(recs)
    apply_render_overrides(recs, BALANCE_OVERRIDES_PATH)   # same format; the balance file wins over the render one

    for rec in recs.values():
        rec["tokens"].sort()
        rec["referenced_by"].sort()
        rec["hero_ids"].sort()
    cards = sorted(recs.values(), key=lambda c: (ROLE_RANK[c["role"]], c["id"]))

    dataset = {
        "dataset_id": DATASET_ID,
        "schema_version": SCHEMA_VERSION,
        "source": {
            **SOURCE_INFO,
            "url": PINNED_SOURCE["url"],
            "file": PINNED_SOURCE["file"],
            "sha256": PINNED_SOURCE["sha256"],
            "size": PINNED_SOURCE["size"],
            "carddefs_file": PINNED_CARDDEFS["file"],
            "carddefs_url": PINNED_CARDDEFS["url"],
            "carddefs_sha256": PINNED_CARDDEFS["sha256"],
            "carddefs_size": PINNED_CARDDEFS["size"],
        },
        "slice_15a_tribes": sorted(PLAYABLE_TRIBES) + [ALL_TRIBES],
        "playable_tribes": sorted(PLAYABLE_TRIBES) + [ALL_TRIBES],
        "content_hash": content_hash(cards),
        "cards": cards,
    }
    analysis = {
        "source_entity_count": len(raws),
        "tribeless": sorted(decisions),
        "flagged": sorted(flagged),
        "unresolved": sorted(unresolved),
        "heroes_without_power": heroes_without_power,
        "power_crosscheck": sorted(power_crosscheck),
        "power_guess_matched": guess_matched,
        "link_log": sorted(link_log),
        "golden_backlink_mismatch": sorted(set(GOLDEN_BACKLINK_MISMATCH)),
        "no_token_summons": sorted(
            (r["id"], r["name"]) for r in pool
            if not recs[r["id"]]["tokens"] and re.search(r"\b(Summon|Add)\b", plain_text(r.get("text")))),
    }
    return dataset, analysis
