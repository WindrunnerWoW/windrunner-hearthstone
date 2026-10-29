#!/usr/bin/env python3
"""Tests for the Battlegrounds data pipeline and the pinned 148526 (patch 24.2) snapshot.

Run from the repository root with either:
    python -I -m unittest discover -s tools/battlegrounds -v
    python -I tools/battlegrounds/test_import.py
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bglib  # noqa: E402

_BUILD = None


def _built():
    global _BUILD
    if _BUILD is None:
        _BUILD = bglib.build_dataset()
    return _BUILD


class SourceIntegrity(unittest.TestCase):
    def test_pinned_hash_verifies(self):
        self.assertEqual(bglib.verify_source().name, bglib.PINNED_SOURCE["file"])
        self.assertEqual(bglib.verify_source(pin=bglib.PINNED_CARDDEFS).name, bglib.PINNED_CARDDEFS["file"])

    def test_hash_matches_provenance_doc(self):
        doc = (bglib.SOURCE_DIR / "PROVENANCE.md").read_text(encoding="utf-8")
        for pin in bglib.PINNED_SOURCES:
            self.assertIn(pin["sha256"], doc)
            self.assertIn(str(pin["size"]), doc)
            self.assertIn(pin["url"], doc)
        self.assertIn("148526", doc)

    def test_tampered_source_fails_loudly(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = (bglib.SOURCE_DIR / bglib.PINNED_SOURCE["file"]).read_bytes()
            (Path(tmp) / bglib.PINNED_SOURCE["file"]).write_bytes(data + b" ")
            with self.assertRaises(bglib.BgDataError) as ctx:
                bglib.verify_source(Path(tmp))
            self.assertIn("MISMATCH", str(ctx.exception))
            (Path(tmp) / bglib.PINNED_CARDDEFS["file"]).write_bytes(b"<CardDefs/>")
            with self.assertRaises(bglib.BgDataError):
                bglib.verify_source(Path(tmp), bglib.PINNED_CARDDEFS)


class Reproducibility(unittest.TestCase):
    def test_two_builds_are_byte_identical(self):
        d1, _ = bglib.build_dataset()
        d2, _ = bglib.build_dataset()
        self.assertEqual(bglib.dumps_dataset(d1), bglib.dumps_dataset(d2))

    def test_committed_output_matches_fresh_build(self):
        if not bglib.NORMALIZED_PATH.is_file():
            self.skipTest("bg.normalized.json not generated yet: run tools/battlegrounds/import_bg.py")
        dataset, _ = _built()
        self.assertEqual(bglib.NORMALIZED_PATH.read_bytes(), bglib.dumps_dataset(dataset).encode("utf-8"),
                         "bg.normalized.json is stale: re-run tools/battlegrounds/import_bg.py")

    def test_no_timestamps_or_machine_paths(self):
        text = bglib.dumps_dataset(_built()[0])
        self.assertNotIn("Users", text)
        self.assertNotIn("\r", text)


class Snapshot(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ds, cls.analysis = _built()
        cls.cards = bglib.cards_by_id(cls.ds)
        cls.pool = [c for c in cls.ds["cards"] if c["role"] == "pool_minion"]

    def role(self, role):
        return [c for c in self.ds["cards"] if c["role"] == role]

    def test_metadata(self):
        self.assertEqual(self.ds["dataset_id"], "bg-148526")
        self.assertEqual(self.ds["schema_version"], 1)
        self.assertEqual(self.ds["source"]["hearthstonejson_build"], 148526)
        self.assertEqual(self.ds["source"]["game_patch"], "24.2")
        self.assertEqual(self.ds["content_hash"], bglib.content_hash(self.ds["cards"]))

    def test_sorted_with_one_schema(self):
        cards = self.ds["cards"]
        keys = [(bglib.ROLE_RANK[c["role"]], c["id"]) for c in cards]
        self.assertEqual(keys, sorted(keys))
        self.assertEqual(len(self.cards), len(cards))                    # unique ids
        self.assertEqual({tuple(c) for c in cards}, {tuple(cards[0])})   # same keys, same order

    def test_pool_counts(self):
        self.assertEqual(len(self.pool), 167)
        self.assertEqual(sorted(Counter(c["tier"] for c in self.pool).items()),
                         [(1, 20), (2, 30), (3, 33), (4, 34), (5, 28), (6, 22)])
        tribes = Counter(c["tribes"][0] if c["tribes"] else None for c in self.pool)
        self.assertEqual(dict(tribes), {"BEAST": 16, "QUILBOAR": 16, "MECHANICAL": 15, "PIRATE": 15, "NAGA": 15,
                                        "DEMON": 14, "DRAGON": 14, "ELEMENTAL": 14, "MURLOC": 12, None: 35,
                                        "ALL": 1})
        amalgam = self.cards["BG_GIL_681"]
        self.assertEqual((amalgam["name"], amalgam["tribes"], amalgam["tier"]), ("Nightmare Amalgam", ["ALL"], 3))

    def test_pool_minion_fields(self):
        rat = self.cards["BG19_010"]
        self.assertEqual((rat["name"], rat["tier"], rat["attack"], rat["health"], rat["tribes"]),
                         ("Sewer Rat", 2, 3, 2, ["BEAST"]))
        self.assertEqual(rat["text"], "<b>Deathrattle:</b> Summon a 2/3 Turtle with <b>Taunt</b>.")  # markup kept
        self.assertEqual(rat["referenced_tags"], ["DEATHRATTLE", "TAUNT"])
        for c in self.pool:
            self.assertEqual(c["type"], "MINION")
            self.assertIsNotNone(c["attack"], c["id"])
            self.assertIsNotNone(c["health"], c["id"])

    def test_every_pool_minion_has_a_golden_version(self):
        for c in self.pool:
            g = c["golden"]
            self.assertIsNotNone(g, c["id"])
            self.assertNotEqual(g["id"], c["id"])
            self.assertIsNotNone(g["dbf_id"])
            self.assertGreaterEqual(g["attack"], c["attack"], c["id"])
            self.assertGreaterEqual(g["health"], c["health"], c["id"])
            self.assertTrue(g["text"] or not c["text"], c["id"])
        rat = self.cards["BG19_010"]["golden"]
        self.assertEqual((rat["id"], rat["attack"], rat["health"]), ("BG19_010_G", 6, 4))
        self.assertIn("4/6 Turtle", rat["text"])

    def test_heroes(self):
        heroes = self.role("hero")
        self.assertEqual(len(heroes), 86)
        for h in heroes:
            self.assertEqual(h["type"], "HERO")
            self.assertIsNotNone(h["health"], h["id"])
        self.assertEqual(self.cards["BG20_HERO_100"]["name"], "Rokara")
        self.assertEqual(self.cards["BG20_HERO_100"]["health"], 40)
        self.assertEqual(self.cards["BG20_HERO_100"]["hero_power"], "BG20_HERO_100p")
        self.assertEqual(self.cards["TB_BaconShop_HERO_01"]["hero_power"], "TB_BaconShop_HP_001")
        self.assertEqual(self.cards["BG23_HERO_303"]["hero_power"], "BG23_HERO_303p2")
        # links come from CardDefs tag 380, not from ids: old heroes do not follow the HERO_NN -> HP_0NN pattern
        for hero, power, name in (("TB_BaconShop_HERO_02", "TB_BaconShop_HP_011", "Galakrond's Greed"),
                                  ("TB_BaconShop_HERO_58", "TB_BaconShop_HP_052", None),    # Malygos
                                  ("TB_BaconShop_HERO_12", "TB_BaconShop_HP_041", None),    # The Rat King
                                  ("TB_BaconShop_HERO_29", "TB_BaconShop_HP_104", None),    # C'Thun
                                  ("TB_BaconShop_HERO_93", "TB_BaconShop_HP_105", None),    # N'Zoth
                                  ("TB_BaconShop_HERO_10", "TB_BaconShop_HP_008", "Smart Savings")):
            self.assertEqual(self.cards[hero]["hero_power"], power, hero)
            if name:
                self.assertEqual(self.cards[power]["name"], name)
        # every hero has a power (CardDefs has none missing) that points back
        self.assertEqual(self.analysis["heroes_without_power"], [])
        self.assertEqual(len({h["hero_power"] for h in heroes}), 86)
        for h in heroes:
            hp = self.cards[h["hero_power"]]
            self.assertEqual(hp["type"], "HERO_POWER")
            self.assertIn(h["id"], hp["hero_ids"])
            for extra in h["extra_hero_powers"]:
                self.assertEqual(self.cards[extra]["type"], "HERO_POWER")
        # the old id-convention guess found 64 heroes and was wrong for 31 of them (kept as a cross-check only)
        self.assertEqual(self.analysis["power_guess_matched"], 64)
        wrong = [x for x in self.analysis["power_crosscheck"] if x[2] is not None]
        self.assertEqual(len(wrong), 31)
        self.assertEqual(len(self.analysis["power_crosscheck"]) - len(wrong), 22)
        for hp in self.role("hero_power"):
            self.assertEqual(hp["type"], "HERO_POWER")
            self.assertTrue(hp["hero_ids"], hp["id"])

    def test_skins_are_excluded(self):
        self.assertFalse([c for c in self.ds["cards"] if "_SKIN" in c["id"]])

    def test_quests_and_rewards(self):
        rewards = self.role("quest_reward")
        self.assertEqual(len(rewards), 35)
        self.assertTrue(all(c["type"] == "BATTLEGROUND_QUEST_REWARD" for c in rewards))
        self.assertEqual(self.cards["BG24_Reward_109"]["name"], "Stolen Gold")
        quests = self.role("quest")
        self.assertEqual(len(quests), 15)
        self.assertEqual(self.cards["BG24_Quest_112"]["name"], "Track the Footprints")
        self.assertEqual(self.cards["BG24_Quest_126"]["name"], "Follow the Money")

    def test_token_links(self):
        self.assertEqual(self.cards["BG19_010"]["tokens"], ["BG19_010t"])              # id prefix
        self.assertEqual(self.cards["BGS_014"]["tokens"], ["BG_BRM_006t"])             # stats + name (Imprisoner)
        self.assertEqual(self.cards["BG_LOOT_368"]["tokens"], ["BG_CS2_065"])          # stats + tribe (Voidlord)
        self.assertEqual(self.cards["BG21_025"]["tokens"], ["BG_BOT_312t"])            # Omega Buster, Microbots
        self.assertIn("BG20_GEM", self.cards["BG20_100"]["tokens"])                    # keyword entity
        self.assertNotIn("BG20_GEM", self.cards["BG20_205"]["tokens"])                 # only mentions Blood Gems
        for t in self.role("token"):
            self.assertTrue(t["referenced_by"], t["id"])
            for owner in t["referenced_by"]:
                self.assertIn(t["id"], self.cards[owner]["tokens"])
            self.assertIn(t["type"], ("MINION", "SPELL"))
        golden_less = sorted(t["id"] for t in self.role("token") if t["type"] == "MINION" and not t["golden"])
        self.assertEqual(golden_less, ["BG22_HERO_001p_t1et", "BG22_HERO_305t", "BG23_353_Gt", "BG23_HERO_201pt",
            "BGS_115t", "EX1_170", "EX1_554t", "TB_BaconShop_HP_033t"])
        self.assertEqual(self.cards["BG22_HERO_305"]["tokens"], ["BG22_HERO_305t"])   # hero token
        self.assertEqual(self.cards["BG22_HERO_001p"]["tokens"], ["BG22_HERO_001p_t1et"])   # Earth
        self.assertEqual(self.cards["TB_BaconShop_HP_033"]["tokens"], ["TB_BaconShop_HP_033t"])
        # The 10 Secrets and the two classic tokens they summon (Tier 1 from the token fallback).
        power_tokens = self.cards["TB_BaconShop_HP_020"]["tokens"]
        self.assertEqual(len(power_tokens), 12)
        self.assertIn("TB_Bacon_Secrets_01", power_tokens)
        self.assertIn("EX1_554t", power_tokens)
        self.assertEqual(self.cards["EX1_170"]["tier"], 1)
        self.assertEqual(self.cards["EX1_554t"]["tier"], 1)
        # The 29 Darkmoon Prizes tagged 1735; Big Banana and Regular Discount are not prizes of 24.2.
        prizes = self.cards["TB_BaconShop_HP_106"]["tokens"]
        self.assertEqual(len(prizes), 29)
        self.assertNotIn("BGS_Treasures_000", prizes)
        self.assertNotIn("BGS_Treasures_003", prizes)
        self.assertEqual(self.analysis["unresolved"], [])


class Slice15A(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ds, cls.analysis = _built()
        cls.cards = bglib.cards_by_id(cls.ds)
        cls.pool = [c for c in cls.ds["cards"] if c["role"] == "pool_minion"]
        cls.in15a = [c for c in cls.pool if c["slice"] == "15A"]

    def test_no_card_from_an_excluded_tribe(self):
        self.assertTrue(self.in15a)
        allowed = set(bglib.SLICE_15A_TRIBES) | {"ALL"}
        for c in self.ds["cards"]:
            if c["slice"] == "15A" and c["tribes"]:
                self.assertTrue(set(c["tribes"]) <= allowed, c["id"])
            if c["slice"] == "15A":
                self.assertIn(c["role"], ("pool_minion", "token"))
        for c in self.pool:
            if c["tribes"] and c["tribes"][0] in bglib.EXCLUDED_TRIBES:
                self.assertIsNone(c["slice"], c["id"])
        self.assertEqual({c["slice"] for c in self.ds["cards"]}, {"15A", None})

    def test_all_tribed_minions_of_the_five_are_in(self):
        for c in self.pool:
            if c["tribes"] and c["tribes"][0] in set(bglib.SLICE_15A_TRIBES) | {"ALL"}:
                self.assertEqual(c["slice"], "15A", c["id"])
        self.assertEqual(self.cards["BG_GIL_681"]["slice"], "15A")                  # Nightmare Amalgam

    def test_tribeless_rule(self):
        tribeless = [c for c in self.pool if not c["tribes"]]
        self.assertEqual(len(tribeless), 35)
        self.assertEqual([t[0] for t in self.analysis["tribeless"]], sorted(c["id"] for c in tribeless))
        for c in tribeless:
            plain = bglib.plain_text(c["text"])
            blocked = any(rx.search(plain) for _, rx in bglib.TRIBELESS_BLOCKERS)
            if c["id"] in bglib.TRIBELESS_OVERRIDES:                    # the owner forced it in
                self.assertEqual(c["slice"] == "15A", bglib.TRIBELESS_OVERRIDES[c["id"]], c["id"])
            else:
                self.assertEqual(c["slice"] is None, blocked, c["id"])
        for cid in ("BG20_203",):
            self.assertEqual(self.cards[cid]["slice"], "15A", cid)   # implements Blood Gems
        self.assertEqual(self.cards["BG23_015"]["slice"], "15A")   # implements Spellcraft
        for cid in ("BGS_012", "BG_FP1_031", "BG24_020", "BG21_002", "BG23_352", "BGS_105", "BG21_036", "BG24_018"):
            self.assertEqual(self.cards[cid]["slice"], "15A", cid)

    def test_tokens_follow_their_owners(self):
        forced = {tid for ids in bglib.HERO_TOKEN_LINKS.values() for tid in ids}
        for t in self.ds["cards"]:
            if t["role"] == "token":
                if t["id"] in forced:
                    self.assertEqual(t["slice"], "15A", t["id"])
                    continue
                owners = [self.cards[o]["slice"] for o in t["referenced_by"]]
                self.assertEqual(t["slice"] == "15A", "15A" in owners, t["id"])
        self.assertEqual(len(self.in15a), 167)   # adds the 16 Quilboar and Prophet of the Boar


if __name__ == "__main__":
    unittest.main(verbosity=2)
