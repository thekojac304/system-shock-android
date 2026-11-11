"""Coverage contract for the accepted, packaged generated-light profiles."""
import json
from pathlib import Path
import re
import struct
import unittest
import zipfile
import zlib


ROOT = Path(__file__).resolve().parents[2]
QA_ASSETS = ROOT / "AndroidProject/app/src/qa/assets/horror-generated"
QA_APK = ROOT / "AndroidProject/app/build/outputs/apk/qa/app-qa.apk"
HEADER = struct.Struct("<4s6I32s32s")
ACCEPTED = (0, 1, 2, 4, 5, 6, 7, 9)


class DecklightCoverage(unittest.TestCase):
    def test_rules_exclude_security(self):
        rules = json.loads((ROOT / "profiles/horror-author1/rules.json").read_text())
        self.assertEqual({int(level) for level in rules["archetypes"]}, {0, 1, 2, 3, 4, 5, 6, 7, 9})
        self.assertNotIn('8', rules['deck_luminous_fixture'])

    def test_runtime_assets_are_bounded_signed_and_complete(self):
        if not QA_ASSETS.is_dir():
            self.skipTest("QA assets require the user's owned archive")
        assets = sorted(QA_ASSETS.glob("level-*.bin"))
        self.assertEqual([p.name for p in assets], [f"level-{level}.bin" for level in ACCEPTED])
        for asset in assets:
            blob = asset.read_bytes()
            magic, version, level, lights, surfaces, regions, crc, baseline, rules = HEADER.unpack_from(blob)
            self.assertEqual((magic, version, level), (b"HG1P", 1, int(asset.stem.split("-")[1])))
            self.assertGreater(lights, 0)
            self.assertGreater(surfaces, 0)
            self.assertLessEqual(lights, 512)
            self.assertLessEqual(surfaces, 4096)
            self.assertLessEqual(regions, 1024)
            self.assertEqual(zlib.crc32(blob[HEADER.size:]), crc)
            self.assertEqual(len(baseline), 32)
            self.assertEqual(len(rules), 32)

    def test_runtime_registry_matches_accepted_assets(self):
        if not QA_ASSETS.is_dir():
            self.skipTest("QA assets require the user's owned archive")
        loader = (ROOT / "SourceSnapshot/src/MacSrc/GpuGenerated.c").read_text()
        world = (ROOT / "SourceSnapshot/src/MacSrc/GpuWorld.c").read_text()
        registry = loader.split("accepted_profiles[]={", 1)[1].split("};", 1)[0]
        accepted = {int(level): bytes.fromhex(signature) for level, signature in
                    re.findall(r'\{(\d+),"([0-9a-f]{64})"\}', registry)}
        assets = sorted(QA_ASSETS.glob("level-*.bin"))
        self.assertEqual(set(accepted), {int(p.stem.split("-")[1]) for p in assets})
        self.assertEqual(set(accepted), set(ACCEPTED))
        for asset in assets:
            _, _, level, _, _, _, _, baseline, _ = HEADER.unpack_from(asset.read_bytes())
            self.assertEqual(baseline, accepted[level])
        self.assertIn("gg_profile_supported(level)", world)

    def test_qa_package_contains_only_compact_accepted_profiles(self):
        if not QA_APK.is_file():
            self.skipTest("QA APK has not been built")
        with zipfile.ZipFile(QA_APK) as apk:
            names = sorted(n for n in apk.namelist() if "horror-generated/" in n)
            self.assertEqual(names, [f"assets/horror-generated/level-{level}.bin" for level in ACCEPTED])
            for name in names:
                self.assertEqual(apk.read(name), (QA_ASSETS / Path(name).name).read_bytes())


if __name__ == "__main__":
    unittest.main()
