#!/usr/bin/env python3
"""Tests of tools/fx/gen_effect_profiles.py:  python tools/fx/test_gen_effect_profiles.py"""
import copy
import glob
import hashlib
import json
import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_effect_profiles as G  # noqa: E402


class GenTests(unittest.TestCase):
    def setUp(self):
        with open(G.MANIFEST, "rb") as f:
            self.raw = f.read()
        self.doc = json.loads(self.raw.decode("utf-8"))

    def bad(self, mutate):
        d = copy.deepcopy(self.doc)
        mutate(d)
        with self.assertRaises(G.ManifestError):
            G.generate(json.dumps(d).encode("utf-8"))

    def test_checked_in_file_is_current(self):
        with open(G.OUT, "rb") as f:
            have = f.read().replace(b"\r\n", b"\n")
        self.assertEqual(have, G.generate(self.raw).encode("utf-8"))

    def test_manifest_is_valid_and_unverified(self):
        G.validate(self.doc)
        self.assertTrue(12 <= len(self.doc["presets"]) <= 20)
        self.assertTrue(all(p["verified"] is False or p["verified"] is True for p in self.doc["presets"]))

    def test_visual_coverage_is_curated_and_exactly_twenty_three_keys(self):
        expected = ["fireball", "bolt", "impact", "slash", "nova", "rain", "heal", "buff", "lightning",
            "silence", "transform", "summon", "destroy", "shield", "bounce", "control", "secret", "drain", "resource",
            "volley", "freeze", "stealth", "curse"]
        self.assertEqual(G.VISUALS, expected)
        self.assertEqual(set(G.coverage(self.doc)), set(expected))
        self.assertGreater(sum("visual" in c for c in self.doc["cards"].values()), 155)
        self.assertEqual(G.RESULT_TYPES, ["dmg", "heal", "dest", "sil", "frz", "tf", "buff", "sum", "bnc", "ctl", "arm", "sec", "eq", "stl", "rev"])
        self.assertEqual(self.doc["cards"]["EX1_596"]["resultVisuals"], {"dmg": "impact", "buff": "buff"})
        self.assertEqual(self.doc["cards"]["CS1_112"]["resultVisuals"], {"dmg": "nova", "heal": "heal"})
        self.assertEqual(self.doc["cards"]["CS2_061"]["resultVisuals"], {"dmg": "drain", "heal": "heal"})
        self.assertEqual(self.doc["cards"]["EX1_309"]["resultVisuals"]["heal"], "heal")
        self.assertEqual(self.doc["cards"]["EX1_309"]["visual"], "destroy")
        self.assertEqual(self.doc["cards"]["EX1_126"]["visual"], "slash")
        self.assertEqual(self.doc["cards"]["EX1_126"]["resultVisuals"], {"dmg": "slash"})
        self.assertEqual(self.doc["cards"]["EX1_277"]["visual"], "volley")
        self.assertEqual(self.doc["cards"]["EX1_384"]["visual"], "volley")
        self.assertEqual(self.doc["cards"]["DS1_183"]["visual"], "impact")
        self.assertEqual(self.doc["cards"]["DS1_183"]["resultVisuals"], {"dmg": "impact"})
        self.assertEqual(self.doc["cards"]["EX1_275"]["visual"], "freeze")
        self.assertEqual(self.doc["cards"]["EX1_128"]["resultVisuals"], {"buff": "stealth"})
        self.assertEqual(self.doc["cards"]["CS2_233"]["resultVisuals"], {"dest": "destroy", "dmg": "nova"})
        self.assertEqual(self.doc["cards"]["EX1_607"]["resultVisuals"], {"dmg": "impact", "buff": "buff"})
        self.assertEqual(self.doc["cards"]["EX1_570"]["resultVisuals"], {"buff": "buff", "arm": "shield"})
        self.assertEqual(self.doc["cards"]["EX1_277"]["tint"], [0.48, 0.38, 0.72])
        self.assertEqual(self.doc["cards"]["EX1_384"]["tint"], [1, 0.78, 0.35])
        self.assertEqual(self.doc["cards"]["EX1_128"]["tint"], [0.3, 0.35, 0.45])
        self.assertEqual(self.doc["cards"]["CS2_038"]["resultVisuals"], {"buff": "buff", "sum": "summon"})
        self.assertEqual(self.doc["cards"]["CS2_063"]["resultVisuals"], {"buff": "curse", "dest": "destroy"})
        self.assertEqual(self.doc["cards"]["CS2_074"]["resultVisuals"], {"buff": "buff"})
        self.assertEqual(self.doc["cards"]["EX1_409"]["resultVisuals"], {"buff": "buff", "eq": "buff"})
        self.assertEqual(self.doc["cards"]["EX1_544"]["resultVisuals"], {"stl": "silence", "rev": "silence"})
        self.assertNotIn("NEW1_006", self.doc["cards"])
        self.assertEqual(self.doc["cards"]["CS2_023"]["visual"], "resource")
        self.assertEqual(self.doc["cards"]["EX1_130"]["visual"], "secret")
        self.assertNotIn("visual", self.doc["cards"]["CS2_034"])
        self.assertNotIn("visual", self.doc["cards"]["DS1h_292"])
        self.assertEqual(self.doc["cards"]["CS2_061"]["visual"], "drain")  # Preserve the accepted card with the improved cue.
        self.assertEqual(self.doc["cards"]["CS2_234"]["family"], "shadow")
        self.assertEqual(self.doc["cards"]["EX1_622"]["family"], "shadow")
        self.assertEqual(self.doc["cards"]["EX1_312"]["family"], "shadow")

    def test_curated_triggered_ability_profiles_and_choose_one_order(self):
        cards = self.doc["cards"]
        for cid, result in {
            "CS2_042": {"dmg": "fireball"}, "CS2_064": {"dmg": "nova"},
            "CS2_088": {"heal": "heal"}, "EX1_016": {"ctl": "control"},
            "EX1_048": {"sil": "silence"}, "FP1_024": {"dmg": "nova"},
            "NEW1_030": {"dest": "destroy"}, "EX1_133": {"dmg": "impact"},
            "FP1_021": {"dmg": "nova"}, "Mekka2": {"heal": "heal"},
            "NAXM_001": {"dest": "destroy"}, "CS2_196": {"sum": "summon"},
            "EX1_402": {"arm": "shield"}, "NEW1_014": {"buff": "stealth"},
            "NEW1_019": {"dmg": "impact"}, "tt_004": {"buff": "buff"},
        }.items():
            self.assertEqual(cards[cid]["resultVisuals"], result, cid)
        self.assertEqual(cards["EX1_160"]["choices"], ["EX1_160b", "EX1_160a"])
        self.assertEqual(cards["NEW1_007"]["choices"], ["NEW1_007b", "NEW1_007a"])
        self.assertEqual(cards["EX1_178"]["choices"], ["EX1_178b", "EX1_178a"])
        self.assertEqual(cards["EX1_323"]["resultVisuals"], {"tf": "transform", "eq": "buff"})
        self.assertEqual(cards["EX1_362"]["resultVisuals"], {"buff": "shield"})
        self.assertEqual(cards["Mekka1"]["resultVisuals"], {"dest": "destroy"})
        self.assertEqual(cards["EX1_306"]["visual"], "resource")  # Battlecry discards a card.
        self.assertEqual(cards["EX1_006"]["resultVisuals"], {"bnc": "bounce", "sum": "summon"})
        self.assertEqual(cards["EX1_084"]["resultVisuals"], {"buff": "buff"})
        self.assertEqual(cards["NAX4_05"]["resultVisuals"], {"dest": "destroy"})
        self.assertEqual(cards["NAX1_05"]["resultVisuals"], {"dmg": "nova", "heal": "heal"})
        for cid in ("NAX6_03", "NAX6_04"):
            self.assertEqual(cards[cid]["resultVisuals"]["sum"], "summon")
        for cid in ("EX1_366", "EX1_536", "NEW1_025"):
            self.assertEqual(cards[cid]["resultVisuals"]["buff"], "buff")
        self.assertEqual(cards["EX1_154b"]["resultVisuals"], {"dmg": "impact"})
        self.assertEqual(cards["NEW1_008b"]["resultVisuals"], {"heal": "heal"})
        for cid in ("EX1_165a", "EX1_165b"):
            self.assertEqual(cards[cid]["resultVisuals"], {"tf": "transform"})
        self.assertEqual(cards["EX1_178b"]["resultVisuals"], {"buff": "buff"})
        self.assertEqual(cards["EX1_059"]["resultVisuals"], {"buff": "transform"})
        self.assertEqual(cards["EX1_564"]["resultVisuals"], {"tf": "transform"})
        self.assertEqual(cards["EX1_561"]["resultVisuals"], {"buff": "buff"})
        self.assertEqual(cards["EX1_304"]["resultVisuals"], {"dest": "destroy", "buff": "buff"})
        self.assertEqual(cards["PRO_001b"]["resultVisuals"], {"dmg": "impact"})
        for cid in ("EX1_612", "NEW1_029", "FP1_030"):
            self.assertIs(cards[cid]["cueOnPlay"], True)

    def test_every_mapped_visual_is_a_collectible_spell_in_addon_carddb(self):
        with open(os.path.join(G.ROOT, "addon", "Hearthstone", "CardDB.lua"), encoding="utf-8") as f:
            card_db = f.read()
        card_rows = re.findall(r'^\s*\["([^\"]+)"\]\s*=\s*(\{[^\r\n]*\})', card_db, re.MULTILINE)
        collectible_spell_ids = {cid for cid, row in card_rows
                                 if 'type="SPELL"' in row and re.search(r'\bcoll=1\b', row)}
        mapped_visual_ids = {cid for cid, profile in self.doc["cards"].items() if "visual" in profile}
        self.assertTrue(collectible_spell_ids <= mapped_visual_ids)
        self.assertEqual(len(collectible_spell_ids), 155)
        self.assertNotIn("NEW1_006", collectible_spell_ids)
        for cid, profile in self.doc["cards"].items():
            if "visual" not in profile:
                continue
            match = re.search(r'^\s*\["%s"\]\s*=\s*(\{[^\r\n]*\})' % re.escape(cid), card_db, re.MULTILINE)
            self.assertIsNotNone(match, "mapped visual card exists in CardDB: " + cid)
            if cid in collectible_spell_ids:
                self.assertIn('type="SPELL"', match.group(1), "collectible spell visual: " + cid)
                self.assertRegex(match.group(1), r'\bcoll=1\b', "mapped collectible spell: " + cid)

    def test_noncollectible_profiles_resolve_to_carddb_and_are_not_equip_or_summon_cues(self):
        with open(os.path.join(G.ROOT, "addon", "Hearthstone", "CardDB.lua"), encoding="utf-8") as f:
            card_db = f.read()
        rows = {cid: row for cid, row in re.findall(r'^\s*\["([^\"]+)"\]\s*=\s*(\{[^\r\n]*\})', card_db, re.MULTILINE)}
        for cid, profile in self.doc["cards"].items():
            if "visual" not in profile or cid in rows and 'type="SPELL"' in rows[cid] and re.search(r'\bcoll=1\b', rows[cid]):
                continue
            self.assertIn(cid, rows, "profile source exists in CardDB: " + cid)
        for cid in ("CS2_042", "CS2_097", "EX1_133", "EX1_366", "FP1_021"):
            self.assertIn("resultVisuals", self.doc["cards"][cid], cid)
        for cid in ("EX1_306",):
            self.assertIn("visual", self.doc["cards"].get(cid, {}), cid)

    def test_every_user_list_id_has_a_curated_profile_or_exclusion_reason(self):
        fixture_path = os.path.join(G.ROOT, "data", "fx", "ability_profile_coverage.json")
        with open(fixture_path, encoding="utf-8") as f:
            fixture = json.load(f)
        requested = fixture["requested"]
        excluded = fixture["excluded"]
        self.assertEqual(len(requested), len(set(requested)), "fixture IDs are unique")
        self.assertEqual(len(requested), 229)
        self.assertEqual(hashlib.sha256("\n".join(sorted(requested)).encode("utf-8")).hexdigest(),
                         "9efc56c11446286d3c6712d9931c67cdf5d6bacdb1cf9068d88c56596aacc5af",
                         "review the explicit user ID list when updating this fixture")
        self.assertTrue(all(isinstance(reason, str) and reason.strip() for reason in excluded.values()))
        rows = {}
        with open(os.path.join(G.ROOT, "addon", "Hearthstone", "CardDB.lua"), encoding="utf-8") as f:
            rows = {cid: row for cid, row in re.findall(r'^\s*\["([^\"]+)"\]\s*=\s*(\{[^\r\n]*\})', f.read(), re.MULTILINE)}
        covered = {cid for cid in requested if cid in self.doc["cards"] and
                   ("visual" in self.doc["cards"][cid] or "choices" in self.doc["cards"][cid])}
        self.assertTrue(set(requested) <= set(rows), "every fixture ID is a CardDB entry")
        self.assertEqual(set(requested), covered | set(excluded), "every requested ID is mapped or explained")
        self.assertFalse(covered & set(excluded), "an ID cannot be both covered and excluded")
        for cid in covered:
            profile = self.doc["cards"][cid]
            self.assertTrue(profile.get("name"), "profile has a display name: " + cid)
            self.assertIn(profile.get("family"), G.FAMILIES, "profile has a semantic family: " + cid)
        self.assertEqual(set(excluded), {"CS2_221", "GAME_006"})

        # Keep dispatch metadata in the exact order registered by the server scripts.
        server_choices = {}
        for path in glob.glob(os.path.join(G.ROOT, "src", "match", "*.cpp")):
            with open(path, encoding="utf-8") as f:
                for parent, first, second in re.findall(
                        r't\.Add\("([^\"]+)"\)\.ChooseOne\("([^\"]+)",\s*"([^\"]+)"\)', f.read()):
                    server_choices[parent] = [first, second]
        profile_choices = {cid: profile["choices"] for cid, profile in self.doc["cards"].items()
                           if "choices" in profile}
        self.assertEqual(len(server_choices), 10)
        self.assertEqual(profile_choices, server_choices)

    def test_rejects_bad_presets(self):
        self.bad(lambda d: d["presets"][0].update(kind="beam"))
        self.bad(lambda d: d["presets"][0].update(dur=99))
        self.bad(lambda d: d["presets"][0].update(scale=0))
        self.bad(lambda d: d["presets"][0].update(pos=[1, 2]))
        self.bad(lambda d: d["presets"][0].update(tint=[2, 0, 0]))
        self.bad(lambda d: d["presets"][0].update(fallback="sparkle"))
        self.bad(lambda d: d["presets"][0].update(size=5))
        self.bad(lambda d: d["presets"][0].update(ext=0))
        self.bad(lambda d: d["presets"][0].update(ext=500))
        self.bad(lambda d: d["presets"].append(copy.deepcopy(d["presets"][0])))

    def test_rejects_bad_references(self):
        self.bad(lambda d: d["families"]["fire"].update(missile="nope"))
        self.bad(lambda d: d["families"]["fire"].update(missile="fire_impact"))   # not a missile
        self.bad(lambda d: d["families"]["fire"].update(impact="fireball"))       # a missile as impact
        self.bad(lambda d: d["cards"]["CS2_029"].update(family="plasma"))
        self.bad(lambda d: d["cards"]["CS2_029"].update(impact="nope"))
        self.bad(lambda d: d["families"].pop("generic"))

    def test_ext_and_size_are_optional_and_generated(self):
        G.validate(self.doc)
        d = copy.deepcopy(self.doc)
        for p in d["presets"]:
            p.pop("size", None)
            p.pop("ext", None)
        G.validate(d)                               # neither is required
        self.assertTrue(any("ext" in p for p in self.doc["presets"]))
        self.assertTrue(all("size" not in p for p in self.doc["presets"]))   # sizes come from the anchor
        text = G.generate(self.raw)
        self.assertIn("ext = 1.92", text)

    def test_card_can_switch_a_role_off(self):
        text = G.generate(self.raw)
        self.assertIn("EX1_309 = { missile = false", text)

    def test_visuals_flight_tint_scale_and_result_maps_are_generated_and_bounded(self):
        text = G.generate(self.raw)
        self.assertIn('CS2_029 = { visual = "fireball", flight = 0.34', text)
        representatives = {"CS2_029": "fireball", "CS2_024": "bolt", "CS1_130": "impact",
            "CS2_072": "slash", "CS2_025": "nova", "CS2_028": "rain", "CS2_007": "heal",
            "CS2_087": "buff", "EX1_238": "lightning", "EX1_332": "silence", "CS2_022": "transform",
            "CS2_027": "summon", "EX1_312": "destroy", "EX1_371": "shield", "EX1_144": "bounce",
            "CS1_113": "control", "EX1_130": "secret", "CS2_061": "drain", "CS2_023": "resource"}
        representatives.update({"DS1_183": "impact", "EX1_277": "volley", "EX1_384": "volley", "EX1_275": "freeze",
            "EX1_128": "stealth", "CS2_063": "curse"})
        for cid, visual in representatives.items():
            self.assertIn('visual = "%s"' % visual, text)
            self.assertIn(cid + " = {", text)
        self.assertIn('tint = { 0.3, 1, 0.35 }', text) # Soulfire recolours the neutral fire core.
        self.assertIn("fxScale = 1.55", text)         # Pyroblast is a larger projectile.
        self.assertIn('resultVisuals = { dmg = "nova", heal = "heal" }', text)
        self.assertIn('resultVisuals = { buff = "buff", sum = "summon" }', text)
        self.assertIn('resultVisuals = { dest = "destroy", buff = "curse" }', text)
        self.assertIn('resultVisuals = { stl = "silence", rev = "silence" }', text)
        self.assertIn('EX1_596 = { visual = "impact", flight = 0.3, resultVisuals = { dmg = "impact", buff = "buff" }', text)
        self.bad(lambda d: d["cards"]["CS2_029"].update(visual="frostbolt"))
        self.bad(lambda d: d["cards"]["CS2_029"].update(visual="barrier"))
        self.bad(lambda d: d["cards"]["CS2_029"].update(flight=0))
        self.bad(lambda d: d["cards"]["CS2_029"].update(flight=-0.1))
        self.bad(lambda d: d["cards"]["CS2_029"].update(flight=0.099))
        self.bad(lambda d: d["cards"]["CS2_029"].update(flight=float("nan")))
        self.bad(lambda d: d["cards"]["CS2_029"].update(flight=float("inf")))
        self.bad(lambda d: d["cards"]["CS2_029"].update(flight=-float("inf")))
        self.bad(lambda d: d["cards"]["CS2_029"].update(flight=1.01))
        self.bad(lambda d: d["cards"]["CS2_029"].pop("flight"))
        self.bad(lambda d: d["cards"]["CS2_029"].pop("visual"))
        d = copy.deepcopy(self.doc)
        d["cards"]["CS2_029"]["flight"] = 0.1
        self.assertIn('flight = 0.1', G.generate(json.dumps(d).encode("utf-8")))
        self.bad(lambda d: d["cards"]["CS2_029"].update(fxScale=2.01))
        self.bad(lambda d: d["cards"]["CS2_029"].update(fxScale=float("inf")))
        self.bad(lambda d: d["cards"]["CS2_029"].update(tint=[float("nan"), 0, 0]))
        self.bad(lambda d: d["cards"]["CS2_034"].update(tint=[1, 0, 0]))
        self.bad(lambda d: d["cards"]["CS2_029"].update(visual="rain", tint=[0, 0, 2]))
        self.bad(lambda d: d["cards"]["CS1_112"].update(resultVisuals={"draw": "resource"}))
        self.bad(lambda d: d["cards"]["CS1_112"].update(resultVisuals={"heal": "unregistered"}))
        self.bad(lambda d: d["cards"]["CS1_112"].update(resultVisuals={"secret_reveal": "secret"}))
        self.bad(lambda d: d["cards"]["EX1_160"].update(choices=["missing", "EX1_160a"]))
        self.bad(lambda d: d["cards"]["CS1_112"].pop("flight"))
        self.bad(lambda d: d["cards"]["CS1_112"].pop("visual"))

    def test_checksum_ignores_line_endings(self):
        lf = self.raw.replace(b"\r\n", b"\n")
        self.assertEqual(G.checksum(lf), G.checksum(lf.replace(b"\n", b"\r\n")))


if __name__ == "__main__":
    unittest.main()
