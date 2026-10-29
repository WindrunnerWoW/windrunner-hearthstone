"""Shared library for the card data pipeline.

Everything that reads or writes ``data/cards/cards.normalized.json`` goes through
this module so that the importer, the validator, the tests and any future
generator (server SQL, addon CardDB.lua, ...) agree on the schema.

Standard library only.
"""

from __future__ import annotations

import csv
import hashlib
import json
import re
from pathlib import Path

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parents[2]
DATA_DIR = REPO_ROOT / "data" / "cards"
SOURCE_DIR = DATA_DIR / "source"
OVERRIDES_DIR = DATA_DIR / "overrides"
CUSTOM_DIR = DATA_DIR / "custom"
LINKS_DIR = DATA_DIR / "links"
NORMALIZED_PATH = DATA_DIR / "cards.normalized.json"

# ---------------------------------------------------------------------------
# Dataset identity and pinned inputs
# ---------------------------------------------------------------------------

DATASET_ID = "hs-6024-basic-classic"
SCHEMA_VERSION = 1

HSDATA_COMMIT = "ff82e0ab42e3d3f783c6a5e5fbd5c9d7bd9c8acb"

# Every raw input is pinned by sha256. The importer refuses to run on a mismatch.
PINNED_SOURCES = {
    "cards_json": {
        "file": "cards.enUS.6024.json",
        "url": "https://api.hearthstonejson.com/v1/6024/enUS/cards.json",
        "sha256": "bc24fb05c433ced4f32e491fa8d7fe7babfc2f1386e6e1b7a0c41c3e89fff604",
        "size": 238448,
        "committed": True,
    },
    "manifest_cards_csv": {
        "file": "manifest-cards.ff82e0a.csv",
        "url": f"https://raw.githubusercontent.com/HearthSim/hsdata/{HSDATA_COMMIT}/manifest-cards.csv",
        "sha256": "0cac012d34cd9e620227e3ef22e74d6486f4e6fac2fa05fba7e2a2a6eb129c4e",
        "size": 14802,
        "committed": True,
    },
    # Cross-check only: large, multi-locale, NOT stored in the repository.
    "carddefs_xml": {
        "file": "CardDefs.xml",
        "url": f"https://raw.githubusercontent.com/HearthSim/hsdata/{HSDATA_COMMIT}/CardDefs.xml",
        "sha256": "1c7717e9a5851b3eaeffe77cc0973305dd95feef709c639fce7247a503ca6ba3",
        "size": 2454456,
        "committed": False,
    },
}

SOURCE_INFO = {
    "game": "Hearthstone",
    "client_build": "1.1.0.6024",
    "build_number": 6024,
    "patch_date": "2014-07-22",
    "note": "Curse of Naxxramas launch build; this dataset keeps the Basic and Classic sets, the Naxxramas set "
            "(NAXX) and a few reward-only cards of other sets.",
    "hsdata_commit": HSDATA_COMMIT,
}

# Sets whose entities are imported wholesale (collectible AND runtime entities).
INCLUDED_SETS = ("CORE", "EXPERT1", "NAXX")
# Sets whose collectible cards are "standard": the booster pool, crafting and the starter collection (378 cards).
STANDARD_SETS = ("CORE", "EXPERT1")
# Sets whose collectible cards are earned only through quest rewards (acquisition "quest"): the Curse of Naxxramas
# set. NAXX also holds 131 non-collectible entities (boss heroes and hero powers, encounter cards, tokens,
# enchantments, Heroic variants); they are kept as runtime entities (acquisition "none") for the boss encounters.
QUEST_SETS = ("NAXX",)

# Collectible cards from other sets (PROMO/REWARD) that are imported, keeping their
# original set, but that stay out of packs and crafting: they can only be earned later
# as special quest rewards (acquisition "reward_only").
REWARD_ONLY_IDS = ("EX1_062", "EX1_112", "NEW1_016", "PRO_001")

# Entities of the pinned build that never existed in the released game and are dropped from the dataset altogether
# (owner decision): id -> why. The importer fails if a kept card references one of them.
REMOVED_IDS = {
    "NEW1_006": "Adrenaline Rush (Rogue epic spell): flagged collectible in build 6024 but removed during the closed "
                "alpha (replaced by Shiv) and never obtainable in the released game",
    "EX1_016t": "Skeleton 3/3 (EXPERT1): no card summons or creates it (no entourage entry, no Skeleton in any card text)",
}

# Tokens that a kept card creates through its script, but that the source does not list as an entourage entry. They
# come along with their card as "reference" entities: card id -> token ids. (The two promo tokens are PROMO set entities,
# so without this they would be dropped as other-set entities.)
IMPLICIT_REFERENCES = {
    "PRO_001a": ("PRO_001at",),  # I Am Murloc (Elite Tauren Chieftain): the 1/1 Murloc token
    "Mekka4": ("Mekka4t",),      # Poultryizer (Gelbin Mekkatorque): the 1/1 Chicken token
}

# Non-collectible entities that no Basic/Classic card of this dataset creates or references
# (no entourage entry, no mention in any card text; Naxxramas cards that say "Skeleton" mean their own NAX4_03), so they never appear in play. They stay
# in the dataset and the server tables; the addon CardDB and the art pipeline leave them out.
# Tokens that ARE created by a card effect (Silver Hand Recruit, Treant, Murloc Scout, Boar,
# ...) are often referenced only implicitly by the card text, so this is an explicit list and
# not a computed one. validate_cards.py fails if a card starts to reference one of these ids.
UNSUMMONED_TOKENS = {
    "skele11": "Skeleton 1/1 (CORE): no card summons or creates it; left over from mission scripts",
}

# Acquisition values whose entities are not shown by the client (collection, card previews).
CLIENT_HIDDEN_ACQUISITIONS = ("unreleased",)


def client_visible(card: dict) -> bool:
    """False for entities the addon CardDB and the art pipeline should leave out because
    they never appear in the released game."""
    return card["acquisition"] not in CLIENT_HIDDEN_ACQUISITIONS and card["id"] not in UNSUMMONED_TOKENS

# ---------------------------------------------------------------------------
# Schema
# ---------------------------------------------------------------------------

CARD_TYPES = ("MINION", "SPELL", "WEAPON", "HERO", "HERO_POWER", "ENCHANTMENT")
RARITIES = ("FREE", "COMMON", "RARE", "EPIC", "LEGENDARY")
CLASSES = (
    "NEUTRAL", "DRUID", "HUNTER", "MAGE", "PALADIN", "PRIEST",
    "ROGUE", "SHAMAN", "WARLOCK", "WARRIOR", "DREAM",
)
# Types that make up the collectible catalog (heroes are flagged collectible in
# the source data but are not cards).
CATALOG_TYPES = ("MINION", "SPELL", "WEAPON")

# How a card can be obtained: "standard" = normal Basic/Classic collectible (packs,
# crafting, starter collection); "reward_only" = imported promo collectible with its own
# special-reward rules (arena milestones); "quest" = collectible Naxxramas card earned only
# through quest rewards (never in packs, crafting, the trader or arena offers; usable in decks once
# owned); "none" = runtime entity that is never in a collection (heroes, hero
# powers, tokens, enchantments, generated cards); "unreleased" = card that is flagged
# collectible in the pinned build but was never obtainable in the released game (not in
# packs, crafting or rewards, not shown in the collection). It is set by a sourced
# override in data/cards/overrides/, never by the importer itself.
ACQUISITIONS = ("standard", "reward_only", "quest", "none", "unreleased")

# Field order is part of the format: it is the key order written to disk.
CARD_FIELDS = (
    "id", "dbf_id", "name", "text", "collectible", "set", "class", "rarity",
    "type", "cost", "attack", "health", "durability", "race", "mechanics",
    "faction", "spell_damage", "overload", "entourage", "hero_power",
    "play_requirements", "text_in_play", "targeting_arrow_text",
    "acquisition", "origin", "included_by",
)

CARD_DEFAULTS = {
    "dbf_id": None, "name": None, "text": None, "collectible": False, "set": None,
    "class": "NEUTRAL", "rarity": None, "type": None, "cost": None, "attack": None,
    "health": None, "durability": None, "race": None, "mechanics": [],
    "faction": None, "spell_damage": None, "overload": None, "entourage": [],
    "hero_power": None, "play_requirements": {}, "text_in_play": None,
    "targeting_arrow_text": None, "acquisition": None, "origin": "blizzard",
    "included_by": "set",
}

# Fields an override may change (identity fields are immutable).
OVERRIDABLE_FIELDS = tuple(
    f for f in CARD_FIELDS if f not in ("id", "dbf_id", "origin", "included_by")
)

CUSTOM_ID_PREFIX = "WR_"
CUSTOM_DBF_MIN = 10_000_000


class CardDataError(Exception):
    """Fatal, user-facing data problem (bad hash, bad override, ...)."""


# ---------------------------------------------------------------------------
# Hashing / IO helpers
# ---------------------------------------------------------------------------

def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(Path(path).read_bytes())


def canonical_json(obj) -> str:
    """Compact, key-sorted, ASCII-independent JSON used for hashing."""
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def content_hash(cards: list) -> str:
    return sha256_bytes(canonical_json(cards).encode("utf-8"))


def dumps_dataset(dataset: dict) -> str:
    """Stable on-disk serialisation (LF endings, trailing newline, no timestamps)."""
    return json.dumps(dataset, indent=2, ensure_ascii=False) + "\n"


def write_dataset(dataset: dict, path: Path) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(dumps_dataset(dataset))


def load_dataset(path: Path = NORMALIZED_PATH) -> dict:
    """Entry point for generators: returns the parsed cards.normalized.json."""
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


_TAG_RE = re.compile(r"</?[a-zA-Z][^>]*>")


def plain_text(text) -> str:
    """Card text with the <b>/<i> markup stripped (for searching)."""
    return _TAG_RE.sub("", text or "")


def cards_by_id(dataset: dict) -> dict:
    return {c["id"]: c for c in dataset["cards"]}


def verify_sources(source_dir: Path = SOURCE_DIR) -> dict:
    """Check the committed raw inputs against the pinned hashes. Fails loudly."""
    result = {}
    for key, pin in PINNED_SOURCES.items():
        if not pin["committed"]:
            continue
        path = Path(source_dir) / pin["file"]
        if not path.is_file():
            raise CardDataError(f"missing pinned source file: {path}")
        actual = sha256_file(path)
        if actual != pin["sha256"]:
            raise CardDataError(
                f"SHA-256 MISMATCH for {path.name}\n"
                f"  expected {pin['sha256']}\n"
                f"  actual   {actual}\n"
                f"Re-download from {pin['url']} or check for line-ending conversion "
                f"(.gitattributes must mark data/cards/source/* as -text)."
            )
        result[key] = {"file": pin["file"], "url": pin["url"], "sha256": actual,
                       "size": path.stat().st_size}
    return result


# ---------------------------------------------------------------------------
# Loading raw inputs
# ---------------------------------------------------------------------------

def load_manifest(path: Path) -> dict:
    """manifest-cards.csv: ``dbfId,CardID,flag,flag`` -> {CardID: dbfId}."""
    mapping = {}
    with open(path, "r", encoding="utf-8", newline="") as fh:
        for lineno, row in enumerate(csv.reader(fh), 1):
            if not row:
                continue
            if len(row) != 4:
                raise CardDataError(f"{path.name}:{lineno}: expected 4 columns, got {len(row)}")
            dbf, card_id = int(row[0]), row[1]
            if card_id in mapping:
                raise CardDataError(f"{path.name}:{lineno}: duplicate CardID {card_id}")
            mapping[card_id] = dbf
    return mapping


def load_hero_power_links(links_dir: Path = LINKS_DIR) -> dict:
    """data/cards/links/hero_powers.json -> {hero_id: hero_power_id}."""
    path = Path(links_dir) / "hero_powers.json"
    if not path.is_file():
        return {}
    doc = json.loads(path.read_text(encoding="utf-8"))
    links = {}
    for entry in doc["links"]:
        if entry["hero"] in links:
            raise CardDataError(f"{path.name}: duplicate hero {entry['hero']}")
        links[entry["hero"]] = entry["hero_power"]
    return links


# ---------------------------------------------------------------------------
# Normalisation
# ---------------------------------------------------------------------------

def _opt(raw: dict, key: str):
    return raw.get(key)


def acquisition_of(raw: dict) -> str:
    if raw["id"] in REWARD_ONLY_IDS:
        return "reward_only"
    if raw.get("collectible") and raw["type"] in CATALOG_TYPES:
        if raw["set"] in STANDARD_SETS:
            return "standard"
        if raw["set"] in QUEST_SETS:
            return "quest"
    return "none"


def normalize_card(raw: dict, dbf_ids: dict, hero_links: dict, included_by: str) -> dict:
    card = {
        "id": raw["id"],
        "dbf_id": dbf_ids.get(raw["id"]),
        "name": raw["name"],
        "text": _opt(raw, "text"),
        "collectible": bool(raw.get("collectible", False)),
        "set": raw["set"],
        "class": raw.get("playerClass") or "NEUTRAL",
        "rarity": _opt(raw, "rarity"),
        "type": raw["type"],
        "cost": _opt(raw, "cost"),
        "attack": _opt(raw, "attack"),
        "health": _opt(raw, "health"),
        "durability": _opt(raw, "durability"),
        "race": _opt(raw, "race"),
        "mechanics": sorted(raw.get("mechanics") or []),
        "faction": _opt(raw, "faction"),
        "spell_damage": _opt(raw, "spellDamage"),
        "overload": _opt(raw, "overload"),
        # Order of the entourage list is source order (it is a pool, but we keep it).
        "entourage": list(raw.get("entourage") or []),
        "hero_power": hero_links.get(raw["id"]),
        "play_requirements": dict(sorted((raw.get("playRequirements") or {}).items())),
        "text_in_play": _opt(raw, "textInPlay"),
        "targeting_arrow_text": _opt(raw, "targetingArrowText"),
        "acquisition": acquisition_of(raw),
        "origin": "blizzard",
        "included_by": included_by,
    }
    assert tuple(card) == CARD_FIELDS, "CARD_FIELDS out of sync with normalize_card"
    return card


def select_entities(raw_cards: list, hero_links: dict):
    """Pick the entities to import.

    Returns (by_id, selected {id: reason}, excluded_list).
    All CORE/EXPERT1/NAXX entities are kept (collectible or not: heroes, hero powers,
    tokens, enchantments, generated cards), except the REMOVED_IDS. The REWARD_ONLY_IDS cards are kept with
    reason "reward". Entities of other sets are kept only if a kept card references
    them (entourage / hero power / IMPLICIT_REFERENCES), transitively.
    """
    by_id = {}
    for raw in raw_cards:
        if raw["id"] in by_id:
            raise CardDataError(f"duplicate id in source cards.json: {raw['id']}")
        by_id[raw["id"]] = raw

    for cid in REMOVED_IDS:
        if cid not in by_id:
            raise CardDataError(f"removed card {cid} not found in the source")
    selected = {cid: "set" for cid, raw in by_id.items() if raw["set"] in INCLUDED_SETS and cid not in REMOVED_IDS}
    for cid in REWARD_ONLY_IDS:
        if cid not in by_id:
            raise CardDataError(f"reward-only card {cid} not found in the source")
        if cid in selected:
            raise CardDataError(f"reward-only card {cid} is already in an included set")
        selected[cid] = "reward"

    def raw_refs(raw):
        refs = list(raw.get("entourage") or [])
        hp = hero_links.get(raw["id"])
        if hp:
            refs.append(hp)
        refs.extend(IMPLICIT_REFERENCES.get(raw["id"], ()))
        for ref in refs:
            if ref in REMOVED_IDS:
                raise CardDataError(f"{raw['id']} references the removed card {ref}")
        return refs

    queue = list(selected)
    while queue:
        cid = queue.pop()
        for ref in raw_refs(by_id[cid]):
            if ref in by_id and ref not in selected:
                selected[ref] = "reference"
                queue.append(ref)
    excluded = sorted(cid for cid in by_id if cid not in selected)
    return by_id, selected, excluded


# ---------------------------------------------------------------------------
# Overrides and custom cards
# ---------------------------------------------------------------------------

_URL_RE = re.compile(r"^https?://\S+$")


def load_overrides(overrides_dir: Path = OVERRIDES_DIR) -> list:
    """Read ``*.json`` override files (sorted by filename). Returns a flat list of
    override records, each annotated with the file it came from."""
    overrides_dir = Path(overrides_dir)
    records = []
    if not overrides_dir.is_dir():
        return records
    for path in sorted(overrides_dir.glob("*.json")):
        doc = json.loads(path.read_text(encoding="utf-8"))
        if doc.get("schema_version") != 1:
            raise CardDataError(f"{path.name}: unsupported override schema_version {doc.get('schema_version')!r}")
        for i, rec in enumerate(doc.get("overrides", [])):
            where = f"{path.name}[{i}]"
            for key in ("card_id", "fields", "source_url", "reason"):
                if not rec.get(key):
                    raise CardDataError(f"{where}: missing required key {key!r}")
            if not _URL_RE.match(rec["source_url"]):
                raise CardDataError(f"{where}: source_url must be an http(s) URL")
            bad = [f for f in rec["fields"] if f not in OVERRIDABLE_FIELDS]
            if bad:
                raise CardDataError(f"{where}: fields not overridable: {bad}")
            bad = [f for f in rec.get("expect", {}) if f not in rec["fields"]]
            if bad:
                raise CardDataError(f"{where}: 'expect' names fields not being overridden: {bad}")
            records.append({**rec, "_file": path.name, "_file_sha256": sha256_file(path)})
    return records


def apply_overrides(cards: dict, records: list) -> list:
    """Apply override records in place to ``cards`` ({id: card}). Returns a
    summary list for the dataset metadata."""
    seen = set()
    applied = []
    for rec in records:
        cid = rec["card_id"]
        if cid not in cards:
            raise CardDataError(f"{rec['_file']}: override targets unknown card {cid}")
        for field, value in rec["fields"].items():
            if (cid, field) in seen:
                raise CardDataError(f"{rec['_file']}: field {field} of {cid} overridden twice")
            seen.add((cid, field))
            if field in rec.get("expect", {}) and cards[cid][field] != rec["expect"][field]:
                raise CardDataError(
                    f"{rec['_file']}: stale override for {cid}.{field}: expected "
                    f"{rec['expect'][field]!r}, dataset has {cards[cid][field]!r}"
                )
            cards[cid][field] = value
        applied.append({
            "file": rec["_file"], "file_sha256": rec["_file_sha256"], "card_id": cid,
            "fields": sorted(rec["fields"]), "source_url": rec["source_url"],
        })
    return applied


def load_custom_cards(custom_dir: Path = CUSTOM_DIR) -> tuple:
    """Read ``data/cards/custom/*.json``. Returns (cards, file_summaries).

    Custom ids must start with WR_ and custom dbf ids must be >= 10,000,000, so
    they can never collide with Blizzard ids.
    """
    custom_dir = Path(custom_dir)
    cards, files = [], []
    if not custom_dir.is_dir():
        return cards, files
    for path in sorted(custom_dir.glob("*.json")):
        doc = json.loads(path.read_text(encoding="utf-8"))
        if doc.get("schema_version") != 1:
            raise CardDataError(f"{path.name}: unsupported custom schema_version {doc.get('schema_version')!r}")
        for i, raw in enumerate(doc.get("cards", [])):
            where = f"{path.name}[{i}]"
            unknown = [k for k in raw if k not in CARD_FIELDS]
            if unknown:
                raise CardDataError(f"{where}: unknown fields {unknown}")
            card = {**CARD_DEFAULTS, **raw}
            if not str(card.get("id", "")).startswith(CUSTOM_ID_PREFIX):
                raise CardDataError(f"{where}: custom card id must start with {CUSTOM_ID_PREFIX!r}")
            if not isinstance(card["dbf_id"], int) or card["dbf_id"] < CUSTOM_DBF_MIN:
                raise CardDataError(f"{where}: custom dbf_id must be an int >= {CUSTOM_DBF_MIN}")
            card["origin"] = "custom"
            card["included_by"] = "custom"
            if card["acquisition"] is None:
                card["acquisition"] = "standard" if card["collectible"] and card["type"] in CATALOG_TYPES else "none"
            card["mechanics"] = sorted(card["mechanics"])
            card["play_requirements"] = dict(sorted(card["play_requirements"].items()))
            cards.append({k: card[k] for k in CARD_FIELDS})
        files.append({"file": path.name, "file_sha256": sha256_file(path)})
    return cards, files


# ---------------------------------------------------------------------------
# Dataset construction
# ---------------------------------------------------------------------------

def build_dataset(source_dir: Path = SOURCE_DIR, overrides_dir: Path = OVERRIDES_DIR,
                  custom_dir: Path = CUSTOM_DIR, links_dir: Path = LINKS_DIR) -> tuple:
    """Build the normalized dataset from the pinned inputs.

    Returns (dataset, report) where report is human-oriented information
    (excluded entities, referenced-kept entities, missing dbf ids) for the CLI.
    """
    source_dir = Path(source_dir)
    source_hashes = verify_sources(source_dir)
    raw_cards = json.loads((source_dir / PINNED_SOURCES["cards_json"]["file"]).read_text(encoding="utf-8"))
    dbf_ids = load_manifest(source_dir / PINNED_SOURCES["manifest_cards_csv"]["file"])
    hero_links = load_hero_power_links(links_dir)

    by_id, selected, excluded = select_entities(raw_cards, hero_links)
    cards = {cid: normalize_card(by_id[cid], dbf_ids, hero_links, why) for cid, why in selected.items()}

    applied = apply_overrides(cards, load_overrides(overrides_dir))

    custom_cards, custom_files = load_custom_cards(custom_dir)
    for card in custom_cards:
        if card["id"] in cards:
            raise CardDataError(f"custom card id collides with existing id: {card['id']}")
        cards[card["id"]] = card

    card_list = [cards[cid] for cid in sorted(cards)]

    excluded_by_set = {}
    for cid in excluded:
        excluded_by_set.setdefault(by_id[cid]["set"], []).append(cid)
    excluded_collectible = [cid for cid in excluded if by_id[cid].get("collectible")]
    reward_only = sorted(cid for cid, c in cards.items() if c["acquisition"] == "reward_only")

    dataset = {
        "dataset_id": DATASET_ID,
        "schema_version": SCHEMA_VERSION,
        "source": {
            **SOURCE_INFO,
            "files": source_hashes,
            "cross_check": {k: v for k, v in PINNED_SOURCES["carddefs_xml"].items()
                            if k not in ("file", "committed")},
        },
        "sets_included": list(INCLUDED_SETS),
        "hero_power_links": {"file": "links/hero_powers.json",
                             "sha256": sha256_file(Path(links_dir) / "hero_powers.json")}
        if (Path(links_dir) / "hero_powers.json").is_file() else None,
        "overrides_applied": applied,
        "custom_files": custom_files,
        "excluded": {
            "counts_by_set": {s: len(ids) for s, ids in sorted(excluded_by_set.items())},
            "collectible_ids": excluded_collectible,
        },
        "reward_only_ids": reward_only,
        "card_count": len(card_list),
        "content_hash": content_hash(card_list),
        "cards": card_list,
    }
    report = {
        "excluded_by_set": {s: ids for s, ids in sorted(excluded_by_set.items())},
        "excluded_names": {cid: by_id[cid]["name"] for cid in excluded},
        "excluded_collectible": excluded_collectible,
        "kept_by_reference": sorted(cid for cid, why in selected.items() if why == "reference"),
        "reward_only": {cid: cards[cid]["name"] + " (" + cards[cid]["set"] + ")" for cid in reward_only},
        "quest": {cid: cards[cid]["name"] + " (" + cards[cid]["set"] + ")"
                  for cid in sorted(cards) if cards[cid]["acquisition"] == "quest"},
        "runtime_by_set": {s: sum(1 for c in cards.values() if c["set"] == s and c["acquisition"] == "none")
                           for s in sorted({c["set"] for c in cards.values()})},
        "missing_dbf": sorted(cid for cid in cards if cards[cid]["dbf_id"] is None
                              and cards[cid]["origin"] == "blizzard"),
        "manifest_ids_not_in_json": sorted(set(dbf_ids) - set(by_id)),
        "source_entity_count": len(raw_cards),
    }
    return dataset, report
