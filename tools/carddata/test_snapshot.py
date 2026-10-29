#!/usr/bin/env python3
"""Tests for the card data pipeline and the pinned 6024 snapshot.

Run from the repository root with either:
    python -m unittest discover -s tools/carddata -v
    python tools/carddata/test_snapshot.py
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cardlib  # noqa: E402
import snapshot_expectations as exp  # noqa: E402
import validate_cards  # noqa: E402


def _dataset():
    return cardlib.load_dataset()


class SourceIntegrity(unittest.TestCase):
    def test_pinned_hashes_verify(self):
        found = cardlib.verify_sources()
        self.assertEqual(set(found), {"cards_json", "manifest_cards_csv"})

    def test_hashes_match_provenance_doc(self):
        doc = (cardlib.SOURCE_DIR / "PROVENANCE.md").read_text(encoding="utf-8")
        for key, pin in cardlib.PINNED_SOURCES.items():
            self.assertIn(pin["sha256"], doc, f"{key} sha256 missing from PROVENANCE.md")

    def test_tampered_source_fails_loudly(self):
        with tempfile.TemporaryDirectory() as tmp:
            for pin in cardlib.PINNED_SOURCES.values():
                if pin["committed"]:
                    data = (cardlib.SOURCE_DIR / pin["file"]).read_bytes()
                    (Path(tmp) / pin["file"]).write_bytes(data + b" ")
            with self.assertRaises(cardlib.CardDataError) as ctx:
                cardlib.verify_sources(Path(tmp))
            self.assertIn("MISMATCH", str(ctx.exception))


class Reproducibility(unittest.TestCase):
    def test_two_builds_are_byte_identical(self):
        a = cardlib.dumps_dataset(cardlib.build_dataset()[0])
        b = cardlib.dumps_dataset(cardlib.build_dataset()[0])
        self.assertEqual(a, b)

    def test_committed_output_matches_fresh_build(self):
        fresh = cardlib.dumps_dataset(cardlib.build_dataset()[0]).encode("utf-8")
        self.assertEqual(cardlib.NORMALIZED_PATH.read_bytes(), fresh,
                         "cards.normalized.json is stale: re-run tools/carddata/import_cards.py")


class Snapshot(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ds = _dataset()
        cls.cards = cardlib.cards_by_id(cls.ds)

    def test_metadata(self):
        self.assertEqual(self.ds["dataset_id"], "hs-6024-basic-classic")
        self.assertEqual(self.ds["schema_version"], 1)
        self.assertEqual(self.ds["source"]["client_build"], "1.1.0.6024")
        self.assertEqual(self.ds["content_hash"], cardlib.content_hash(self.ds["cards"]))
        # 6024 is the exact snapshot: the only overrides are the display tag TRIGGER_VISUAL (001_trigger_visual.json,
        # nothing but the mechanics field); Adrenaline Rush and the unsummoned Skeleton are dropped through
        # cardlib.REMOVED_IDS instead
        applied = self.ds["overrides_applied"]
        self.assertTrue(applied)
        for o in applied:
            self.assertEqual(o["file"], "001_trigger_visual.json")
            self.assertEqual(o["fields"], ["mechanics"])
            card = self.cards[o["card_id"]]
            self.assertIn("TRIGGER_VISUAL", card["mechanics"])
            self.assertEqual(card["type"], "MINION")

    def test_historic_spot_checks(self):
        for chk in exp.SPOT_CHECKS:
            with self.subTest(card=f"{chk['id']} {chk['name']}"):
                card = self.cards[chk["id"]]
                self.assertEqual(card["name"], chk["name"])
                for field, want in chk["expect"].items():
                    self.assertEqual(card[field], want, f"{chk['name']}.{field} ({chk['why']})")
                plain = cardlib.plain_text(card["text"])
                for s in chk.get("text_has", []):
                    self.assertIn(s, plain)
                for s in chk.get("text_lacks", []):
                    self.assertNotIn(s, plain)

    def test_named_values_from_the_brief(self):
        c = self.cards
        self.assertEqual(c["EX1_538"]["cost"], 3)        # Unleash the Hounds
        self.assertEqual(c["EX1_116"]["cost"], 4)        # Leeroy Jenkins
        self.assertEqual((c["CS2_237"]["cost"], c["CS2_237"]["attack"], c["CS2_237"]["health"]), (2, 2, 1))
        self.assertEqual(c["EX1_095"]["cost"], 5)        # Gadgetzan Auctioneer
        self.assertEqual(c["EX1_544"]["cost"], 1)        # Flare
        self.assertEqual(c["EX1_308"]["cost"], 0)        # Soulfire
        bow = cardlib.plain_text(c["EX1_536"]["text"])   # Eaglehorn Bow
        self.assertIn("friendly Secret is revealed", bow)  # rule change of build 6024

    def test_no_post_snapshot_cards(self):
        for cid in exp.MUST_BE_ABSENT:
            self.assertNotIn(cid, self.cards)
        extra = {c["set"] for c in self.cards.values()} - {"CORE", "EXPERT1"}
        # Naxxramas, plus only the reward-only cards and their dependencies
        self.assertEqual(extra, {"NAXX", "PROMO", "REWARD"})

    def test_collectible_totals(self):
        cat = [c for c in self.cards.values() if c["acquisition"] == "standard"]
        self.assertTrue(all(c["collectible"] and c["type"] in cardlib.CATALOG_TYPES for c in cat))
        for s, want in exp.EXPECTED_CATALOG_TOTALS.items():
            self.assertEqual(sum(1 for c in cat if c["set"] == s), want, s)
        self.assertEqual(len(cat), 378)
        heroes = [c for c in self.cards.values() if c["collectible"] and c["type"] == "HERO"]
        self.assertEqual(len(heroes), exp.EXPECTED_COLLECTIBLE_HEROES)
        self.assertTrue(all(h["acquisition"] == "none" for h in heroes))
        # every collectible catalog card that is not standard is a reward-only, a quest card or the unreleased card
        self.assertEqual({c["id"] for c in self.cards.values() if c["collectible"]
                          and c["type"] in cardlib.CATALOG_TYPES and c["acquisition"] != "standard"},
                         set(exp.EXPECTED_REWARD_ONLY) | set(exp.EXPECTED_QUEST) | set(exp.EXPECTED_UNRELEASED))

    def test_unreleased_card(self):
        # no card is unreleased any more: Adrenaline Rush was removed from the dataset (owner decision)
        self.assertEqual({c["id"] for c in self.cards.values() if c["acquisition"] == "unreleased"},
                         set(exp.EXPECTED_UNRELEASED))

    def test_removed_cards(self):
        self.assertEqual(set(cardlib.REMOVED_IDS), {"NEW1_006", "EX1_016t"})
        for cid in cardlib.REMOVED_IDS:
            self.assertNotIn(cid, self.cards)

    def test_promo_tokens(self):
        # the tokens of I Am Murloc and Poultryizer come along with their promo cards
        for cid, name, cost in (("PRO_001at", "Murloc", 1), ("Mekka4t", "Chicken", 0)):
            card = self.cards[cid]
            self.assertEqual((card["name"], card["type"], card["set"], card["cost"], card["attack"], card["health"],
                              card["acquisition"], card["included_by"]),
                             (name, "MINION", "PROMO", cost, 1, 1, "none", "reference"))

    def test_unsummoned_tokens(self):
        for cid in cardlib.UNSUMMONED_TOKENS:
            card = self.cards[cid]
            self.assertEqual((card["collectible"], card["acquisition"]), (False, "none"))
            for other in self.cards.values():
                self.assertNotIn(cid, other["entourage"])
                if other["id"] != cid and other["set"] not in cardlib.QUEST_SETS:  # NAXX has its own Skeleton NAX4_03
                    self.assertNotIn(card["name"].lower(), (other["text"] or "").lower(), other["id"])

    def test_reward_only_cards(self):
        got = {c["id"] for c in self.cards.values() if c["acquisition"] == "reward_only"}
        self.assertEqual(got, set(exp.EXPECTED_REWARD_ONLY))
        for cid, (name, set_name) in exp.EXPECTED_REWARD_ONLY.items():
            card = self.cards[cid]
            self.assertEqual((card["name"], card["set"]), (name, set_name))  # original set kept
            self.assertTrue(card["collectible"])
            self.assertEqual(card["included_by"], "reward")
        for cid in exp.EXPECTED_REWARD_DEPENDENCIES:
            card = self.cards[cid]
            self.assertEqual((card["acquisition"], card["included_by"], card["collectible"]),
                             ("none", "reference", False))
        self.assertIn("Mekka1", self.cards["EX1_112"]["entourage"])
        self.assertEqual(self.ds["reward_only_ids"], sorted(exp.EXPECTED_REWARD_ONLY))

    def test_quest_cards(self):
        got = {c["id"] for c in self.cards.values() if c["acquisition"] == "quest"}
        self.assertEqual(got, set(exp.EXPECTED_QUEST))
        self.assertEqual(len(got), 30)
        for cid, name in exp.EXPECTED_QUEST.items():
            card = self.cards[cid]
            self.assertEqual((card["name"], card["set"], card["collectible"], card["included_by"]),
                             (name, "NAXX", True, "set"))
            self.assertIn(card["type"], cardlib.CATALOG_TYPES)
            self.assertIsNotNone(card["rarity"])
        # the class cards keep their class, everything else is neutral
        self.assertEqual({self.cards[c]["class"] for c in ("FP1_019", "FP1_026", "FP1_018", "FP1_011", "FP1_025",
                                                           "FP1_022", "FP1_021", "FP1_023", "FP1_020")},
                         {"DRUID", "ROGUE", "MAGE", "HUNTER", "SHAMAN", "WARLOCK", "WARRIOR", "PRIEST", "PALADIN"})
        # the standard catalog (packs, crafting) is unchanged by the expansion
        self.assertEqual(sum(1 for c in self.cards.values() if c["acquisition"] == "standard"), 378)

    def test_naxx_runtime_entities(self):
        naxx = [c for c in self.cards.values() if c["set"] == "NAXX"]
        self.assertEqual(len(naxx), exp.EXPECTED_NAXX_ENTITIES)
        for c in naxx:
            self.assertEqual(c["acquisition"], "quest" if c["collectible"] else "none", c["id"])
        # boss heroes (Normal and Heroic) all have a hero power; Heroic variants are included
        heroes = [c for c in naxx if c["type"] == "HERO"]
        self.assertEqual(len(heroes), 30)
        for h in heroes:
            self.assertIsNotNone(h["hero_power"], h["id"])
            self.assertEqual(self.cards[h["hero_power"]]["type"], "HERO_POWER")
        self.assertEqual(self.cards["NAX6_01"]["health"], 75)
        self.assertEqual(self.cards["NAX6_01"]["hero_power"], "NAX6_02")
        self.assertEqual(self.cards["NAX9_01H"]["hero_power"], "NAX9_06")
        # the known manifest gap: no dbf id for the Necrotic Aura enchantment (kept, like the other dbf-less enchantments)
        self.assertIsNone(self.cards["NAX6_02e"]["dbf_id"])
        self.assertEqual(self.cards["NAX6_02e"]["type"], "ENCHANTMENT")
        self.assertEqual([c["id"] for c in naxx if c["dbf_id"] is None], ["NAX6_02e"])
        # entities the boss decks use that are not collectible
        for cid in exp.EXPECTED_BOSS_DECK_ONLY:
            self.assertEqual((self.cards[cid]["set"], self.cards[cid]["collectible"], self.cards[cid]["acquisition"]),
                             ("NAXX", False, "none"))
        self.assertEqual(self.cards["NAX7_05"]["name"], "Mind Control Crystal")
        # every entity a NAXX card references is in the dataset
        for c in naxx:
            for ref in c["entourage"]:
                self.assertIn(ref, self.cards, f"{c['id']} entourage")


    def test_acquisition_values(self):
        # "unreleased" stays in the schema but no card uses it at the moment
        self.assertEqual({c["acquisition"] for c in self.cards.values()}, set(cardlib.ACQUISITIONS) - {"unreleased"})
        for c in self.cards.values():
            if not c["collectible"]:
                self.assertEqual(c["acquisition"], "none", c["id"])
        self.assertEqual(self.cards["CS2_029"]["acquisition"], "standard")
        self.assertEqual(self.cards["HERO_08"]["acquisition"], "none")
        self.assertEqual(self.cards["EX1_116t"]["acquisition"], "none")

    def test_engine_dependencies_are_kept(self):
        raw = json.loads((cardlib.SOURCE_DIR / cardlib.PINNED_SOURCES["cards_json"]["file"]).read_text(encoding="utf-8"))
        wanted = {r["id"] for r in raw if r["set"] in cardlib.INCLUDED_SETS and r["id"] not in cardlib.REMOVED_IDS}
        self.assertEqual(len(wanted), 602 + exp.EXPECTED_NAXX_ENTITIES)
        self.assertTrue(wanted <= set(self.cards))
        self.assertEqual(set(self.cards) - wanted,
                         set(exp.EXPECTED_REWARD_ONLY) | set(exp.EXPECTED_REWARD_DEPENDENCIES))
        self.assertEqual(len(self.cards), exp.EXPECTED_ENTITY_COUNT)
        for cid, kind in (("EX1_116t", "MINION"), ("CS2_102", "HERO_POWER"), ("EX1_323h", "HERO"),
                          ("GAME_005", "SPELL"), ("EX1_573t", "MINION")):
            self.assertEqual(self.cards[cid]["type"], kind)
            self.assertFalse(self.cards[cid]["collectible"])
        self.assertGreater(sum(1 for c in self.cards.values() if c["type"] == "ENCHANTMENT"), 100)

    def test_hero_power_links_and_dbf_ids(self):
        self.assertEqual(self.cards["HERO_08"]["hero_power"], "CS2_034")
        self.assertEqual(self.cards["HERO_01"]["dbf_id"], 7)
        self.assertEqual(self.cards["EX1_116"]["dbf_id"], 559)
        self.assertEqual(self.cards["CS2_029"]["dbf_id"], 315)

    def test_validator_passes(self):
        rep, _ = validate_cards.validate(self.ds)
        self.assertEqual(rep.errors, [])


class Overrides(unittest.TestCase):
    def _build(self, overrides=None, custom=None):
        with tempfile.TemporaryDirectory() as tmp:
            o, c = Path(tmp) / "overrides", Path(tmp) / "custom"
            o.mkdir(), c.mkdir()
            if overrides is not None:
                (o / "001_test.json").write_text(json.dumps(overrides), encoding="utf-8")
            if custom is not None:
                (c / "001_test.json").write_text(json.dumps(custom), encoding="utf-8")
            return cardlib.build_dataset(overrides_dir=o, custom_dir=c)[0]

    def _ov(self, **kw):
        rec = {"card_id": "EX1_116", "fields": {"cost": 5}, "expect": {"cost": 4},
               "source_url": "https://example.invalid/patch-notes", "reason": "test"}
        rec.update(kw)
        return {"schema_version": 1, "overrides": [rec]}

    def test_override_applies_and_changes_hash(self):
        base = _dataset()
        ds = self._build(overrides=self._ov())
        leeroy = cardlib.cards_by_id(ds)["EX1_116"]
        self.assertEqual(leeroy["cost"], 5)
        self.assertEqual(ds["overrides_applied"][0]["card_id"], "EX1_116")
        self.assertNotEqual(ds["content_hash"], base["content_hash"])

    def test_stale_expectation_rejected(self):
        with self.assertRaises(cardlib.CardDataError):
            self._build(overrides=self._ov(expect={"cost": 99}))

    def test_source_url_and_reason_required(self):
        for missing in ("source_url", "reason"):
            with self.subTest(missing=missing):
                doc = self._ov()
                del doc["overrides"][0][missing]
                with self.assertRaises(cardlib.CardDataError):
                    self._build(overrides=doc)

    def test_identity_fields_not_overridable(self):
        with self.assertRaises(cardlib.CardDataError):
            self._build(overrides=self._ov(fields={"id": "X"}, expect={}))

    def test_unknown_card_rejected(self):
        with self.assertRaises(cardlib.CardDataError):
            self._build(overrides=self._ov(card_id="NOPE_001"))


class CustomNamespace(unittest.TestCase):
    def _card(self, **kw):
        card = {"id": "WR_001", "dbf_id": 10_000_001, "name": "Test Whelp", "set": "WR_CUSTOM",
                "type": "MINION", "rarity": "COMMON", "cost": 1, "attack": 1, "health": 1}
        card.update(kw)
        return {"schema_version": 1, "cards": [card]}

    def _build(self, doc):
        with tempfile.TemporaryDirectory() as tmp:
            c = Path(tmp) / "custom"
            c.mkdir()
            (c / "a.json").write_text(json.dumps(doc), encoding="utf-8")
            return cardlib.build_dataset(custom_dir=c)[0]

    def test_valid_custom_card(self):
        ds = self._build(self._card())
        card = cardlib.cards_by_id(ds)["WR_001"]
        self.assertEqual(card["origin"], "custom")
        rep, _ = validate_cards.validate(ds)
        self.assertEqual([e for e in rep.errors if "WR_001" in e], [])

    def test_bad_prefix_rejected(self):
        with self.assertRaises(cardlib.CardDataError):
            self._build(self._card(id="EX1_999"))

    def test_low_dbf_rejected(self):
        with self.assertRaises(cardlib.CardDataError):
            self._build(self._card(dbf_id=1234))


if __name__ == "__main__":
    unittest.main(verbosity=2)
