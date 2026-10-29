#!/usr/bin/env python3
"""Tests for the addon Battlegrounds card generator (gen_bg_lua.py).

Run from the repository root with either:
    python -I -m unittest discover -s tools/battlegrounds -v
    python -I tools/battlegrounds/test_lua.py

The tests on the real dataset are skipped when data/battlegrounds/bg.normalized.json is missing (it is git-ignored:
python -I tools/battlegrounds/import_bg.py rebuilds it).
"""

from __future__ import annotations

import contextlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bglib  # noqa: E402
import gen_bg_cpp  # noqa: E402
import gen_bg_lua as gen  # noqa: E402


def card(card_id, **kw):
    """A minimal pool minion record with a golden twin."""
    rec = {"id": card_id, "dbf_id": 1, "name": "Name", "role": "pool_minion", "type": "MINION", "slice": "15A",
           "tier": 2, "attack": 3, "health": 4, "tribes": [], "mechanics": [], "text": None,
           "golden": {"id": card_id + "_G", "dbf_id": 2, "attack": 6, "health": 8, "mechanics": [], "text": None}}
    rec.update(kw)
    return rec


def dataset(*cards):
    return {"dataset_id": "test", "schema_version": 1, "content_hash": "0" * 64, "cards": list(cards)}


def find_lua():
    candidates = [os.environ.get("LUA"), shutil.which("lua"), shutil.which("lua5.1"), shutil.which("lua5.0"),
                  r"C:\TurtleDev\02_build\companion-recruiter-review-check\lua52.exe"]
    for c in candidates:
        if c and Path(c).exists():
            return c
    return None


def rows_in(text, name):
    """The ``["id"] = { ... },`` rows inside the ``HS_BgCardDB`` block ``name`` (cards / heroes / powers)."""
    m = re.search(r'^\s*%s = \{\n(.*?)^\s*\},$' % re.escape(name), text, re.MULTILINE | re.DOTALL)
    if not m:
        return {}
    return dict(re.findall(r'^\s*\["([^"]+)"\] = \{ (.*) \},$', m.group(1), re.MULTILINE))


class Heroes(unittest.TestCase):
    def hero(self, hid="HID", **kw):
        rec = {"id": hid, "dbf_id": 1, "name": "Hero", "role": "hero", "type": "HERO", "health": 55,
               "hero_power": "PID", "slice": None}
        rec.update(kw)
        return rec

    def power(self, pid="PID", **kw):
        rec = {"id": pid, "dbf_id": 2, "name": "Power", "role": "hero_power", "type": "HERO_POWER",
               "cost": 1, "text": "<b>Passive</b>\nDo a thing.", "slice": None}
        rec.update(kw)
        return rec

    def test_hero_and_power_block(self):
        text = gen.render(dataset(self.hero(), self.power()),
                          armor={"curated": ["HID"], "armor_tier": {"HID": 4}})
        self.assertIn('["HID"] = { name="Hero", health=55, armor=4, power="PID" },', text)
        self.assertIn('["PID"] = { name="Power", cost=1, text="|cffffd100Passive|r\\nDo a thing." },', text)
        self.assertIn("heroCount = 1,", text)
        self.assertIn("powerCount = 1,", text)
        # a hero and a power are not part of the card table
        self.assertNotIn('["HID"] = { dbf', text)

    def test_hero_health_fallback_and_empty_text(self):
        text = gen.render(dataset(self.hero(health=0), self.power(cost=0, text=None)),
                          armor={"curated": ["HID"], "armor_tier": {"HID": 1}})
        self.assertIn('["HID"] = { name="Hero", health=40, armor=1, power="PID" },', text)
        self.assertIn('["PID"] = { name="Power", cost=0 },', text)      # no text field when the power has none

    def test_curated_hero_missing_from_dataset_is_skipped(self):
        text = gen.render(dataset(card("A")), armor={"curated": ["NOPE"], "armor_tier": {"NOPE": 1}})
        self.assertIn("heroCount = 0,", text)
        self.assertIn("powerCount = 0,", text)

    def test_missing_power_fails(self):
        with self.assertRaises(gen.GenError):
            gen.render(dataset(self.hero()), armor={"curated": ["HID"], "armor_tier": {"HID": 1}})


class Text(unittest.TestCase):
    def test_display_text(self):
        self.assertEqual(gen.display_text("<b>Taunt</b>"), "|cffffd100Taunt|r")
        self.assertEqual(gen.display_text("<i>reminder</i>"), "|cffb8b8b8reminder|r")
        self.assertEqual(gen.display_text(None), "")
        self.assertEqual(gen.display_text("Gain a\n<b>Blood Gem</b>."), "Gain a\n|cffffd100Blood Gem|r.")
        self.assertEqual(gen.display_text("Summon a 2/3 Turtle with <b>Taunt</b>."),
                         "Summon a 2/3 Turtle with |cffffd100Taunt|r.")
        self.assertEqual(gen.display_text("a  |  b"), "a || b")   # a literal pipe is doubled, blanks collapsed
        self.assertEqual(gen.display_text("[x]Deal 3 damage."), "Deal 3 damage.")

    def test_fill_text(self):
        self.assertEqual(gen.fill_text("BG24_Quest_313", "<b>Quest:</b> Buy {0} minions.", 7),
                         "|cffffd100Quest:|r Buy 7 minions.")
        self.assertEqual(gen.fill_text("BG24_Reward_130", "Add '{0}' to your hand."), "Add 'Lightfang Enforcer' to your hand.")
        self.assertIn("minions of one of the game's minion types", gen.fill_text("BG24_Reward_134", "get 2 random {0}."))
        self.assertNotIn("{", gen.fill_text("BG24_Quest_151", "Play {0} {2} or {3}.", 5))

    def test_lua_str(self):
        self.assertEqual(gen.lua_str('a"b\\c\nd'), '"a\\"b\\\\c\\nd"')
        self.assertTrue(gen.lua_str("\u00e9").endswith('"'))     # non-ASCII goes through \ddd escapes
        self.assertIn("\\195", gen.lua_str("\u00e9"))
        self.assertNotIn("\u00e9", gen.lua_str("\u00e9"))


class Keywords(unittest.TestCase):
    def test_mechanics(self):
        self.assertEqual(gen.keywords(["TAUNT", "MODULAR"], None, False), ["TAUNT", "MAGNETIC"])
        self.assertEqual(gen.keywords(["DIVINE_SHIELD", "POISONOUS", "REBORN"], None, False),
                         ["DIVINE_SHIELD", "POISONOUS", "REBORN"])   # KEYWORD_ORDER
        self.assertEqual(gen.keywords(["BATTLECRY"], None, False), [])

    def test_text_keywords(self):
        self.assertEqual(gen.text_keywords("<b>Taunt</b>\n<b>Battlecry:</b> Give a minion <b>Taunt</b>."), ["TAUNT"])
        self.assertEqual(gen.text_keywords("<b>Deathrattle:</b> Summon a 2/3 Turtle with <b>Taunt</b>."), [])
        self.assertEqual(gen.text_keywords(None), [])

    def test_golden_windfury_is_mega(self):
        self.assertEqual(gen.keywords(["WINDFURY"], "<b>Windfury</b>", False), ["WINDFURY"])
        self.assertEqual(gen.keywords(["WINDFURY"], "<b>Mega-Windfury</b>", True), ["MEGA_WINDFURY"])
        # a golden without the Mega-Windfury text keeps Windfury (Zapp's render override)
        self.assertEqual(gen.keywords(["WINDFURY"], "<b>Windfury</b>", True), ["WINDFURY"])
        self.assertEqual(gen.keywords(["VENOMOUS"], "<b>Venomous</b>", False), ["VENOMOUS"])


class Entries(unittest.TestCase):
    def test_normal_and_golden(self):
        rows = dict(gen.card_entries(card("X1", name="Rat", text="<b>Taunt</b>", tribes=["BEAST"])))
        self.assertIn("X1", rows)
        self.assertIn("X1_G", rows)
        normal = rows["X1"]
        self.assertIn('name="Rat"', normal)
        self.assertIn('text="|cffffd100Taunt|r"', normal)
        self.assertIn("tier=2", normal)
        self.assertIn("atk=3", normal)
        self.assertIn("hp=4", normal)
        self.assertIn('kw={"TAUNT"}', normal)
        self.assertIn('tribes={"BEAST"}', normal)
        self.assertIn('golden="X1_G"', normal)
        self.assertNotIn("g=1", normal)
        self.assertNotIn("token=1", normal)
        self.assertIn("g=1", rows["X1_G"])
        self.assertIn("atk=6", rows["X1_G"])
        self.assertNotIn("golden=", rows["X1_G"])

    def test_token_flag_and_no_golden(self):
        rows = dict(gen.card_entries(card("T", role="token", golden=None)))
        self.assertEqual(len(rows), 1)
        self.assertIn("token=1", rows["T"])
        self.assertNotIn("golden=", rows["T"])

    def test_tier_out_of_range(self):
        with self.assertRaises(gen.GenError):
            gen.card_entries(card("X2", tier=7))

    def test_selection(self):
        ds = dataset(card("A"), card("T", role="token", slice="15A"), card("S", role="token", type="SPELL"),
                     card("O", role="token", slice=None), card("H", role="hero"))
        text = gen.render(ds)
        self.assertTrue(text.startswith("-- GENERATED by tools/battlegrounds/gen_bg_lua.py"))
        self.assertIn("Do not edit by hand", text)
        self.assertIn("1 pool minions, 1 minion tokens and 1 spells", text)
        self.assertIn('["A"] =', text)
        self.assertIn('["T"] =', text)
        self.assertIn('["S"] =', text)          # a 15A spell is in the addon table too
        self.assertNotIn('["O"] =', text)
        self.assertNotIn('["H"] =', text)

    def test_duplicate_id_fails(self):
        with self.assertRaises(gen.GenError):
            gen.render(dataset(card("A"), card("A")))

    def test_byte_stable_and_check_mode(self):
        ds = dataset(card("A"), card("B", tribes=["BEAST"]))
        self.assertEqual(gen.render(ds), gen.render(ds))
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            data = Path(tmp) / "bg.json"
            out = Path(tmp) / "out.lua"
            data.write_text(json.dumps(ds), encoding="utf-8")
            self.assertEqual(gen.main(["--data", str(data), "--out", str(out), "--check"]), 1)   # missing file
            self.assertEqual(gen.main(["--data", str(data), "--out", str(out)]), 0)
            self.assertEqual(gen.main(["--data", str(data), "--out", str(out), "--check"]), 0)
            out.write_bytes(out.read_bytes() + b"-- edit\n")
            self.assertEqual(gen.main(["--data", str(data), "--out", str(out), "--check"]), 1)
            self.assertEqual(gen.main(["--data", str(Path(tmp) / "missing.json"), "--out", str(out)]), 2)
            self.assertEqual(gen.main(["--data", str(Path(tmp) / "missing.json"), "--out", str(out), "--check"]), 2)
            self.assertEqual(gen.main(["--data", str(data), "--out", str(out)]), 0)
            self.assertNotIn(b"\r", out.read_bytes())


@unittest.skipUnless(bglib.NORMALIZED_PATH.is_file(), "bg.normalized.json missing (run import_bg.py)")
class RealDataset(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ds = bglib.load_dataset()
        cls.text = gen.render(cls.ds)
        cls.rows = rows_in(cls.text, "cards")
        cls.hero_rows = rows_in(cls.text, "heroes")
        cls.power_rows = rows_in(cls.text, "powers")
        cls.quest_rows = rows_in(cls.text, "quests")
        cls.reward_rows = rows_in(cls.text, "rewards")

    def test_counts(self):
        self.assertEqual(len(self.rows), 426)   # the 29 Darkmoon Prizes
        self.assertEqual(sum(1 for r in self.rows.values() if "token=1" in r), 34)
        self.assertEqual(sum(1 for r in self.rows.values() if "g=1" in r), 187)
        self.assertEqual(sum(1 for r in self.rows.values() if "golden=" in r), 187)
        self.assertIn("count = 426,", self.text)
        self.assertIn("poolCount = 167,", self.text)
        self.assertIn("tokenCount = 72,", self.text)

    def test_golden_links_resolve(self):
        for card_id, row in self.rows.items():
            m = re.search(r'golden="([^"]+)"', row)
            if m:
                self.assertIn(m.group(1), self.rows, f"{card_id} links to a missing golden")
            if "g=1" in row:
                self.assertNotIn("golden=", row)

    def test_keyword_totals_match_the_engine_table(self):
        # Same rule as gen_bg_cpp.py, so the totals must agree: 4 Windfury (normal), 1 Mega-Windfury (golden), 6 Magnetic.
        normal_windfury = sum(1 for r in self.rows.values() if "g=1" not in r and '"WINDFURY"' in r)
        golden_mega = sum(1 for r in self.rows.values() if "g=1" in r and '"MEGA_WINDFURY"' in r)
        magnetic = sum(1 for r in self.rows.values() if '"MAGNETIC"' in r)
        self.assertEqual(normal_windfury, 4)
        self.assertEqual(golden_mega, 1)   # golden Zapp keeps Windfury (render override)
        self.assertEqual(magnetic, 6)      # 3 Magnetic Mechs, normal and golden
        cpp = gen_bg_cpp.build_inc(self.ds)
        cpp_rows = [line for line in cpp.splitlines() if line.startswith("        {")]
        self.assertEqual(sum(1 for r in cpp_rows if re.search(r"kKwWindfury", r)), 4)
        self.assertEqual(sum(1 for r in cpp_rows if "kKwMegaWindfury" in r), 1)
        self.assertEqual(sum(1 for r in cpp_rows if "kKwMagnetic" in r), 3)

    def test_known_row(self):
        rat = self.rows["BG19_010"]
        self.assertIn('name="Sewer Rat"', rat)
        self.assertIn('text="|cffffd100Deathrattle:|r Summon a 2/3 Turtle with |cffffd100Taunt|r."', rat)
        self.assertIn("tier=2", rat)
        self.assertIn("atk=3", rat)
        self.assertIn("hp=2", rat)
        self.assertIn('tribes={"BEAST"}', rat)
        self.assertIn('golden="BG19_010_G"', rat)
        self.assertNotIn("kw=", rat)       # Deathrattle is not a keyword icon; Taunt is later in the text
        half = self.rows["BG19_010t"]
        self.assertIn('kw={"TAUNT"}', half)   # Taunt from the leading text, the data lists no mechanic
        self.assertIn("token=1", half)

    def test_committed_file_matches(self):
        self.assertTrue(gen.OUT_PATH.is_file(), "run gen_bg_lua.py")
        expected = self.text.encode("utf-8")
        self.assertEqual(gen.OUT_PATH.read_bytes(), expected)

    def test_hero_and_power_counts(self):
        armor = gen.load_armor()
        self.assertEqual(len(self.hero_rows), 86)   # Tickatus
        self.assertEqual(len(self.power_rows), 102)  # their powers, the 13 extra choice powers, Cariel's 3 stances
        self.assertIn("heroCount = 86,", self.text)
        self.assertIn("powerCount = 102,", self.text)
        self.assertEqual(set(self.hero_rows), set(armor["curated"]))
        linked = [m for row in self.hero_rows.values() for m in re.findall(r'power="([^"]+)"', row)]
        self.assertEqual(set(self.power_rows), set(linked) | set(armor["extra_powers"]) | {p[0] for p in gen.SYNTHETIC_POWERS})
        for hid, row in self.hero_rows.items():
            self.assertIn(f"armor={armor['armor_tier'][hid]},", row)

    def test_known_hero(self):
        patchwerk = self.hero_rows["TB_BaconShop_HERO_34"]
        self.assertIn('name="Patchwerk"', patchwerk)
        self.assertIn("health=60", patchwerk)
        self.assertIn("armor=2", patchwerk)
        self.assertIn('power="TB_BaconShop_HP_035"', patchwerk)

    def test_known_power(self):
        power = self.power_rows["TB_BaconShop_HP_035"]
        self.assertIn('name="All Patched Up"', power)
        self.assertIn("cost=0", power)
        self.assertIn('text="|cffffd100Passive|r\\nStart with 60 Health\\ninstead of 40."', power)

    def test_quest_and_reward_blocks(self):
        self.assertEqual(len(self.quest_rows), 15)
        self.assertEqual(len(self.reward_rows), 35)
        self.assertIn("questCount = 15,", self.text)
        self.assertIn("rewardCount = 35,", self.text)
        self.assertIn("number=10", self.quest_rows["BG24_Quest_112"])
        self.assertIn("10 times.", self.quest_rows["BG24_Quest_112"])
        self.assertIn("pool=1", self.quest_rows["BG24_Quest_313"])
        self.assertNotIn("pool=1", self.quest_rows["BG24_Quest_318"])       # not in the 24.2 offer
        self.assertIn("Lightfang Enforcer", self.reward_rows["BG24_Reward_130"])
        self.assertIn("pool=1", self.reward_rows["BG24_Reward_107"])
        self.assertNotIn("pool=1", self.reward_rows["BG24_Reward_311"])      # Another Hidden Body, removed 24.2.1

    def test_lua_interpreter_loads_the_file(self):
        lua = find_lua()
        if not lua:
            self.skipTest("no Lua interpreter found")
        with tempfile.TemporaryDirectory() as tmp:
            script = Path(tmp) / "check.lua"
            script.write_text(
                'dofile(arg[1])\n'
                'local n, g, t = 0, 0, 0\n'
                'for id, c in pairs(HS_BgCardDB.cards) do\n'
                '  n = n + 1\n'
                '  if c.g then g = g + 1 end\n'
                '  if c.token then t = t + 1 end\n'
                '  if c.golden and not HS_BgCardDB.cards[c.golden] then error(id .. " golden missing") end\n'
                'end\n'
                'local h, p = 0, 0\n'
                'for id, hero in pairs(HS_BgCardDB.heroes) do\n'
                '  h = h + 1\n'
                '  if not HS_BgCardDB.powers[hero.power] then error(id .. " power missing") end\n'
                'end\n'
                'for id, pw in pairs(HS_BgCardDB.powers) do p = p + 1 end\n'
                'print(HS_BgCardDB.count, n, g, t, HS_BgCardDB.poolCount, HS_BgCardDB.tokenCount, h, p)\n',
                encoding="ascii")
            res = subprocess.run([lua, str(script), str(gen.OUT_PATH)], capture_output=True, text=True, timeout=60)
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertEqual(res.stdout.split(), ["426", "426", "187", "34", "167", "72", "86", "102"])


if __name__ == "__main__":
    unittest.main()
