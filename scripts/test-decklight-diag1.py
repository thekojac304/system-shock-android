"""Deterministic read-only audit of the eight POST-ONLY deck diagnostics.

Requires the user's owned resource copies under ignored dist/. No resource
bytes, generated profiles, or diagnostic images are committed by this test.
"""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import unittest

import decklight_diag1 as diag


ROOT = Path(__file__).resolve().parents[1]
OWNED = ROOT / 'dist/audio2/owned-ARCHIVE.DAT'
RULES = ROOT / 'profiles/horror-author1/rules.json'
OUTPUT = ROOT / 'dist/gpu-grove1-decklight-diag1/decklight-diagnostics.json'
EXPECTED = {
    'Reactor': (344, 102, 506, 488, 262, 426),
    'Research': (434, 118, 856, 997, 232, 150),
    'Maintenance': (311, 105, 594, 533, 300, 660),
    'Storage': (337, 41, 355, 319, 149, 33),
    'Flight Deck': (667, 159, 745, 1237, 200, 585),
    'Security': (412, 128, 290, 319, 1316, 166),
    'Bridge': (303, 75, 446, 486, 147, 167),
}


@unittest.skipUnless(OWNED.exists(), 'owned resources are not present')
class FrozenDeckDiagnostic(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules_before = RULES.read_bytes()
        with contextlib.redirect_stdout(io.StringIO()):
            diag.main()
        cls.first = OUTPUT.read_bytes()
        with contextlib.redirect_stdout(io.StringIO()):
            diag.main()
        cls.second = OUTPUT.read_bytes()
        cls.data = json.loads(cls.second)

    def test_no_rule_mutation_and_deterministic_output(self):
        self.assertEqual(self.first, self.second)
        self.assertEqual(self.rules_before, RULES.read_bytes())
        self.assertEqual(hashlib.sha256(self.rules_before).hexdigest(),
                         self.data['input_sha256']['rules'])

    def test_frozen_pipeline_first_failure(self):
        self.assertEqual(set(self.data['decks']), set(EXPECTED) | {'Engineering'})
        for deck, row in self.data['decks'].items():
            if deck == 'Engineering':
                self.assertEqual(row['frozen_first_failure'], 'ValueError: Unresolved texture slot')
                self.assertEqual(row['invalid_texture_slots'], [[2109, 'wall', 54, 'slot-out-of-range']])
                self.assertIsNone(row['source_counts'])
            else:
                self.assertEqual(row['frozen_first_failure'], f"KeyError: '{row['level']}'")

    def test_source_acceptance_and_receiver_classification(self):
        for deck, expected in EXPECTED.items():
            row = self.data['decks'][deck]
            candidates, accepted, full, partial, blocked, unsupported = expected
            self.assertEqual((row['source_counts']['candidate'], row['source_counts']['accepted']),
                             (candidates, accepted), deck)
            self.assertEqual(row['source_counts']['policy']['local_light'], accepted, deck)
            self.assertEqual(tuple(row['receiver_counts'][s] for s in
                                   ('full', 'partial', 'blocked', 'unsupported')),
                             (full, partial, blocked, unsupported), deck)
            self.assertEqual(sum(row['receiver_counts'].values()),
                             full + partial + blocked + unsupported, deck)
            self.assertEqual(row['over_budget_relationships'], 0, deck)
            self.assertEqual(row['complexity_bound_failures'], 0, deck)
            self.assertLessEqual(row['receiver_covered_surfaces'],
                                 row['receiver_surface_counts']['full_or_partial'], deck)

    def test_material_inventory_keeps_unreviewed_fixture_ids_out_of_rules(self):
        reviewed = set(diag.BASELINE)
        self.assertEqual(reviewed, set(json.loads(RULES.read_text())['classes']['luminous_fixture']))
        for deck, row in self.data['decks'].items():
            mapped = set(map(int, row['mapped_materials']))
            self.assertEqual(set(row['reviewed_fixture_ids']), mapped & reviewed, deck)
            self.assertEqual(set(row['unreviewed_material_ids']), mapped - reviewed, deck)
        self.assertEqual(sum(self.data['decks']['Research']['mapped_materials']['52'].values()), 399)
        self.assertEqual(self.data['decks']['Storage']['mapped_materials']['99']['ceiling'], 318)


if __name__ == '__main__':
    unittest.main()
