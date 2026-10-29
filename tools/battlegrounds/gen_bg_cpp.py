#!/usr/bin/env python3
"""Generates the engine's generated tables from data/battlegrounds/bg.normalized.json:
    src/battlegrounds/HsBgCardData.inc   the card table (pool minions and included tokens)
    src/battlegrounds/HsBgHeroData.inc   the 15C hero table (86 heroes, skins excluded)
    src/battlegrounds/HsBgQuestData.inc  the 15D quest table (id, dbfId, progress, in-pool)
    src/battlegrounds/HsBgRewardData.inc the 15D quest reward table (id, dbfId, in-pool)

Usage (from the repository root, modules/mod-hearthstone):
    python -I tools/battlegrounds/gen_bg_cpp.py            # write the .inc files
    python -I tools/battlegrounds/gen_bg_cpp.py --check    # do not write; exit 1 if a file differs from a fresh build

Stats only, no card text and no names: the server never needs them (card text is addon tooltip data). One row per
pool minion (167) and per included token (21 minion tokens and 22 spells), in the order of the dataset (pool minions first, ids sorted). The
row index is the card index of Hs::Bg (HsBgTypes.h), so it is stable only until the dataset changes: nothing may
persist an index, use the id. The hero rows come from every hero of the dataset (86, skins excluded) plus its
Armor Tier from data/battlegrounds/hero_armor.json; `curated` marks the 18 first heroes. The quest and reward
rows come from the dataset's `quest` / `quest_reward` roles plus data/battlegrounds/quest_values.json (the
progress number and the 24.2 offer list), id sorted. The output has no timestamps, so two runs give byte-identical
files.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bglib  # noqa: E402

INC_PATH = bglib.REPO_ROOT / "src" / "battlegrounds" / "HsBgCardData.inc"
HERO_INC_PATH = bglib.REPO_ROOT / "src" / "battlegrounds" / "HsBgHeroData.inc"
QUEST_INC_PATH = bglib.REPO_ROOT / "src" / "battlegrounds" / "HsBgQuestData.inc"
REWARD_INC_PATH = bglib.REPO_ROOT / "src" / "battlegrounds" / "HsBgRewardData.inc"
CURATED_HEROES = 18                              # at least 4 for the human + 2 for each of the 7 NPC seats
HERO_ARMOR_PATH = bglib.REPO_ROOT / "data" / "battlegrounds" / "hero_armor.json"
QUEST_VALUES_PATH = bglib.REPO_ROOT / "data" / "battlegrounds" / "quest_values.json"

# Tribe and keyword names, as the symbols declared in HsBgTypes.h (namespace Hs::Bg).
TRIBES = {
    "BEAST": "kTribeBeast", "DEMON": "kTribeDemon", "DRAGON": "kTribeDragon", "ELEMENTAL": "kTribeElemental",
    "MECHANICAL": "kTribeMech", "MURLOC": "kTribeMurloc", "NAGA": "kTribeNaga", "PIRATE": "kTribePirate",
    "QUILBOAR": "kTribeQuilboar", "ALL": "kTribeAll",
}
KEYWORDS = {
    "TAUNT": "kKwTaunt", "DIVINE_SHIELD": "kKwDivineShield", "POISONOUS": "kKwPoisonous", "WINDFURY": "kKwWindfury",
    "REBORN": "kKwReborn", "MODULAR": "kKwMagnetic", "MAGNETIC": "kKwMagnetic", "VENOMOUS": "kKwVenomous",
}
# The data's mechanics miss some keywords the card has: Half-Shell (BG19_010t), Sparring Partner, Deflect-o-Bot and
# BGS_060 say "Taunt" or "Divine Shield" as the first words of their text but list no such mechanic. So the bold
# keywords a card text STARTS with also count (a leading run: "Taunt" then "Battlecry: ..." gives Taunt). Plain
# Windfury counts too: normal Zapp Slywick (BGS_022) starts with it but lists it only as a referenced tag. A leading
# "Mega-Windfury" is left to the mechanics (see the golden rule below).
TEXT_KEYWORDS = {"Taunt": "TAUNT", "Divine Shield": "DIVINE_SHIELD", "Poisonous": "POISONOUS", "Venomous": "VENOMOUS", "Reborn": "REBORN",
                 "Windfury": "WINDFURY"}
LEADING_KEYWORDS = re.compile(r"^(?:(?:<b>)+(?:Taunt|Divine Shield|Poisonous|Venomous|Reborn|Mega-Windfury|Windfury)"
                              r"(?:</b>)+[,.\s]*(?:</b>)*)+")
ROLES = {"pool_minion": "CardRole::Pool", "token": "CardRole::Token", "spell": "CardRole::Spell"}
MEGA = "kKwMegaWindfury"


class GenError(Exception):
    pass


def tribe_expr(tribes: list) -> str:
    unknown = [t for t in tribes if t not in TRIBES]
    if unknown:
        raise GenError(f"unknown tribe {unknown}")
    return " | ".join(TRIBES[t] for t in sorted(tribes, key=lambda t: list(TRIBES).index(t))) or "0"


def text_keywords(text: str | None) -> list:
    """Mechanics named by the keywords a text starts with (see TEXT_KEYWORDS)."""
    m = LEADING_KEYWORDS.match((text or "").replace("[x]", ""))
    if not m:
        return []
    return [TEXT_KEYWORDS[k] for k in re.findall(r">(Taunt|Divine Shield|Poisonous|Venomous|Reborn|Windfury)<", m.group(0))]


def keyword_expr(card_id: str, mechanics: list, text: str | None, golden: bool) -> str:
    """Keyword bits of a card. A golden card with WINDFURY is Mega-Windfury when its text says so (a render override
    can keep a golden Windfury: Zapp Slywick); a normal one never is."""
    names = []
    mechanics = list(mechanics) + text_keywords(text)
    for m in mechanics:
        if m == "WINDFURY":
            if golden and "Mega-Windfury" in (text or ""):
                names.append(MEGA)
            else:
                names.append(KEYWORDS[m])
        elif m in KEYWORDS:
            names.append(KEYWORDS[m])
    order = list(dict.fromkeys(KEYWORDS.values())) + [MEGA]
    names = sorted(set(names), key=order.index)
    return " | ".join(names) or "0"


def row_for(card: dict) -> str:
    spell = card["type"] == "SPELL"
    g = card["golden"]
    tier = card["tier"]
    if not isinstance(tier, int) or (0 if spell else 1) > tier or tier > 6:
        raise GenError(f"{card['id']}: tier {tier!r} out of range")
    if g:
        gold = '"%s", %d, %d, %d, %s' % (g["id"], g["dbf_id"], g["attack"] or 0, g["health"] or 0,
                                         keyword_expr(g["id"], g["mechanics"], g["text"], True) if not spell else "0")
    else:
        gold = '"", 0, 0, 0, 0'
    return '        {"%s", %d, %d, %d, %d, %s, %s, %s, %s, %s},' % (
        card["id"], card["dbf_id"], tier, card["attack"] or 0, card["health"] or 0, tribe_expr(card["tribes"]),
        keyword_expr(card["id"], card["mechanics"], card["text"], False) if not spell else "0", gold,
        "true" if card["slice"] == "15A" else "false", "CardRole::Spell" if spell else ROLES[card["role"]])


def build_inc(dataset: dict) -> str:
    cards = [c for c in dataset["cards"]
             if c["role"] == "pool_minion" or (c["role"] == "token" and c["type"] in ("MINION", "SPELL")
                                                and c["slice"] == "15A")]
    ids = [c["id"] for c in cards]
    if len(set(ids)) != len(ids):
        raise GenError("duplicate card id")
    rows = [row_for(c) for c in cards]
    pool = sum(1 for c in cards if c["role"] == "pool_minion")
    spells = sum(1 for c in cards if c["type"] == "SPELL")
    digest = hashlib.sha256("\n".join(rows).encode("utf-8")).hexdigest()
    return """// GENERATED by tools/battlegrounds/gen_bg_cpp.py from data/battlegrounds/bg.normalized.json (%s, content hash %s).
// Do not edit by hand. Stats only, no names or text: %d pool minions, %d tokens and %d spell cards, in
// dataset order (the row index is the card index of HsBgTypes.h). Row check sha256 %s.
// Row: id, dbfId, tier, attack, health, tribes, keywords, golden id, golden dbfId, golden attack, golden health,
// golden keywords (a golden Windfury is kKwMegaWindfury), slice 15A, role. Tokens are tier 1, spells tier 0 or their
// source minion's Tier (Spellcraft).
// Included inside namespace Hs::Bg by HsBgTypes.cpp, after the declarations of CardDef, tribes and keywords.
// clang-format off
%s
// clang-format on
""" % (dataset["dataset_id"], dataset["content_hash"], pool, len(cards) - pool - spells, spells, digest, "\n".join(rows))


def hero_row(hero: dict, armor_tier: int, curated: bool) -> str:
    health = hero.get("health")
    if not isinstance(health, int) or health <= 0:
        health = 40                                  # a hero without data starts at 40 (Kel'Thuzad-style 0 included)
    power_id = hero.get("hero_power") or ""
    return '        {"%s", %d, %d, "%s", %d, %s},' % (
        hero["id"], hero["dbf_id"], health, power_id, armor_tier, "true" if curated else "false")


def build_hero_inc(dataset: dict, armor: dict) -> str:
    heroes = [c for c in dataset["cards"] if c["role"] == "hero"]
    by_id = {c["id"]: c for c in dataset["cards"]}
    tiers = armor["armor_tier"]
    curated = set(armor["curated"])
    missing = [h["id"] for h in heroes if h["id"] not in tiers]
    if missing:
        raise GenError(f"heroes without an armor tier: {missing}")
    if not curated <= set(tiers):
        raise GenError(f"curated hero not in the armor table: {sorted(curated - set(tiers))}")
    if len(curated) < CURATED_HEROES:
        raise GenError(f"the curated hero list has {len(curated)} heroes, the hero pick needs at least {CURATED_HEROES}")
    heroes = sorted(heroes, key=lambda h: h["id"])
    rows = []
    for h in heroes:
        if not h.get("hero_power"):
            raise GenError(f"hero {h['id']} has no hero power")
        power = by_id.get(h["hero_power"])
        if power is None or power["role"] != "hero_power":
            raise GenError(f"hero {h['id']}: power {h['hero_power']} is not a hero power")
        rows.append(hero_row(h, tiers[h["id"]], h["id"] in curated))
    digest = hashlib.sha256("\n".join(rows).encode("utf-8")).hexdigest()
    return """// GENERATED by tools/battlegrounds/gen_bg_cpp.py from data/battlegrounds/bg.normalized.json (%s, content hash %s)
// and data/battlegrounds/hero_armor.json. Do not edit by hand. The %d heroes of the dataset (skins excluded), id
// sorted, with the hero power id (its cost lives in the HeroPowerDef registry) and the reconstructed 24.2.1 armor tier.
// `curated` marks the %d first heroes. Row check sha256 %s.
// Row: id, dbfId, health, hero power id, armor tier, curated.
// Included inside namespace Hs::Bg by HsBgTypes.cpp, after the declaration of HeroDef.
// clang-format off
%s
// clang-format on
""" % (dataset["dataset_id"], dataset["content_hash"], len(rows), len(curated), digest, "\n".join(rows))


def _quest_values() -> dict:
    values = json.loads(QUEST_VALUES_PATH.read_text(encoding="utf-8"))
    if "quests" not in values or "rewards" not in values:
        raise GenError(f"{QUEST_VALUES_PATH.name} needs a 'quests' and a 'rewards' map")
    return values


def build_quest_inc(dataset: dict, values: dict) -> tuple:
    """The (quest rows, reward rows) of the 15D table. Every quest/reward of the dataset must be in quest_values.json."""
    quests = sorted((c for c in dataset["cards"] if c["role"] == "quest"), key=lambda c: c["id"])
    rewards = sorted((c for c in dataset["cards"] if c["role"] == "quest_reward"), key=lambda c: c["id"])
    vq, vr = values["quests"], values["rewards"]
    missing = [c["id"] for c in quests if c["id"] not in vq] + [c["id"] for c in rewards if c["id"] not in vr]
    if missing:
        raise GenError(f"quest_values.json is missing {missing} (add the new quest/reward rows)")
    qrows, rrows = [], []
    for c in quests:
        v = vq[c["id"]]
        progress = v.get("progress", 0)
        if not isinstance(progress, int) or progress < 0:
            raise GenError(f"{c['id']}: bad progress {progress!r}")
        qrows.append('        {"%s", %d, %d, %s},' % (c["id"], c["dbf_id"], progress, "true" if v["in_pool"] else "false"))
    for c in rewards:
        rrows.append('        {"%s", %d, %s},' % (c["id"], c["dbf_id"], "true" if vr[c["id"]]["in_pool"] else "false"))
    qpool = sum(1 for c in quests if vq[c["id"]]["in_pool"])
    rpool = sum(1 for c in rewards if vr[c["id"]]["in_pool"])
    qdigest = hashlib.sha256("\n".join(qrows).encode("utf-8")).hexdigest()
    rdigest = hashlib.sha256("\n".join(rrows).encode("utf-8")).hexdigest()
    qtext = """// GENERATED by tools/battlegrounds/gen_bg_cpp.py from data/battlegrounds/bg.normalized.json (%s, content hash %s)
// and data/battlegrounds/quest_values.json. Do not edit by hand. The %d quests of the dataset, id sorted; `progress`
// is the 24.2.0 baseline and `inPool` the 24.2 offer list, %d in pool. Row check sha256 %s.
// Row: id, dbfId, progress number, in-pool. Included inside namespace Hs::Bg by HsBgTypes.cpp, after QuestDef.
// clang-format off
%s
// clang-format on
""" % (dataset["dataset_id"], dataset["content_hash"], len(qrows), qpool, qdigest, "\n".join(qrows))
    rtext = """// GENERATED by tools/battlegrounds/gen_bg_cpp.py from data/battlegrounds/bg.normalized.json (%s, content hash %s)
// and data/battlegrounds/quest_values.json. Do not edit by hand. The %d quest rewards of the dataset, id sorted, %d in
// pool. Row check sha256 %s. Row: id, dbfId, in-pool. Included inside namespace Hs::Bg by HsBgTypes.cpp.
// clang-format off
%s
// clang-format on
""" % (dataset["dataset_id"], dataset["content_hash"], len(rrows), rpool, rdigest, "\n".join(rrows))
    return qtext, rtext


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="do not write; exit 1 if a file differs from a fresh build")
    ap.add_argument("--data", type=Path, default=bglib.NORMALIZED_PATH, help="the normalized dataset to read")
    ap.add_argument("--out", type=Path, default=INC_PATH, help="the card .inc file to write or check")
    ap.add_argument("--hero-out", type=Path, default=HERO_INC_PATH, help="the hero .inc file to write or check")
    ap.add_argument("--quest-out", type=Path, default=QUEST_INC_PATH, help="the quest .inc file to write or check")
    ap.add_argument("--reward-out", type=Path, default=REWARD_INC_PATH, help="the reward .inc file to write or check")
    args = ap.parse_args(argv)
    try:
        dataset = json.loads(args.data.read_text(encoding="utf-8"))
        text = build_inc(dataset)
        armor = json.loads(HERO_ARMOR_PATH.read_text(encoding="utf-8"))
        hero_text = build_hero_inc(dataset, armor)
        quest_text, reward_text = build_quest_inc(dataset, _quest_values())
    except (OSError, ValueError, KeyError, GenError) as exc:
        print(f"GENERATE FAILED: {exc} (run tools/battlegrounds/import_bg.py first?)", file=sys.stderr)
        return 2
    outputs = [(args.out, text.encode("utf-8")), (args.hero_out, hero_text.encode("utf-8")),
               (args.quest_out, quest_text.encode("utf-8")), (args.reward_out, reward_text.encode("utf-8"))]
    if args.check:
        bad = [p for p, b in outputs if not p.is_file() or p.read_bytes() != b]
        if bad:
            print("CHECK FAILED: missing or different from a fresh build: " + ", ".join(str(p) for p in bad), file=sys.stderr)
            return 1
        print("check ok: the card, hero, quest and reward .inc files match a fresh build")
        return 0
    for p, b in outputs:
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(b)
        print(f"wrote {p} ({len(b)} bytes, {b.count(10)} lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
