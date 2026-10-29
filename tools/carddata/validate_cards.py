#!/usr/bin/env python3
"""Validate data/cards/cards.normalized.json and print a summary.

Usage:
    python tools/carddata/validate_cards.py [path/to/cards.normalized.json]

Exit status: 0 = no hard errors (warnings allowed), 1 = hard errors, 2 = unreadable input.
"""

from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cardlib  # noqa: E402
import snapshot_expectations as exp  # noqa: E402

STAT_FIELDS = ("cost", "attack", "health", "durability", "spell_damage", "overload")
INT_FIELDS = STAT_FIELDS + ("dbf_id",)


class Report:
    def __init__(self):
        self.errors, self.warnings, self.notes = [], [], []

    def error(self, msg):
        self.errors.append(msg)

    def warn(self, msg):
        self.warnings.append(msg)

    def note(self, msg):
        self.notes.append(msg)


def _tags_balanced(text: str) -> bool:
    for tag in ("b", "i"):
        if text.count(f"<{tag}>") != text.count(f"</{tag}>"):
            return False
    return True


def check_schema(cards, rep):
    for c in cards:
        cid = c.get("id", "<no id>")
        if tuple(c) != cardlib.CARD_FIELDS:
            rep.error(f"{cid}: field set/order differs from the schema: {list(c)}")
            continue
        if not c["name"]:
            rep.error(f"{cid}: empty name")
        for f in INT_FIELDS:
            v = c[f]
            if v is not None and (isinstance(v, bool) or not isinstance(v, int)):
                rep.error(f"{cid}: {f} is not an integer: {v!r}")
        if c["type"] not in cardlib.CARD_TYPES:
            rep.error(f"{cid}: unknown type {c['type']!r}")
        if c["rarity"] is not None and c["rarity"] not in cardlib.RARITIES:
            rep.error(f"{cid}: unknown rarity {c['rarity']!r}")
        if c["acquisition"] not in cardlib.ACQUISITIONS:
            rep.error(f"{cid}: unknown acquisition {c['acquisition']!r}")
        if c["class"] not in cardlib.CLASSES:
            rep.error(f"{cid}: unknown class {c['class']!r}")
        if c["mechanics"] != sorted(c["mechanics"]):
            rep.error(f"{cid}: mechanics not sorted")
        if c["text"] and not _tags_balanced(c["text"]):
            rep.warn(f"{cid}: unbalanced <b>/<i> markup in text")


def check_identity(cards, rep):
    ids, dbfs = Counter(c["id"] for c in cards), Counter(c["dbf_id"] for c in cards if c["dbf_id"] is not None)
    for cid, n in ids.items():
        if n > 1:
            rep.error(f"duplicate id: {cid} x{n}")
    for dbf, n in dbfs.items():
        if n > 1:
            owners = [c["id"] for c in cards if c["dbf_id"] == dbf]
            rep.error(f"duplicate dbf_id {dbf}: {owners}")
    if [c["id"] for c in cards] != sorted(c["id"] for c in cards):
        rep.error("cards are not sorted by id")
    for c in cards:
        custom = c["origin"] == "custom"
        if custom:
            if not c["id"].startswith(cardlib.CUSTOM_ID_PREFIX):
                rep.error(f"{c['id']}: custom card id must start with {cardlib.CUSTOM_ID_PREFIX}")
            if c["dbf_id"] is None or c["dbf_id"] < cardlib.CUSTOM_DBF_MIN:
                rep.error(f"{c['id']}: custom dbf_id must be >= {cardlib.CUSTOM_DBF_MIN}")
        else:
            if c["id"].startswith(cardlib.CUSTOM_ID_PREFIX):
                rep.error(f"{c['id']}: Blizzard card uses the reserved {cardlib.CUSTOM_ID_PREFIX} prefix")
            if c["dbf_id"] is not None and c["dbf_id"] >= cardlib.CUSTOM_DBF_MIN:
                rep.error(f"{c['id']}: Blizzard dbf_id {c['dbf_id']} is in the reserved custom range")
            if c["dbf_id"] is None:
                rep.warn(f"{c['id']} ({c['name']}): no dbf_id (not in manifest-cards.csv)")


def check_stats(cards, rep):
    for c in cards:
        cid, t = f"{c['id']} ({c['name']})", c["type"]
        for f in STAT_FIELDS:
            v = c[f]
            if v is not None and v < 0:
                rep.error(f"{cid}: negative {f} = {v}")
        collectible_catalog = c["collectible"] and t in cardlib.CATALOG_TYPES
        if t == "MINION":
            for f in ("attack", "health"):
                if c[f] is None:
                    rep.error(f"{cid}: minion without {f}")
            if c["health"] is not None and c["health"] <= 0:
                rep.error(f"{cid}: minion with non-positive health {c['health']}")
            if c["durability"] is not None:
                rep.error(f"{cid}: minion has durability")
            if c["cost"] is None:
                (rep.error if collectible_catalog else rep.warn)(f"{cid}: minion without cost")
        elif t == "WEAPON":
            for f in ("attack", "durability"):
                if c[f] is None:
                    rep.error(f"{cid}: weapon without {f}")
            if c["durability"] is not None and c["durability"] <= 0:
                rep.error(f"{cid}: weapon with non-positive durability")
            if c["health"] is not None:
                rep.error(f"{cid}: weapon has health")
            if c["cost"] is None:
                (rep.error if collectible_catalog else rep.warn)(f"{cid}: weapon without cost")
        elif t == "SPELL":
            for f in ("attack", "health", "durability"):
                if c[f] is not None:
                    rep.error(f"{cid}: spell has {f}")
            if c["cost"] is None:
                (rep.error if collectible_catalog else rep.warn)(f"{cid}: spell without cost")
        elif t == "HERO":
            if c["health"] is None or c["health"] <= 0:
                rep.error(f"{cid}: hero without positive health")
        elif t == "HERO_POWER":
            if c["cost"] is None:
                rep.error(f"{cid}: hero power without cost")
            for f in ("attack", "health", "durability"):
                if c[f] is not None:
                    rep.error(f"{cid}: hero power has {f}")
        elif t == "ENCHANTMENT":
            for f in ("attack", "health", "durability"):
                if c[f] is not None:
                    rep.warn(f"{cid}: enchantment carries {f}")


def check_catalog(cards, rep, overrides_applied=()):
    for c in cards:
        cid = f"{c['id']} ({c['name']})"
        acq = c["acquisition"]
        if c["origin"] == "blizzard" and c["collectible"] and c["set"] not in cardlib.INCLUDED_SETS                 and acq != "reward_only":
            rep.error(f"{cid}: collectible card outside {cardlib.INCLUDED_SETS} (set {c['set']}) "
                      "that is not reward_only")
        is_catalog = c["collectible"] and c["type"] in cardlib.CATALOG_TYPES
        if acq in ("standard", "reward_only", "quest") and not is_catalog:
            rep.error(f"{cid}: acquisition {acq} on a non-collectible/non-catalog entity")
        if is_catalog and acq == "none":
            rep.error(f"{cid}: collectible catalog card with acquisition none")
        if acq == "standard" and c["origin"] == "blizzard" and c["set"] not in cardlib.STANDARD_SETS:
            rep.error(f"{cid}: standard acquisition outside {cardlib.STANDARD_SETS}")
        if acq == "quest" and c["origin"] == "blizzard" and c["set"] not in cardlib.QUEST_SETS:
            rep.error(f"{cid}: quest acquisition outside {cardlib.QUEST_SETS}")
        if is_catalog and c["origin"] == "blizzard" and c["set"] in cardlib.QUEST_SETS and acq != "quest":
            rep.error(f"{cid}: collectible {c['set']} card must be acquisition quest, is {acq}")
        if acq == "quest" and c["id"] not in exp.EXPECTED_QUEST and c["origin"] == "blizzard":
            rep.error(f"{cid}: quest but not in EXPECTED_QUEST")
        if acq == "reward_only" and c["id"] not in cardlib.REWARD_ONLY_IDS and c["origin"] == "blizzard":
            rep.error(f"{cid}: reward_only but not in REWARD_ONLY_IDS")
        if c["id"] in cardlib.REWARD_ONLY_IDS and acq != "reward_only":
            rep.error(f"{cid}: must be reward_only, is {acq}")
        if c["id"] in cardlib.UNSUMMONED_TOKENS:
            if c["collectible"] or acq != "none":
                rep.error(f"{cid}: listed in UNSUMMONED_TOKENS but collectible or acquisition {acq}")
            for o in cards:
                # Naxxramas cards name their own set's tokens (NAX4_03 "Skeleton"), not this one, so only the
                # entourage lists of NAXX cards count.
                naxx = o["set"] in cardlib.QUEST_SETS
                if o["id"] != c["id"] and (c["id"] in o["entourage"]
                                           or (not naxx and c["name"].lower() in (o["text"] or "").lower())):
                    rep.error(f"{cid}: listed in UNSUMMONED_TOKENS but referenced by {o['id']} ({o['name']})")
        if acq == "unreleased":
            if c["type"] not in cardlib.CATALOG_TYPES:
                rep.error(f"{cid}: acquisition unreleased on a non-catalog entity")
            if c["id"] not in exp.EXPECTED_UNRELEASED:
                rep.error(f"{cid}: unreleased but not in EXPECTED_UNRELEASED")
            if not any(o["card_id"] == c["id"] and "acquisition" in o["fields"]
                       for o in overrides_applied):
                rep.error(f"{cid}: unreleased without a sourced override in data/cards/overrides/")
        if c["collectible"] and c["type"] in cardlib.CATALOG_TYPES and c["rarity"] is None:
            rep.error(f"{cid}: collectible card without rarity")
        if c["collectible"] and c["type"] in ("HERO_POWER", "ENCHANTMENT"):
            rep.error(f"{cid}: {c['type']} flagged collectible")
        if c["rarity"] == "FREE" and c["set"] == "EXPERT1":
            rep.warn(f"{cid}: FREE rarity in the Classic set")


def check_references(cards, rep):
    by_id = {c["id"]: c for c in cards}
    referenced_powers = set()
    for c in cards:
        cid = f"{c['id']} ({c['name']})"
        for ref in c["entourage"]:
            if ref not in by_id:
                rep.error(f"{cid}: entourage references missing card {ref}")
        hp = c["hero_power"]
        if hp is not None:
            if c["type"] != "HERO":
                rep.error(f"{cid}: hero_power set on a non-hero")
            elif hp not in by_id:
                rep.error(f"{cid}: hero power {hp} is missing")
            else:
                referenced_powers.add(hp)
                if by_id[hp]["type"] != "HERO_POWER":
                    rep.error(f"{cid}: hero_power {hp} is not a HERO_POWER")
                elif by_id[hp]["class"] != c["class"] and c["set"] not in cardlib.QUEST_SETS:
                    rep.warn(f"{cid}: hero power {hp} class {by_id[hp]['class']} != hero class {c['class']}")
        elif c["type"] == "HERO" and c["origin"] == "blizzard":
            rep.error(f"{cid}: hero has no hero_power link")
    unreferenced = sorted(c["id"] for c in cards if c["type"] == "HERO_POWER" and c["id"] not in referenced_powers)
    if unreferenced:
        rep.note("hero powers not linked from any hero (granted by card effects, e.g. Shadowform): "
                 + ", ".join(unreferenced))


def run_spot_checks(by_id, rep):
    results = []
    for chk in exp.SPOT_CHECKS:
        card, problems = by_id.get(chk["id"]), []
        if card is None:
            problems.append("card missing")
        else:
            if card["name"] != chk["name"]:
                problems.append(f"name {card['name']!r} != {chk['name']!r}")
            for f, want in chk["expect"].items():
                if card[f] != want:
                    problems.append(f"{f}={card[f]!r}, expected {want!r}")
            plain = cardlib.plain_text(card["text"])
            for s in chk.get("text_has", []):
                if s not in plain:
                    problems.append(f"text lacks {s!r}: {plain!r}")
            for s in chk.get("text_lacks", []):
                if s in plain:
                    problems.append(f"text contains forbidden {s!r}: {plain!r}")
        results.append((chk, card, problems))
        if problems:
            rep.error(f"SPOT CHECK {chk['id']} {chk['name']}: " + "; ".join(problems))
    for cid in exp.MUST_BE_ABSENT:
        ok = cid not in by_id
        results.append(({"id": cid, "name": "(must be absent)", "expect": {}}, None, [] if ok else ["present"]))
        if not ok:
            rep.error(f"SPOT CHECK {cid} must be absent (post-snapshot card leaked in)")
    return results


def summarize(dataset, cards, rep):
    print(f"dataset {dataset['dataset_id']}  schema v{dataset['schema_version']}  "
          f"build {dataset['source']['client_build']}  cards {len(cards)}")
    print(f"content_hash {dataset['content_hash']}")
    print("\nEntities by set / type:")
    types = cardlib.CARD_TYPES
    print(f"  {'set':10s}" + "".join(f"{t:>12s}" for t in types) + f"{'total':>8s}")
    for s in sorted({c["set"] for c in cards}):
        row = Counter(c["type"] for c in cards if c["set"] == s)
        print(f"  {s:10s}" + "".join(f"{row.get(t, 0):>12d}" for t in types) + f"{sum(row.values()):>8d}")

    cat = [c for c in cards if c["acquisition"] == "standard"]
    reward = [c for c in cards if c["acquisition"] == "reward_only"]
    quest = [c for c in cards if c["acquisition"] == "quest"]
    heroes = [c for c in cards if c["collectible"] and c["type"] == "HERO"]
    print("\nStandard collectible catalog (acquisition=standard: packs, crafting) by set:")
    for s in sorted({c["set"] for c in cat}):
        n = sum(1 for c in cat if c["set"] == s)
        print(f"  {s:10s}{n:>5d}")
    print(f"  {'TOTAL':10s}{len(cat):>5d}   (+ {len(heroes)} collectible hero entities, not counted as cards)")
    print(f"Reward-only cards (imported, out of packs/crafting; quest rewards later): {len(reward)}")
    for c in reward:
        print(f"  {c['id']:10s} {c['name']:24s} set {c['set']:7s} {c['rarity']:10s} {c['type']}")
    print(f"Quest-reward cards (collectible Naxxramas; out of packs/crafting/trader/arena, earned through quests): "
          f"{len(quest)}")
    for c in quest:
        print(f"  {c['id']:10s} {c['name']:24s} set {c['set']:7s} {c['rarity']:10s} {c['type']}")
    unreleased = [c for c in cards if c["acquisition"] == "unreleased"]
    print(f"Unreleased cards (flagged collectible in the build, never obtainable; sourced override): {len(unreleased)}")
    for c in unreleased:
        print(f"  {c['id']:10s} {c['name']:24s} set {c['set']:7s} {c['rarity']:10s} {c['type']}")
    print("Acquisition of all entities:", dict(sorted(Counter(c["acquisition"] for c in cards).items())))
    print("\nStandard catalog by set and rarity:")
    rar = sorted({c["rarity"] for c in cat}, key=cardlib.RARITIES.index)
    print(f"  {'set':10s}" + "".join(f"{r:>11s}" for r in rar))
    for s in sorted({c["set"] for c in cat}):
        row = Counter(c["rarity"] for c in cat if c["set"] == s)
        print(f"  {s:10s}" + "".join(f"{row.get(r, 0):>11d}" for r in rar))
    print("\nStandard catalog by class and set:")
    sets = sorted({c["set"] for c in cat})
    print(f"  {'class':10s}" + "".join(f"{s:>9s}" for s in sets) + f"{'total':>7s}")
    for cl in cardlib.CLASSES:
        row = Counter(c["set"] for c in cat if c["class"] == cl)
        if row:
            print(f"  {cl:10s}" + "".join(f"{row.get(s, 0):>9d}" for s in sets) + f"{sum(row.values()):>7d}")
    print("\nStandard catalog by type:", dict(sorted(Counter(c["type"] for c in cat).items())))
    print("All entities by rarity:", dict(sorted(Counter(str(c["rarity"]) for c in cards).items())))
    noncoll = [c for c in cards if c["acquisition"] == "none"]
    print(f"Runtime entities (acquisition=none) kept: {len(noncoll)} "
          f"{dict(sorted(Counter(c['type'] for c in noncoll).items()))}")
    print(f"Custom cards: {sum(1 for c in cards if c['origin'] == 'custom')}   "
          f"Overrides applied: {len(dataset['overrides_applied'])}")

    # Expected snapshot totals are part of the validation.
    for s, want in exp.EXPECTED_CATALOG_TOTALS.items():
        got = sum(1 for c in cat if c["set"] == s and c["origin"] == "blizzard")
        if got != want:
            rep.error(f"catalog total for {s}: {got}, expected {want}")
    if sorted(c["id"] for c in reward if c["origin"] == "blizzard") != sorted(cardlib.REWARD_ONLY_IDS):
        rep.error(f"reward_only set differs from REWARD_ONLY_IDS: {[c['id'] for c in reward]}")
    if sorted(c["id"] for c in quest) != sorted(exp.EXPECTED_QUEST):
        rep.error(f"quest set differs from EXPECTED_QUEST: {[c['id'] for c in quest]}")
    if sorted(c["id"] for c in unreleased) != sorted(exp.EXPECTED_UNRELEASED):
        rep.error(f"unreleased set differs from EXPECTED_UNRELEASED: {[c['id'] for c in unreleased]}")
    if len(heroes) != exp.EXPECTED_COLLECTIBLE_HEROES:
        rep.error(f"collectible heroes: {len(heroes)}, expected {exp.EXPECTED_COLLECTIBLE_HEROES}")


def validate(dataset):
    """Run every check. Returns (Report, spot_check_results)."""
    rep, cards = Report(), dataset["cards"]
    if dataset.get("card_count") != len(cards):
        rep.error(f"card_count {dataset.get('card_count')} != actual {len(cards)}")
    if dataset.get("content_hash") != cardlib.content_hash(cards):
        rep.error("content_hash does not match the card list (file edited by hand?)")
    check_schema(cards, rep)
    check_identity(cards, rep)
    check_stats(cards, rep)
    check_catalog(cards, rep, dataset.get("overrides_applied", []))
    check_references(cards, rep)
    spots = run_spot_checks({c["id"]: c for c in cards}, rep)
    return rep, spots


def main(argv=None) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    args = sys.argv[1:] if argv is None else argv
    path = Path(args[0]) if args else cardlib.NORMALIZED_PATH
    try:
        dataset = cardlib.load_dataset(path)
    except (OSError, ValueError) as exc:
        print(f"cannot read {path}: {exc}", file=sys.stderr)
        return 2

    rep, spots = validate(dataset)
    summarize(dataset, dataset["cards"], rep)  # may add total-mismatch errors

    print("\nSpot checks against the historic 6024 values:")
    for chk, card, problems in spots:
        shown = ", ".join(f"{f}={card[f]}" for f in chk["expect"]) if card else "absent"
        print(f"  [{'FAIL' if problems else ' ok '}] {chk['id']:10s} {chk['name']:22s} {shown}")

    for n in rep.notes:
        print(f"note: {n}")
    print(f"\n{len(rep.warnings)} warning(s), {len(rep.errors)} error(s)")
    for w in rep.warnings:
        print(f"  warning: {w}")
    for e in rep.errors:
        print(f"  ERROR: {e}")
    print("VALIDATION " + ("FAILED" if rep.errors else "PASSED"))
    return 1 if rep.errors else 0


if __name__ == "__main__":
    sys.exit(main())
