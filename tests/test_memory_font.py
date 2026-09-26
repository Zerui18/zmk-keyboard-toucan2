"""Validate native display artwork, including TEXT/SEQ coverage and geometry."""

from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
import runpy
import string
import tarfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
artwork = runpy.run_path(str(ROOT / "scripts/preview-memory-font.py"))
read_font = artwork["read_font"]
read_icon = artwork["read_icon"]
generator = runpy.run_path(str(ROOT / "scripts/generate-display-font.py"))
read_bdf = generator["read_bdf"]
pack_font = generator["pack_font"]


class MemoryFontTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.font = read_font()
        cls.glyphs = cls.font["glyphs"]

    def test_printable_ascii_coverage(self):
        # The renderer advances spaces without a bitmap.
        for name in ("small_font", "status_font"):
            self.assertEqual(set(read_font(name)["glyphs"]), {chr(code) for code in range(33, 127)})

    def test_cases_have_distinct_bitmaps(self):
        for lower in string.ascii_lowercase:
            with self.subTest(letter=lower):
                self.assertNotEqual(self.glyphs[lower], self.glyphs[lower.upper()])

    def test_similar_characters_remain_distinct(self):
        for group in ("0O", "1Il", ".,", "'`", "{}[]()", ":;"):
            bitmaps = {"".join(self.glyphs[code]) for code in group}
            self.assertEqual(len(bitmaps), len(group), group)

    def test_shared_baseline_and_descenders(self):
        def ink_bounds(code):
            ink = [row + self.font["y_offset"]
                   for row, pixels in enumerate(self.glyphs[code]) if "X" in pixels]
            return min(ink), max(ink) + 1

        self.assertEqual(ink_bounds("H"), (0, self.font["cap_height"]))
        self.assertEqual(ink_bounds("x"), (3, self.font["cap_height"]))
        for code in "gjpqy":
            self.assertGreater(ink_bounds(code)[1], self.font["cap_height"], code)
        # Do not crop taller punctuation to capital height.
        self.assertLess(ink_bounds("$")[0], 0)

    def test_glyphs_fit_content_line(self):
        for code, rows in self.glyphs.items():
            with self.subTest(letter=code):
                self.assertLessEqual(len(rows), 18)
                self.assertLessEqual(self.font["y_offset"] + len(rows), 16)
                self.assertLessEqual(len(rows[0]), 10)
                self.assertIn("X", "".join(rows))

    def test_native_detail_is_not_pixel_doubling(self):
        for code in "ACXag":
            rows = self.glyphs[code]
            with self.subTest(letter=code):
                self.assertTrue(any(
                    len({rows[y + dy][x + dx] for dy in range(2) for dx in range(2)}) > 1
                    for y in range(0, len(rows), 2)
                    for x in range(0, len(rows[0]), 2)
                ))

    def test_font_sizes_and_monospacing(self):
        for name, height, cap_height, advance, offset in (
            ("small_font", 12, 8, 6, -2), ("status_font", 18, 12, 10, -3),
            ("title_font", 15, 15, 12, 0), ("layer_font", 20, 20, 16, 0),
        ):
            with self.subTest(font=name):
                font = read_font(name)
                self.assertEqual(font["height"], height)
                self.assertEqual(font["cap_height"], cap_height)
                self.assertEqual(font["advance"], advance)
                self.assertEqual(font["y_offset"], offset)
                self.assertIn("?", font["glyphs"])
                for code, rows in font["glyphs"].items():
                    self.assertEqual(len(rows), height, code)
                    self.assertTrue(all(len(row) == advance for row in rows), code)
                    self.assertIn("X", "".join(rows), code)

    def test_upstream_pixels_and_advances_unchanged(self):
        # Independently decoded from the pinned 4.49.1 BDFs, not from the packer.
        # Hash advance + row-major ink coordinates relative to the BDF baseline.
        # This also detects shifted bearings, padding mistakes and retouched pixels.
        for name, fingerprint in (
            ("small_font", "4a43a5d5542caff253a40ea7ec96027146302806a27aa237ac2d37e9a4eeb773"),
            ("status_font", "25edda7d60e794aa3e605b31bed0d4973f2616d059c5be567b5bac3a54ae891a"),
            ("title_font", "223b4e34e32d2c34a52366d2ec54841245edab363ad933bb5e770d8e837166bb"),
            ("layer_font", "cdeefef66400b8cea58a4645ddacf5bffe540122cbf1ed10d486da0c9fa97fb5"),
        ):
            with self.subTest(font=name):
                font = read_font(name)
                ink = {
                    code: [font["advance"],
                           [[x, y + font["y_offset"] - font["cap_height"]]
                            for y, row in enumerate(rows)
                            for x, pixel in enumerate(row) if pixel == "X"]]
                    for code, rows in font["glyphs"].items()
                }
                data = json.dumps(ink, sort_keys=True, separators=(",", ":")).encode()
                self.assertEqual(sha256(data).hexdigest(), fingerprint)

    def test_titles_and_layer_names_fit(self):
        for name, labels in (
            ("title_font", ("TOUCAN", "OFF")),
            ("layer_font", ("BASE", "SYM", "NAV", "CLP", "EDT", "APP",
                            "MOU", "SYS", "FN", "DNG", "UF2", "L255")),
        ):
            font = read_font(name)
            for label in labels:
                with self.subTest(font=name, label=label):
                    self.assertTrue(set(label) <= set(font["glyphs"]))
                    width = len(label) * font["advance"]
                    self.assertLessEqual(width, 128)

    def test_dashboard_dirty_band_geometry(self):
        font = self.font
        # Check actual ink, not blank cell padding above the capitals.
        for label, y, band_top, band_bottom in (
            ("USB! BT5? OFF", 8, 7, 23),
            ("BT12345", 84, 82, 98),
            ("ME123", 116, 114, 130),
            ("CAPS", 148, 146, 162),
        ):
            for code in label.replace(" ", ""):
                for row, pixels in enumerate(font["glyphs"][code]):
                    if "X" in pixels:
                        self.assertGreaterEqual(y + font["y_offset"] + row, band_top)
                        self.assertLess(y + font["y_offset"] + row, band_bottom)

    def test_memory_lines_and_footer_do_not_overlap(self):
        font = self.font
        pitch = font["height"] + 1
        self.assertGreaterEqual(36 + font["y_offset"], 28)  # below the header divider
        self.assertLessEqual(36 + 5 * pitch + font["y_offset"] + font["height"], 148)
        small = read_font("small_font")
        self.assertGreaterEqual(154 + small["y_offset"], 148)
        self.assertLessEqual(154 + small["y_offset"] + small["height"], 168)

    def test_icon_native_dimensions(self):
        dimensions = {"bolt_icon": (8, 12), "cross_icon": (10, 10)}
        dimensions.update({name: (16, 16) for name in (
            "apple_icon", "windows_icon", "command_icon", "shift_icon",
            "control_icon", "option_icon",
        )})
        for name, (width, height) in dimensions.items():
            with self.subTest(icon=name):
                rows = read_icon(name)
                self.assertEqual((len(rows[0]), len(rows)), (width, height))
                self.assertIn("X", "".join(rows))


class BitmapImportTest(unittest.TestCase):
    MULTIBYTE = ("STARTCHAR A\nENCODING 65\nDWIDTH 10 0\n"
                 "BBX 10 1 0 0\nBITMAP\n8040\nENDCHAR\n")
    BEARINGS = ("STARTCHAR A\nENCODING 65\nDWIDTH 6 0\n"
                "BBX 3 5 1 0\nBITMAP\n00\n40\nA0\nE0\nA0\nENDCHAR\n"
                "STARTCHAR g\nENCODING 103\nDWIDTH 6 0\n"
                "BBX 3 5 2 -2\nBITMAP\n60\nA0\n60\n20\nC0\nENDCHAR\n")

    def test_multibyte_rows_are_msb_first_with_byte_padding(self):
        for source in (self.MULTIBYTE, self.MULTIBYTE.replace("\n", "\r\n")):
            glyph = read_bdf(source, "A")["A"]
            self.assertEqual(glyph, dict(rows=["X........X"], left=0, top=-1, advance=10))

    def test_packing_preserves_bearings_and_descenders(self):
        rows, advance, height, caps, offset = pack_font(read_bdf(self.BEARINGS, "Ag"))
        self.assertEqual((advance, height, caps, offset), (6, 6, 4, 0))
        self.assertEqual(rows["A"], ["..X...", ".X.X..", ".XXX..", ".X.X..", "......", "......"])
        self.assertEqual(rows["g"], ["......", "...XX.", "..X.X.", "...XX.", "....X.", "..XX.."])

    def test_invalid_bdf_is_rejected_instead_of_clipped(self):
        for source in (
            self.MULTIBYTE.replace("8040", "8041"),  # nonzero padding
            self.MULTIBYTE.replace("8040", "80"),  # truncated row
            self.MULTIBYTE.replace("8040", "GGGG"),
            self.MULTIBYTE.replace("8040", "8040\n0000"),  # extra row
            self.MULTIBYTE.replace("8040", "0000"),  # no ink
            self.MULTIBYTE.replace("BITMAP\n", ""),
            self.MULTIBYTE.replace("DWIDTH 10 0", "DWIDTH 10 1"),
            self.MULTIBYTE + self.MULTIBYTE,  # duplicate encoding
            "",
        ):
            with self.subTest(source=source), self.assertRaises(ValueError):
                read_bdf(source, "A")
        for source in (
            self.BEARINGS.replace("DWIDTH 6 0", "DWIDTH 7 0", 1),
            self.BEARINGS.replace("BBX 3 5 2 -2", "BBX 3 5 5 -2"),
            self.BEARINGS.replace("BBX 3 5 1 0", "BBX 3 5 1 1"),
        ):
            with self.subTest(source=source), self.assertRaises(ValueError):
                pack_font(read_bdf(source, "Ag"))

    def test_archive_members_are_read_without_extraction(self):
        data = BytesIO()
        with tarfile.open(fileobj=data, mode="w") as archive:
            member = tarfile.TarInfo(f'{generator["ARCHIVE_ROOT"]}/test.bdf')
            member.size = 3
            archive.addfile(member, BytesIO(b"x\r\n"))
            link = tarfile.TarInfo(f'{generator["ARCHIVE_ROOT"]}/link.bdf')
            link.type = tarfile.SYMTYPE
            link.linkname = "test.bdf"
            archive.addfile(link)
        data.seek(0)
        with tarfile.open(fileobj=data) as archive:
            self.assertEqual(generator["read_member"](archive, "test.bdf"), "x\n")
            with self.assertRaisesRegex(ValueError, "regular archive member"):
                generator["read_member"](archive, "link.bdf")

    def test_wrong_archive_is_rejected_before_parsing(self):
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            generator["generate"](b"not the pinned font")


if __name__ == "__main__":
    unittest.main()
