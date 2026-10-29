#!/usr/bin/env python3
"""Tests of tools/arena/gen_draft_scores.py:  python tests/test_gen_draft_scores.py"""
import copy
import json
import os
import re
import subprocess
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "arena"))
import gen_draft_scores as G  # noqa: E402

LINE_RE = re.compile(r'^\{ "([A-Z]+)", "([A-Za-z0-9_]+)", (\d+) \},$')


class GenDraftScoresTests(unittest.TestCase):
    def setUp(self):
        with open(G.INPUT, "rb") as f:
            self.raw = f.read()
        self.doc = json.loads(self.raw.decode("utf-8"))

    def test_output_is_deterministic(self):
        self.assertEqual(G.generate(self.raw), G.generate(self.raw))

    def test_output_does_not_depend_on_deck_order(self):
        d = copy.deepcopy(self.doc)
        d["decks"].reverse()
        self.assertEqual(G.generate(self.raw), G.generate(json.dumps(d).encode("utf-8")))

    def test_scores_are_integers_within_bounds(self):
        entries = G.scores(G.count_copies(self.doc))
        self.assertTrue(entries)
        for cls, card, s in entries:
            self.assertIsInstance(s, int)
            self.assertTrue(1 <= s <= 100, (cls, card, s))

    def test_every_class_has_a_hundred(self):
        entries = G.scores(G.count_copies(self.doc))
        for cls in G.CLASSES:
            tops = [s for c, _, s in entries if c == cls and s == 100]
            self.assertTrue(tops, cls)

    def test_entries_are_sorted_by_class_then_id(self):
        keys = [(c, card) for c, card, _ in G.scores(G.count_copies(self.doc))]
        self.assertEqual(keys, sorted(keys))

    def test_neutral_card_gets_one_score_per_class(self):
        entries = G.scores(G.count_copies(self.doc))
        per_card = {}
        for cls, card, _ in entries:
            per_card.setdefault(card, []).append(cls)
        self.assertTrue(any(len(v) > 1 for v in per_card.values()))

    def test_rendered_lines_match_format(self):
        lines = [ln for ln in G.generate(self.raw).splitlines() if ln.startswith("{")]
        self.assertEqual(len(lines), len(G.scores(G.count_copies(self.doc))))
        for ln in lines:
            m = LINE_RE.match(ln)
            self.assertIsNotNone(m, ln)
            self.assertTrue(0 <= int(m.group(3)) <= 100)

    def test_checked_in_file_is_current(self):
        result = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "arena", "gen_draft_scores.py"),
                                 "--check"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with open(G.OUT, "rb") as f:
            have = f.read().decode("utf-8").replace("\r\n", "\n")
        self.assertEqual(have, G.generate(self.raw))


if __name__ == "__main__":
    unittest.main()
