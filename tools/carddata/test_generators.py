#!/usr/bin/env python3
"""Tests for the server SQL and addon CardDB.lua generators.

Run from the module root:  python -m unittest discover -s tools/carddata -v
"""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cardlib  # noqa: E402
import gen_addon_carddb as lua_gen  # noqa: E402
import gen_world_sql as sql_gen  # noqa: E402
import generate_all  # noqa: E402
import gencommon as gc  # noqa: E402

ROOT = gc.REPO_ROOT
BS = chr(92)  # backslash, spelled out to keep this file free of escape-sequence confusion


# ---------------------------------------------------------------------------
# A faithful port of the statement splitter in src/shared/Database/AutoUpdater.cpp
# (AutoUpdater::ExecuteUpdate), so we test what the core will really send to MySQL.
# ---------------------------------------------------------------------------

def updater_split(sql: str) -> list:
    queries, query = [], ""
    string_scope = None      # None, "'" or '"'
    comment = None           # None, "single", "multi"
    n = len(sql)
    for i, ch in enumerate(sql):
        if ch in "\r\n":
            if ch == "\n":
                query += " "
            if comment == "single":
                comment = None
            continue
        if ch == "/":
            if comment == "multi" and i > 0 and sql[i - 1] == "*":
                comment = None
                continue
            if i < n - 1 and string_scope is None and sql[i + 1] == "*":
                comment = "multi"
                continue
        if comment is not None:
            continue
        if ch == "-" and i < n - 1 and string_scope is None and sql[i + 1] == "-":
            comment = "single"
            continue
        if ch == "'":
            if string_scope is None:
                string_scope = "'"
            elif string_scope == "'" and sql[i - 1] != BS:
                string_scope = None
        if ch == '"':
            if string_scope is None:
                string_scope = '"'
            elif string_scope == '"' and sql[i - 1] != BS:
                string_scope = None
        query += ch
        if ch == ";" and string_scope is None:
            queries.append(query)
            query = ""
    if query.strip(" \t\r\n"):
        queries.append(query)
    if string_scope is not None:
        raise AssertionError("updater would end inside a string")
    return queries


# ---------------------------------------------------------------------------
# Minimal parser for the value tuples of the generated INSERT statements.
# ---------------------------------------------------------------------------

class TupleParser:
    def __init__(self, text: str):
        self.s = text
        self.i = 0

    def ws(self):
        while self.i < len(self.s) and self.s[self.i] in " \t\n":
            self.i += 1

    def expect(self, lit: str):
        self.ws()
        assert self.s.startswith(lit, self.i), f"expected {lit!r} at {self.i}: {self.s[self.i:self.i + 40]!r}"
        self.i += len(lit)

    def string(self) -> str:
        assert self.s[self.i] == "'"
        self.i += 1
        out = []
        while True:
            ch = self.s[self.i]
            if ch == "'":
                if self.s.startswith("''", self.i):
                    out.append("'")
                    self.i += 2
                    continue
                self.i += 1
                return "".join(out)
            assert ch != BS, "backslash inside a generated string"
            out.append(ch)
            self.i += 1

    def value(self):
        self.ws()
        if self.s.startswith("NULL", self.i):
            self.i += 4
            return None
        if self.s[self.i] == "'":
            return self.string()
        if self.s.startswith("CONCAT(", self.i):
            self.i += 7
            parts = []
            while True:
                parts.append(self.value())
                self.ws()
                if self.s[self.i] == ",":
                    self.i += 1
                    continue
                self.expect(")")
                return "".join(parts)
        if self.s.startswith("CHAR(", self.i):
            m = re.compile(r"CHAR\((\d+) USING utf8mb4\)").match(self.s, self.i)
            assert m, self.s[self.i:self.i + 40]
            self.i = m.end()
            return chr(int(m.group(1)))
        m = re.compile(r"\d+").match(self.s, self.i)
        assert m, self.s[self.i:self.i + 40]
        self.i = m.end()
        return int(m.group(0))

    def tuples(self) -> list:
        rows = []
        while True:
            self.expect("(")
            row = [self.value()]
            while True:
                self.ws()
                if self.s[self.i] == ",":
                    self.i += 1
                    row.append(self.value())
                else:
                    break
            self.expect(")")
            rows.append(row)
            self.ws()
            if self.s[self.i] == ",":
                self.i += 1
                continue
            self.expect(";")
            return rows


def parse_inserts(sql: str, table: str) -> list:
    rows = []
    for stmt in (q.strip() for q in updater_split(sql)):
        head = f"INSERT INTO `{table}` ("
        if stmt.startswith(head):
            values = stmt[stmt.index(") VALUES", len(head)) + len(") VALUES"):]
            rows += TupleParser(values).tuples()
    return rows


def find_lua():
    candidates = [os.environ.get("LUA"), shutil.which("lua"), shutil.which("lua5.1"), shutil.which("lua5.0"),
                  r"C:\TurtleDev\02_build\companion-recruiter-review-check\lua52.exe"]
    for c in candidates:
        if c and Path(c).exists():
            return c
    return None


def strip_lua(src: str) -> str:
    """Code with strings and comments removed (same idea as the lint in tests/lua_tests.lua)."""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("--", i):
            while i < n and src[i] != "\n":
                i += 1
        elif c in "\"'":
            q = c
            i += 1
            while i < n and src[i] != q:
                if src[i] == BS:
                    i += 1
                i += 1
            i += 1
            out.append('""')
        else:
            out.append(c)
            i += 1
    return "".join(out)


DS = None


def dataset() -> dict:
    global DS
    if DS is None:
        DS = gc.load()
    return DS


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class Determinism(unittest.TestCase):
    def test_render_is_repeatable(self):
        ds = dataset()
        self.assertEqual(sql_gen.render(ds), sql_gen.render(ds))
        self.assertEqual(lua_gen.render(ds), lua_gen.render(ds))

    def test_input_order_does_not_matter(self):
        ds = dict(dataset())
        shuffled = dict(ds, cards=list(reversed(ds["cards"])))
        self.assertEqual(sql_gen.render(ds), sql_gen.render(shuffled))
        self.assertEqual(lua_gen.render(ds), lua_gen.render(shuffled))

    def test_committed_outputs_are_current(self):
        for label, path, text in generate_all.outputs(dataset()):
            with self.subTest(output=label):
                self.assertEqual(Path(path).read_bytes().replace(b"\r\n", b"\n"), text.encode("utf-8"),
                                 f"{label} is stale: run python tools/carddata/generate_all.py")

    def test_check_mode(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "x.txt"
            self.assertFalse(gc.check_or_write(p, "a\n", True, "x"))
            self.assertTrue(gc.check_or_write(p, "a\n", False, "x"))
            self.assertTrue(gc.check_or_write(p, "a\n", True, "x"))
            self.assertFalse(gc.check_or_write(p, "b\n", True, "x"))
            self.assertEqual(p.read_bytes(), b"a\n")  # LF only

    def test_generate_all_check_passes(self):
        self.assertEqual(generate_all.main(["--check"]), 0)


class SqlEscaping(unittest.TestCase):
    CASES = [
        "Mukla's",
        "Ragnaros the Firelord",
        "it's a 'quoted' \"double\" word",
        "ends with quote'",
        "'starts with quote",
        "''",
        "back" + BS + "slash",
        "ends with backslash" + BS,
        BS + "'" + BS,
        BS + BS + "'",
        "line1\nline2\r\nline3",
        "\n",
        "",
        "semi; colon; -- not a comment /* nor this */ still text",
        "tab\there",
        "Unicode: caf\u00e9 \u2019 \u4e2d\u6587",
        "nul" + chr(0) + "byte and ctrl-z" + chr(26),
    ]

    def test_roundtrip_through_updater_splitter(self):
        for s in self.CASES:
            with self.subTest(value=s):
                literal = sql_gen.sql_str(s)
                self.assertNotIn(BS, literal)
                stmt = f"INSERT INTO `t` (`a`, `b`) VALUES ({literal}, 7);\n"
                queries = updater_split(stmt + "SELECT 1;\n")
                self.assertEqual(len(queries), 2, "one statement must stay one statement")
                (row,) = TupleParser(queries[0].split("VALUES", 1)[1].replace("\n", " ")).tuples()
                # the updater turns raw newlines into spaces, but we never emit raw newlines in strings
                self.assertEqual(row, [s, 7])
                self.assertNotIn("\n", literal)

    def test_non_bmp_is_rejected(self):
        with self.assertRaises(gc.GeneratorError):
            sql_gen.sql_str("emoji \U0001F600")

    def test_values(self):
        self.assertEqual(sql_gen.sql_val(None), "NULL")
        self.assertEqual(sql_gen.sql_val(5), "5")
        self.assertEqual(sql_gen.sql_val(True), "1")
        self.assertEqual(sql_gen.sql_val("a'b"), "'a''b'")


class WorldSql(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ds = dataset()
        cls.text = sql_gen.render(cls.ds)
        cls.statements = updater_split(cls.text)

    def test_file_properties(self):
        self.assertNotIn(BS, self.text)
        self.assertNotIn("\r", self.text)
        self.assertTrue(self.text.endswith("\n"))
        self.assertNotIn("/*", self.text.split("DROP TABLE", 1)[0], "'/*' in a comment confuses the updater")
        self.assertEqual(sql_gen.MIGRATION_NAME, "0001_hearthstone_card_template.sql")

    def test_statement_layout(self):
        kinds = [re.match(r"\s*(DROP TABLE|CREATE TABLE|INSERT INTO)\s+(?:IF EXISTS\s+)?`(\w+)`", s).groups()
                 for s in self.statements]
        n_batches = -(-len(self.ds["cards"]) // sql_gen.BATCH_ROWS)
        self.assertEqual(len(self.statements), 2 + 2 + 1 + n_batches)
        self.assertEqual(kinds[0], ("DROP TABLE", "hearthstone_card_template"))
        self.assertEqual(kinds[1], ("DROP TABLE", "hearthstone_dataset"))
        self.assertEqual({k for k in kinds[2:4]}, {("CREATE TABLE", "hearthstone_dataset"),
                                                     ("CREATE TABLE", "hearthstone_card_template")})
        self.assertTrue(all(k == ("INSERT INTO", "hearthstone_card_template") for k in kinds[5:]))
        for s in self.statements:
            self.assertLess(len(s), 30000, "statement too large")

    def test_card_rows_match_dataset(self):
        rows = parse_inserts(self.text, "hearthstone_card_template")
        self.assertEqual(len(rows), self.ds["card_count"])
        self.assertEqual(len(rows), 776)
        cols = [name for name, _ in sql_gen.COLUMNS]
        by_id = {r[cols.index("id")]: dict(zip(cols, r)) for r in rows}
        self.assertEqual(len(by_id), len(rows))
        for card in self.ds["cards"]:
            with self.subTest(card=card["id"]):
                row = by_id[card["id"]]
                self.assertEqual(row["dataset_id"], self.ds["dataset_id"])
                self.assertEqual(row["dbf_id"], card["dbf_id"])
                self.assertEqual(row["name"], card["name"])
                self.assertEqual(row["text"], card["text"])
                self.assertEqual(row["card_set"], card["set"])
                self.assertEqual(row["card_class"], card["class"])
                self.assertEqual(row["rarity"], card["rarity"])
                self.assertEqual(row["card_type"], card["type"])
                for col, key in (("cost", "cost"), ("attack", "attack"), ("health", "health"),
                                 ("durability", "durability"), ("spell_damage", "spell_damage"),
                                 ("overload", "overload"), ("race", "race"), ("hero_power", "hero_power")):
                    self.assertEqual(row[col], card[key], col)
                self.assertEqual(row["mechanics"], ",".join(card["mechanics"]))
                self.assertEqual(row["entourage"], ",".join(card["entourage"]))
                self.assertEqual(row["collectible"], 1 if card["collectible"] else 0)
                self.assertEqual(row["acquisition"], card["acquisition"])
                self.assertEqual(row["origin"], card["origin"])

    def test_acquisition_counts(self):
        rows = parse_inserts(self.text, "hearthstone_card_template")
        acq = [r[[n for n, _ in sql_gen.COLUMNS].index("acquisition")] for r in rows]
        self.assertEqual(acq.count("standard"), 378)
        self.assertEqual(acq.count("reward_only"), 4)
        self.assertEqual(acq.count("quest"), 30)
        self.assertEqual(acq.count("unreleased"), 0)
        self.assertEqual(acq.count("none"), 364)

    def test_acquisition_enum_has_quest(self):
        self.assertIn("ENUM('standard','reward_only','quest','none','unreleased')", self.text)

    def test_dataset_row(self):
        rows = parse_inserts(self.text, "hearthstone_dataset")
        self.assertEqual(rows, [[self.ds["dataset_id"], self.ds["content_hash"], self.ds["card_count"],
                                 self.ds["schema_version"]]])

    def test_special_cards_survive(self):
        rows = parse_inserts(self.text, "hearthstone_card_template")
        cols = [name for name, _ in sql_gen.COLUMNS]
        by_id = {r[1]: dict(zip(cols, r)) for r in rows}
        self.assertEqual(by_id["EX1_298"]["name"], "Ragnaros the Firelord")
        self.assertEqual(by_id["EX1_014"]["name"], "King Mukla")
        self.assertIn("\u2019", by_id["EX1_100"]["text"])            # non-ASCII kept in the SQL
        self.assertIn("\n", by_id["CS2_004"]["text"])                # newline kept
        self.assertIn("'", by_id["CS1_129"]["text"])                 # apostrophe kept
        self.assertIsNone(by_id["CS2_103e"]["dbf_id"])               # NULL dbf id
        self.assertEqual(by_id["CS2_103e"]["card_type"], "ENCHANTMENT")

    def test_column_ranges_fit(self):
        for card in self.ds["cards"]:
            sql_gen.row_values(card, self.ds["dataset_id"])  # raises on overflow

    def test_ddl_matches_insert_columns(self):
        ddl = self.text.split("CREATE TABLE `hearthstone_card_template` (", 1)[1].split("PRIMARY KEY", 1)[0]
        ddl_cols = re.findall(r"^\s+`(\w+)`", ddl, re.M)
        self.assertEqual(ddl_cols, [n for n, _ in sql_gen.COLUMNS])
        self.assertIn("DEFAULT CHARSET=utf8mb4", self.text)

    def test_length_limits_are_enforced(self):
        card = dict(self.ds["cards"][0], id="X" * 40)
        with self.assertRaises(gc.GeneratorError):
            sql_gen.row_values(card, "d")
        card = dict(self.ds["cards"][0], cost=-1)
        with self.assertRaises(gc.GeneratorError):
            sql_gen.row_values(card, "d")


class LuaText(unittest.TestCase):
    def test_lua_str_escapes(self):
        self.assertEqual(lua_gen.lua_str('a"b'), '"a' + BS + '"b"')
        self.assertEqual(lua_gen.lua_str("a" + BS + "b"), '"a' + BS + BS + 'b"')
        self.assertEqual(lua_gen.lua_str("a\nb\rc"), '"a' + BS + "nb" + BS + 'rc"')
        self.assertEqual(lua_gen.lua_str("\u2019"), '"' + BS + "226" + BS + "128" + BS + '153"')
        self.assertEqual(lua_gen.lua_str("\x01" + "5"), '"' + BS + "0015" + '"')   # 3-digit escapes
        self.assertEqual(lua_gen.lua_str("|cff|r"), '"|cff|r"')
        self.assertEqual(lua_gen.lua_str("]]--[["), '"]]--[["')

    def test_display_text(self):
        d = lua_gen.display_text
        B, I, C = lua_gen.BOLD_OPEN, lua_gen.ITALIC_OPEN, lua_gen.COLOR_CLOSE
        self.assertEqual(d(None), "")
        self.assertEqual(d("<b>Taunt</b>"), B + "Taunt" + C)
        self.assertEqual(d("Deal $3 damage.  Restore #4 Health to all  friendly characters."),
                         "Deal 3 damage. Restore 4 Health to all friendly characters.")
        self.assertEqual(d("<b>Hero Power</b> \n Deal 1 damage."), B + "Hero Power" + C + "\nDeal 1 damage.")
        self.assertEqual(d("Give <b>Taunt</b>.<i> (+2 Attack)</i>"), "Give " + B + "Taunt" + C + "." + I + " (+2 Attack)" + C)
        self.assertEqual(d("[x]<b>Charge</b>\n\n\nMore"), B + "Charge" + C + "\nMore")
        self.assertEqual(d("player\u2019s hand \u2013 ok\u2026"), "player's hand - ok...")
        self.assertEqual(d("a | b"), "a || b")
        self.assertEqual(d("<span>x</span> <b>unclosed"), "x unclosed")
        self.assertEqual(d("Ragnaros the Firelord"), "Ragnaros the Firelord")
        self.assertEqual(d("King Mukla's"), "King Mukla's")


class LuaOutput(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ds = dataset()
        cls.text = lua_gen.render(cls.ds)

    def test_ascii_only_and_lf(self):
        self.text.encode("ascii")
        self.assertNotIn("\r", self.text)
        self.assertTrue(self.text.endswith("}\n"))

    def test_lua50_compatible_constructs(self):
        code = strip_lua(self.text)
        self.assertNotIn("#", code, "'#' operator")
        self.assertNotIn("...", code, "varargs")
        self.assertNotIn("[[", code, "long strings")
        self.assertNotIn("--[[", self.text)
        self.assertNotIn("::", code)
        self.assertNotIn("goto", code)
        self.assertNotIn("//", code)
        self.assertNotIn("%", code)
        self.assertNotIn("function", code)
        # string escapes: only backslash, double quote, n, r and decimal
        for m in re.finditer(re.escape(BS) + "(.)", self.text.replace(BS + BS, "@@")):
            self.assertIn(m.group(1), '"nr0123456789', f"unsupported escape {m.group(0)!r}")
        self.assertNotIn(BS + "x", self.text.replace(BS + BS, ""))
        self.assertNotIn(BS + "z", self.text.replace(BS + BS, ""))
        for line in self.text.split("\n"):
            self.assertLess(len(line), 1000)

    def test_markup_is_gone(self):
        db = self.text
        m = re.findall(r'text="((?:[^"' + BS + BS + ']|' + BS + BS + '.)*)"', db)
        self.assertGreater(len(m), 300)
        for t in m:
            self.assertNotRegex(t, r"[<>\[\]$#]")
            self.assertEqual(t.count("|cff"), t.count("|r"), t)
            self.assertNotIn("||||", t)

    def test_contents(self):
        ds = self.ds
        included = [c for c in ds["cards"] if c["type"] != "ENCHANTMENT"
                     and cardlib.client_visible(c)]
        # 776 entities - 141 enchantments - 1 unsummoned Skeleton token (skele11)
        self.assertEqual(len(included), 634)
        for cid in ("NEW1_006", "EX1_016t", "skele11"):
            self.assertNotIn('["' + cid + '"]', self.text)
        for cid in ("FP1_028", "NAX1_01", "NAX7_05", "NAXM_001"):  # quest cards and encounter entities are in the client file
            self.assertIn('["' + cid + '"]', self.text)
        for cid in ("EX1_506a", "EX1_tk9", "CS2_101t", "EX1_614t", "PRO_001at", "Mekka4t"):  # tokens created by card effects stay
            self.assertIn('["' + cid + '"]', self.text)
        self.assertEqual(self.text.count("\n        [\""), len(included))
        self.assertIn('datasetId = "' + ds["dataset_id"] + '"', self.text)
        self.assertIn('hash = "' + ds["content_hash"][:16] + '"', self.text)
        self.assertIn('fullHash = "' + ds["content_hash"] + '"', self.text)
        self.assertNotIn('type="ENCHANTMENT"', self.text)

    def test_order(self):
        cards = {c["id"]: c for c in self.ds["cards"]}
        order = lua_gen.browse_order([c for c in self.ds["cards"] if lua_gen.included(c)])
        self.assertEqual(len(order), len(set(order)))
        self.assertEqual(len(order), 378 + 4 + 30)
        for cid in order:
            self.assertIn(cards[cid]["acquisition"], ("standard", "reward_only", "quest"))
            self.assertIn(cards[cid]["type"], ("MINION", "SPELL", "WEAPON"))
        keys = [(gc.CLASS_ORDER.index(cards[i]["class"]), cards[i]["cost"]) for i in order]
        self.assertEqual(keys, sorted(keys))
        self.assertEqual(cards[order[0]]["class"], "DRUID")
        self.assertEqual(cards[order[-1]]["class"], "NEUTRAL")
        # cards of one class and cost are alphabetical
        for a, b in zip(order, order[1:]):
            ca, cb = cards[a], cards[b]
            if (ca["class"], ca["cost"]) == (cb["class"], cb["cost"]):
                self.assertLessEqual(lua_gen.display_text(ca["name"]).lower(), lua_gen.display_text(cb["name"]).lower())

    def test_consistency_with_header_and_sql(self):
        header = (ROOT / "src" / "HsDataset.h").read_text(encoding="utf-8")
        h_id = re.search(r'kDatasetId = "([^"]+)"', header).group(1)
        h_hash = re.search(r'kDatasetHash = "([0-9a-f]+)"', header).group(1)
        lua_id = re.search(r'datasetId = "([^"]+)"', self.text).group(1)
        lua_hash = re.search(r'\bhash = "([0-9a-f]+)"', self.text).group(1)
        self.assertEqual((lua_id, lua_hash), (h_id, h_hash))
        sql_rows = parse_inserts(sql_gen.render(self.ds), "hearthstone_dataset")
        self.assertEqual(sql_rows[0][0], lua_id)
        self.assertEqual(sql_rows[0][1][:16], lua_hash)
        self.assertEqual(len(lua_hash), gc.WIRE_HASH_CHARS)

    def test_size_is_reasonable(self):
        self.assertLess(len(self.text.encode("utf-8")), 250_000)

    @unittest.skipUnless(find_lua(), "no Lua interpreter found (set LUA=...)")
    def test_loads_and_roundtrips_in_lua(self):
        lua = find_lua()
        with tempfile.TemporaryDirectory() as tmp:
            db_path = Path(tmp) / "CardDB.lua"
            db_path.write_bytes(self.text.encode("ascii"))
            script = Path(tmp) / "dump.lua"
            script.write_text(
                'dofile(arg[1])\n'
                'local ids = {}\n'
                'for id in pairs(HS_CardDB.cards) do ids[#ids + 1] = id end\n'
                'table.sort(ids)\n'
                'local function hex(s) return (string.gsub(s or "", ".", function(c) return string.format("%02x", string.byte(c)) end)) end\n'
                'print(HS_CardDB.datasetId, HS_CardDB.hash, HS_CardDB.count, #HS_CardDB.order)\n'
                'for _, id in ipairs(ids) do local c = HS_CardDB.cards[id]\n'
                '  print(id, hex(c.name), hex(c.text), tostring(c.cost), tostring(c.atk), tostring(c.hp), c.type, c.acq, c.coll)\n'
                'end\n'
                'for i, id in ipairs(HS_CardDB.order) do print("O", id) end\n',
                encoding="ascii")
            res = subprocess.run([lua, str(script), str(db_path)], capture_output=True, text=True, timeout=60)
            self.assertEqual(res.returncode, 0, res.stderr)
        lines = res.stdout.splitlines()
        head = lines[0].split("\t")
        included = sorted((c for c in self.ds["cards"] if lua_gen.included(c)), key=lambda c: c["id"])
        self.assertEqual(head[0], self.ds["dataset_id"])
        self.assertEqual(head[1], self.ds["content_hash"][:16])
        self.assertEqual(int(head[2]), len(included))
        cards = [ln.split("\t") for ln in lines[1:1 + len(included)]]
        self.assertEqual([c[0] for c in cards], [c["id"] for c in included])
        for parts, card in zip(cards, included):
            with self.subTest(card=card["id"]):
                self.assertEqual(bytes.fromhex(parts[1]).decode("utf-8"), lua_gen.display_text(card["name"]))
                self.assertEqual(bytes.fromhex(parts[2]).decode("utf-8"), lua_gen.display_text(card["text"]))
                self.assertEqual(parts[3], "nil" if card["cost"] is None else str(card["cost"]))
                self.assertEqual(parts[6], card["type"])
                self.assertEqual(parts[7], card["acquisition"])
        order = [ln.split("\t")[1] for ln in lines if ln.startswith("O\t")]
        self.assertEqual(order, lua_gen.browse_order(included))
        self.assertEqual(int(head[3]), len(order))


class Toc(unittest.TestCase):
    def test_toc_lists_carddb_before_hearthstone(self):
        toc = (ROOT / "addon" / "Hearthstone" / "Hearthstone.toc").read_text(encoding="utf-8").splitlines()
        files = [ln.strip() for ln in toc if ln.strip() and not ln.startswith("##")]
        self.assertIn("CardDB.lua", files)
        self.assertLess(files.index("HsProtocol.lua"), files.index("CardDB.lua"))
        self.assertLess(files.index("CardDB.lua"), files.index("Hearthstone.lua"))

    def test_sql_hash_stable_marker(self):
        # the SHA1 of the migration is what the core keys on: it must be a pure function of the dataset
        a = hashlib.sha1(sql_gen.render(dataset()).encode("utf-8")).hexdigest()
        b = hashlib.sha1(Path(sql_gen.OUT_PATH).read_bytes().replace(b"\r\n", b"\n")).hexdigest()
        self.assertEqual(a, b)


if __name__ == "__main__":
    unittest.main()
