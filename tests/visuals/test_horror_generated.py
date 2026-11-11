"""Focused accepted-input checks for the QA generated-light asset compiler."""
import importlib.util
import json
from pathlib import Path
import struct
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('horror_generated', ROOT / 'scripts/compile-horror-generated.py')
compiler = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)


class GeneratedAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = ROOT / 'dist/audio2/owned-ARCHIVE.DAT'
        if not path.exists():
            raise unittest.SkipTest('Owned archive is required for accepted-profile compilation')
        cls.archive = path.read_bytes()
        cls.rules = json.loads((ROOT / 'profiles/horror-author1/rules.json').read_text())

    def test_deterministic_bounded_profiles_and_masks(self):
        for level in (0, 1, 2, 4, 5, 6, 7, 9):
            first, summary = compiler.encode_level(level, self.archive, self.rules)
            second, _ = compiler.encode_level(level, self.archive, self.rules)
            self.assertEqual(first, second)
            header = struct.unpack_from('<4s6I32s32s', first)
            self.assertEqual(header[:3], (b'HG1P', 1, level))
            self.assertEqual(zlib.crc32(first[92:]), header[6])
            self.assertEqual(header[3], summary['lights'])
            self.assertEqual(header[4], summary['surfaces'])
            self.assertLess(len(first), 300000)
            mask_bytes = first[-256*len(compiler.MASK_IDS):]
            for row, material in enumerate(compiler.MASK_IDS):
                actual = {i for i, bit in enumerate(mask_bytes[row*256:(row+1)*256]) if bit}
                active = material in self.rules['classes']['luminous_fixture'] or material in self.rules['deck_luminous_fixture'].get(str(level), [])
                expected = set(self.rules['reviewed_emissive_indices'][str(material)]) if active else set()
                self.assertEqual(actual, expected)

    def test_research_material_classification_and_signature(self):
        self.assertIn(52, self.rules['classes']['luminous_fixture'])
        self.assertIn(9, self.rules['classes']['non_emissive_bright'])
        self.assertEqual(self.rules['reviewed_emissive_indices']['52'], [176, 177, 178, 179])
        self.assertEqual(compiler.author.emission_policy(self.rules, 'luminous_fixture', 52, 'ceiling'),
                         'emits_normal')
        self.assertEqual(compiler.author.emission_policy(self.rules, 'luminous_fixture', 52, 'floor'),
                         'review_only')
        self.assertEqual(compiler.author.emission_policy(self.rules, 'non_emissive_bright', 9, 'ceiling'),
                         'review_only')
        research = json.loads((ROOT / 'dist/horror-author1d/generated/level-2.generated.json').read_text())
        self.assertEqual(research['rules_signature'], compiler.author.digest(compiler.author.encode(self.rules)))
        self.assertTrue(any(l['source_resource_id'] == 52 for l in research['lights']))
        self.assertFalse(any(l['source_resource_id'] == 9 for l in research['lights']))

    def test_storage_fixture_and_bright_exclusion(self):
        self.assertIn(99, self.rules['classes']['luminous_fixture'])
        self.assertIn(102, self.rules['classes']['non_emissive_bright'])
        self.assertEqual(self.rules['reviewed_emissive_indices']['99'], [208])
        self.assertEqual(compiler.author.emission_policy(self.rules, 'luminous_fixture', 99, 'ceiling'),
                         'emits_normal')
        storage = json.loads((ROOT / 'dist/horror-author1d/generated/level-4.generated.json').read_text())
        self.assertEqual(storage['rules_signature'], compiler.author.digest(compiler.author.encode(self.rules)))
        self.assertTrue(any(l['source_resource_id'] == 99 for l in storage['lights']))
        self.assertFalse(any(l['source_resource_id'] == 102 for l in storage['lights']))

    def test_reactor_and_deck_specific_spots(self):
        self.assertIn(26, self.rules['classes']['luminous_fixture'])
        self.assertIn(218, self.rules['classes']['non_emissive_bright'])
        self.assertEqual(self.rules['reviewed_emissive_indices']['26'], [176, 177, 178, 179, 180, 213])
        self.assertNotIn(188, self.rules['classes']['luminous_fixture'])
        self.assertEqual(self.rules['deck_luminous_fixture'], {'3': [188], '5': [188], '7': [155, 188]})
        self.assertEqual(self.rules['reviewed_emissive_indices']['155'], [176, 177, 178, 179, 180])
        for level, source in ((0, 26), (5, 188), (7, 155), (7, 188), (9, 185)):
            profile = json.loads((ROOT / f'dist/horror-author1d/generated/level-{level}.generated.json').read_text())
            self.assertTrue(any(light['source_resource_id'] == source for light in profile['lights']))
        medical = json.loads((ROOT / 'dist/horror-author1d/generated/level-1.generated.json').read_text())
        self.assertFalse(any(light['source_resource_id'] == 188 for light in medical['lights']))
        engineering = json.loads((ROOT / 'dist/horror-author1d/generated/level-7.generated.json').read_text())
        self.assertIsNone(next(t for t in engineering['diagnostics']['tiles'] if t['id'] == 2109)['textures']['wall'])
        self.assertFalse(any(light['source_resource_id'] in (9, 102) for light in engineering['lights']))

    def test_stale_rules_and_archive_rejected(self):
        changed = dict(self.rules)
        changed['version'] = 999
        with self.assertRaisesRegex(ValueError, 'signature'):
            compiler.encode_level(1, self.archive, changed)
        resources = compiler.author.ARCHIVE(self.archive, 4100, 4200)
        changed_resources = dict(resources)
        changed_map = bytearray(resources[4105])
        changed_map[1000] ^= 1
        changed_resources[4105] = bytes(changed_map)
        with patch.object(compiler.author, 'ARCHIVE', return_value=changed_resources):
            with self.assertRaisesRegex(ValueError, 'archive|baseline|Incompatible'):
                compiler.encode_level(1, self.archive, self.rules)


if __name__ == '__main__':
    unittest.main()
