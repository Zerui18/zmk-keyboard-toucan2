"""Validate the actual pixel definitions used by both TEXT and SEQ previews."""

from pathlib import Path
import runpy
import string
import unittest

ROOT = Path(__file__).resolve().parents[1]
read_glyphs = runpy.run_path(str(ROOT / "scripts/preview-memory-font.py"))["read_glyphs"]


class MemoryFontTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.glyphs = read_glyphs()

    def test_printable_ascii_coverage(self):
        # The renderer advances spaces without a bitmap.
        self.assertEqual(set(self.glyphs), {chr(code) for code in range(33, 127)})

    def test_cases_have_distinct_bitmaps(self):
        for lower in string.ascii_lowercase:
            with self.subTest(letter=lower):
                self.assertNotEqual(self.glyphs[lower], self.glyphs[lower.upper()])

    def test_original_capital_metrics(self):
        for upper in string.ascii_uppercase:
            with self.subTest(letter=upper):
                self.assertEqual(len(self.glyphs[upper]), 6)
                self.assertEqual(len(self.glyphs[upper][0]), 3 if upper == "I" else 5)

    def test_x_height_and_descenders(self):
        for code in "acegmnopqrsuvwxyz":
            with self.subTest(letter=code):
                self.assertNotIn("X", "".join(self.glyphs[code][:2]))
                self.assertIn("X", self.glyphs[code][2])
        for code in "gjpqy":
            with self.subTest(letter=code):
                self.assertEqual(len(self.glyphs[code]), 8)
                self.assertIn("X", "".join(self.glyphs[code][6:]))
        self.assertNotIn("X", self.glyphs["i"][1])
        self.assertNotIn("X", self.glyphs["j"][1])

    def test_glyphs_fit_content_line(self):
        for code, rows in self.glyphs.items():
            with self.subTest(letter=code):
                self.assertLessEqual(len(rows), 8)
                self.assertLessEqual(len(rows[0]), 5)
                self.assertIn("X", "".join(rows))


if __name__ == "__main__":
    unittest.main()
