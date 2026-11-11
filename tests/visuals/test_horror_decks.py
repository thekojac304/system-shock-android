"""Check the authored Auto rows and preserve the accepted anchor parameters."""
from pathlib import Path
import re
import unittest


SOURCE = (Path(__file__).resolve().parents[2] /
          "SourceSnapshot/src/MacSrc/GpuWorld.c").read_text()
TABLE = SOURCE.split("static const GpuPostPreset post_presets[] = {", 1)[1].split(
    "_Static_assert", 1)[0]
ROWS = [(name, [float(value.removesuffix("f")) for value in re.findall(
    r"(?<![A-Za-z_])[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:f)?", body)])
    for name, body in re.findall(r'\{"([^"\n]+)"(.*?)(?=\n\s*\{"|\n\};)',
                               TABLE, re.S)]


class DeckPresets(unittest.TestCase):
    def test_every_ordinary_deck_has_one_row(self):
        self.assertEqual(len(ROWS), 16)
        self.assertEqual([name for name, _ in ROWS[7:]], [
            "REACTOR INDUSTRIAL", "RESEARCH CLINICAL", "MAINTENANCE UTILITY",
            "STORAGE WAREHOUSE", "FLIGHT METALLIC", "ENGINEERING MACHINERY",
            "SECURITY SEVERE", "BRIDGE ENDGAME", "GROVE BOTANICAL"])
        self.assertTrue(all(len(values) == 42 for _, values in ROWS[7:]))

    def test_anchors_are_unchanged(self):
        medical = ROWS[1][1]
        executive = ROWS[4][1]
        self.assertEqual(medical[:17], [1.02, 1.13, .035, .12, .94,
            .78, .90, 1.05, 1.02, 1.02, 1, .10, .025, .10, .72, .25, .007])
        self.assertEqual(medical[30:39], [0, 0, 0, 1, 1, 0, 0, 0, 0])
        self.assertEqual(executive[:17], [1.02, 1.17, .04, .17, .94,
            .85, .91, 1.02, 1.07, 1.01, .89, .10, .08, .11, .74, .25, .006])
        self.assertEqual(executive[30:39], [.55, .030, .08, .88, 1.05,
            1.55, .9, .045, .04])

    def test_new_rows_are_restrained_and_finite(self):
        limits = {0: (.95, 1.07), 1: (1, 1.25), 2: (0, .08),
                  3: (0, .25), 4: (.8, 1.05), 11: (0, .2),
                  12: (0, .2), 13: (0, .2), 16: (0, .02),
                  22: (0, .1), 23: (0, .1), 24: (0, .05),
                  29: (0, .01), 30: (0, 1), 31: (0, .05),
                  32: (0, .1), 33: (.8, 1.2), 34: (.8, 1.2),
                  37: (0, .1), 38: (0, .1)}
        for name, values in ROWS[7:]:
            with self.subTest(name=name):
                for index, (low, high) in limits.items():
                    self.assertLessEqual(low, values[index])
                    self.assertLessEqual(values[index], high)
                self.assertLess(values[20], values[21])
                self.assertEqual(values[33:35], [1, 1])
                self.assertLessEqual(values[38], values[37])


if __name__ == "__main__":
    unittest.main()
