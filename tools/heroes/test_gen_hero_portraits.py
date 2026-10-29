import copy
import json
import unittest
from gen_hero_portraits import ROOT, SOURCE, CLASSES, generate


class HeroPortraitGeneratorTests(unittest.TestCase):
    def setUp(self):
        self.data = json.loads(SOURCE.read_text(encoding='utf-8-sig'))

    def test_output_covers_the_catalog_and_input_is_unchanged(self):
        before = copy.deepcopy(self.data)
        lua, rows = generate(self.data)
        self.assertEqual(before, self.data)
        self.assertEqual(len(rows.splitlines()), self.data['count'] + 1)
        for cls in CLASSES:
            for e in self.data['classes'][cls]:
                self.assertIn('id = "%s"' % e['card_id'], lua)
                self.assertIn('{"%s", "%s"' % (cls, e['card_id']), rows)

    def test_every_class_default_is_the_classic_hero(self):
        for i, cls in enumerate(CLASSES):
            self.assertTrue(self.data['classes'][cls][0]['default'])

    def test_duplicate_id_is_refused(self):
        c = self.data['classes']
        c['MAGE'][1]['card_id'] = c['MAGE'][2]['card_id']
        with self.assertRaises(AssertionError):
            generate(self.data)

    def test_id_shared_between_classes_is_refused(self):
        c = self.data['classes']
        c['MAGE'][1]['card_id'] = c['PRIEST'][1]['card_id']
        with self.assertRaises(AssertionError):
            generate(self.data)

    def test_two_defaults_are_refused(self):
        self.data['classes']['MAGE'][1]['default'] = True
        with self.assertRaises(AssertionError):
            generate(self.data)

    def test_overlong_id_is_refused(self):
        self.data['classes']['MAGE'][1]['card_id'] = 'HERO_08_this_id_is_much_too_long_for_it'
        with self.assertRaises(AssertionError):
            generate(self.data)

    def test_count_mismatch_is_refused(self):
        self.data['count'] += 1
        with self.assertRaises(AssertionError):
            generate(self.data)

    def test_generated_files_are_current(self):
        lua, rows = generate(self.data)
        self.assertEqual((ROOT / 'addon/Hearthstone/HeroPortraits.lua').read_text(encoding='utf-8'), lua)
        self.assertEqual((ROOT / 'src/heroes/HsHeroRows.inc').read_text(encoding='utf-8'), rows)


if __name__ == '__main__':
    unittest.main()
