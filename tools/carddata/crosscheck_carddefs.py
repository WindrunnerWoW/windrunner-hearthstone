#!/usr/bin/env python3
"""Cross-check the normalized dataset against hsdata's raw CardDefs.xml (build 6024).

CardDefs.xml is ~1.9 MB of 12-locale text and is deliberately NOT stored in the
repository. Download it (URL and sha256 in data/cards/source/PROVENANCE.md), then:

    python tools/carddata/crosscheck_carddefs.py path/to/CardDefs.xml

It checks the file hash, then compares every entity that the XML and the JSON
both describe, independently of the JSON-derived pipeline:
name, English text, cost/attack/health/durability, mechanics flags, entourage
lists, collectible flag, and the numeric enums (set/type/rarity/class/race),
which must map consistently onto the string names used in the dataset. The
Lord Jaraxxus HERO_POWER tag is resolved through the dbf manifest and compared
with data/cards/links/hero_powers.json.

Exit status 0 = consistent, 1 = differences found, 2 = bad input.
"""

from __future__ import annotations

import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cardlib  # noqa: E402

INT_TAGS = {"COST": "cost", "ATK": "attack", "HEALTH": "health", "DURABILITY": "durability",
            "SPELLPOWER": "spell_damage", "OVERLOAD": "overload"}
# XML boolean tag name -> JSON mechanic name (identical here except where noted).
MECHANIC_TAGS = ["DEATHRATTLE", "TAUNT", "BATTLECRY", "FREEZE", "WINDFURY", "TAG_ONE_TURN_EFFECT",
                 "ImmuneToSpellpower", "CHARGE", "SILENCE", "STEALTH", "COMBO", "AURA", "SECRET",
                 "DIVINE_SHIELD", "MORPH", "ENRAGED", "POISONOUS", "ADJACENT_BUFF", "CANT_ATTACK",
                 "CANT_BE_TARGETED_BY_SPELLS", "CANT_BE_TARGETED_BY_HERO_POWERS",
                 "AI_MUST_PLAY", "InvisibleDeathrattle"]  # the last two appear on Naxxramas entities
ENUM_TAGS = {"CARD_SET": "set", "CARDTYPE": "type", "RARITY": "rarity", "CLASS": "class", "CARDRACE": "race"}


def parse_entities(path: Path) -> dict:
    root = ET.parse(path).getroot()
    out = {}
    for ent in root.findall("Entity"):
        tags, loc = {}, {}
        for t in ent.findall("Tag"):
            name = t.get("name")
            if t.get("type") == "LocString":
                en = t.find("enUS")
                loc[name] = en.text if en is not None else None
            elif t.get("type") == "Int":
                tags[name] = int(t.get("value"))
        out[ent.get("CardID")] = {
            "tags": tags, "loc": loc,
            "entourage": [e.get("cardID") or e.get("CardID") or e.text for e in ent.findall("EntourageCard")],
        }
    return out


def main(argv=None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if len(args) != 1:
        print(__doc__)
        return 2
    xml_path = Path(args[0])
    pin = cardlib.PINNED_SOURCES["carddefs_xml"]
    actual = cardlib.sha256_file(xml_path)
    if actual != pin["sha256"]:
        print(f"SHA-256 MISMATCH for {xml_path}\n  expected {pin['sha256']}\n  actual   {actual}", file=sys.stderr)
        return 2
    print(f"CardDefs.xml sha256 ok: {actual}")

    dataset = cardlib.load_dataset()
    cards = {c["id"]: c for c in dataset["cards"] if c["origin"] == "blizzard"}
    ents = parse_entities(xml_path)
    problems, elite_notes, ws_notes = [], [], []

    # Entities kept in the dataset must exist in the XML; report XML-only ones (excluded sets).
    missing_in_xml = sorted(set(cards) - set(ents))
    if missing_in_xml:
        problems.append(f"dataset cards missing from XML: {missing_in_xml}")
    print(f"XML entities {len(ents)}; dataset Blizzard cards {len(cards)}; "
          f"XML-only (excluded sets) {len(set(ents) - set(cards))}")

    enum_maps = {t: {} for t in ENUM_TAGS}
    for cid, card in sorted(cards.items()):
        ent = ents.get(cid)
        if ent is None:
            continue
        tags, loc = ent["tags"], ent["loc"]
        if loc.get("CARDNAME") != card["name"]:
            problems.append(f"{cid}: name {card['name']!r} vs XML {loc.get('CARDNAME')!r}")
        xt_text, jt_text = loc.get("CARDTEXT_INHAND") or None, card["text"] or None
        if xt_text != jt_text and (xt_text or "").strip() == (jt_text or "").strip():
            ws_notes.append(cid)  # HearthstoneJSON quirk: surrounding whitespace only
        elif xt_text != jt_text:
            problems.append(f"{cid}: text {card['text']!r} vs XML {loc.get('CARDTEXT_INHAND')!r}")
        for xt, field in INT_TAGS.items():
            # The XML omits zero-valued tags, so an absent tag matches 0 (or absent).
            xv = tags.get(xt)
            if xv != card[field] and not (xv is None and card[field] == 0):
                problems.append(f"{cid}: {field} {card[field]!r} vs XML {xv!r}")
        xml_mech = sorted(m for m in MECHANIC_TAGS if tags.get(m))
        if xml_mech != card["mechanics"]:
            problems.append(f"{cid}: mechanics {card['mechanics']} vs XML {xml_mech}")
        if bool(tags.get("COLLECTIBLE")) != card["collectible"]:
            problems.append(f"{cid}: collectible {card['collectible']} vs XML {bool(tags.get('COLLECTIBLE'))}")
        # ELITE is informational: two non-collectible legendary-rarity entities lack the tag.
        if bool(tags.get("ELITE")) != (card["rarity"] == "LEGENDARY"):
            elite_notes.append(cid)
        if ent["entourage"] != card["entourage"]:
            problems.append(f"{cid}: entourage {card['entourage']} vs XML {ent['entourage']}")
        for xt, field in ENUM_TAGS.items():
            xv = tags.get(xt)
            want = card[field] if field != "class" or card[field] != "NEUTRAL" else None
            prev = enum_maps[xt].setdefault(xv, want)
            if prev != want:
                problems.append(f"{cid}: enum {xt}={xv} maps to {want!r} but earlier cards map it to {prev!r}")

    # Enum maps must also be injective (different names never share a number).
    for xt, mapping in enum_maps.items():
        names = [v for v in mapping.values()]
        if len(names) != len(set(names)):
            problems.append(f"enum {xt} is not one-to-one: {mapping}")
        print(f"  {xt:9s} mapping: {dict(sorted(mapping.items(), key=lambda kv: (kv[0] is None, kv[0])))}")

    # Hero power link that only the XML knows.
    manifest = cardlib.load_manifest(cardlib.SOURCE_DIR / cardlib.PINNED_SOURCES["manifest_cards_csv"]["file"])
    dbf_to_id = {v: k for k, v in manifest.items()}
    for cid, ent in ents.items():
        hp = ent["tags"].get("HERO_POWER")
        if hp is not None:
            linked = dbf_to_id.get(hp)
            ours = cards.get(cid, {}).get("hero_power")
            print(f"  HERO_POWER tag: {cid} -> dbf {hp} -> {linked}; dataset link {ours}")
            if linked != ours:
                problems.append(f"{cid}: hero power link {ours!r} vs XML {linked!r}")

    if problems:
        print(f"\n{len(problems)} DIFFERENCE(S):")
        for p in problems:
            print(f"  {p}")
        return 1
    print("\nCROSS-CHECK PASSED: dataset agrees with CardDefs.xml on every compared field")
    return 0


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())
