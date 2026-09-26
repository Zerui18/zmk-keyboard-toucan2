#!/usr/bin/env python3
"""Pack the supplied Terminus bitmaps into native display tables, without rasterizing."""
# SPDX-License-Identifier: MIT

import argparse
from hashlib import sha256
from io import BytesIO
from pathlib import Path
import re
import tarfile
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "boards/shields/nice_view_gem/assets/generated_fonts.h"
LICENSE = ROOT / "boards/shields/nice_view_gem/assets/Terminus-OFL.txt"
VERSION = "4.49.1"
ARCHIVE_ROOT = f"terminus-font-{VERSION}"
ARCHIVE_URL = (
    "https://downloads.sourceforge.net/project/terminus-font/"
    f"terminus-font-4.49/{ARCHIVE_ROOT}.tar.gz"
)
ARCHIVE_SHA256 = "d961c1b781627bf417f9b340693d64fc219e0113ad3a3af1a3424c7aa373ef79"
ASCII = "".join(chr(code) for code in range(33, 127))
# Only these labels use the two large sizes. Keep all digits for L<n> fallbacks.
TITLE_LABELS = ("TOUCAN", "OFF")
LAYER_LABELS = ("BASE", "SYM", "NAV", "CLP", "EDT", "APP", "MOU", "SYS", "FN", "DNG", "UF2")
LABEL_CHARS = "".join(sorted(set("0123456789?L" + "".join(TITLE_LABELS + LAYER_LABELS))))
# Use upstream's actual bitmap sizes. Do not synthesize intermediate sizes.
FONTS = (("small", "ter-u12n.bdf", ASCII), ("status", "ter-u18b.bdf", ASCII),
         ("title", "ter-u24b.bdf", LABEL_CHARS), ("layer", "ter-u32b.bdf", LABEL_CHARS))


def read_bdf(source, characters):
    """Read selected BDF glyphs with their original baseline, bearings and advance."""
    source = source.replace("\r\n", "\n")
    wanted = {ord(code): code for code in characters}
    glyphs = {}
    for block in re.findall(r"^STARTCHAR [^\n]*\n(.*?)^ENDCHAR$", source, re.M | re.S):
        properties, marker, bitmap = block.partition("BITMAP\n")
        fields = {parts[0]: parts[1:] for line in properties.splitlines()
                  if (parts := line.split())}
        encoding = int(fields["ENCODING"][0])
        if encoding not in wanted:
            continue
        code = wanted[encoding]
        if code in glyphs:
            raise ValueError(f"Duplicate glyph: {code!r}")
        if not marker:
            raise ValueError(f"Missing BITMAP: {code!r}")
        advance, vertical_advance = map(int, fields["DWIDTH"])
        width, height, left, below = map(int, fields["BBX"])
        if advance <= 0 or vertical_advance != 0 or width <= 0 or height <= 0:
            raise ValueError(f"Invalid glyph metrics: {code!r}")
        hex_rows = bitmap.splitlines()
        row_bits = ((width + 7) // 8) * 8
        if len(hex_rows) != height:
            raise ValueError(f"Wrong row count: {code!r}")
        rows = []
        for hex_row in hex_rows:
            if len(hex_row) != row_bits // 4 or not re.fullmatch(r"[0-9A-Fa-f]+", hex_row):
                raise ValueError(f"Invalid bitmap row: {code!r}")
            value = int(hex_row, 16)
            if value & ((1 << (row_bits - width)) - 1):
                raise ValueError(f"Nonzero row padding: {code!r}")
            rows.append("".join("X" if value & (1 << (row_bits - 1 - x)) else "."
                                for x in range(width)))
        if not any("X" in row for row in rows):
            raise ValueError(f"Empty glyph: {code!r}")
        glyphs[code] = dict(rows=rows, left=left, top=-below-height, advance=advance)
    missing = set(characters) - glyphs.keys()
    if missing:
        raise ValueError(f"Missing glyphs: {sorted(missing)!r}")
    return {code: glyphs[code] for code in characters}


def pack_font(glyphs):
    """Remove only common empty top/bottom rows; preserve every ink coordinate."""
    advances = {glyph["advance"] for glyph in glyphs.values()}
    if len(advances) != 1:
        raise ValueError("Font must be monospaced")
    advance = advances.pop()
    bounds = {}
    for code, glyph in glyphs.items():
        ink_rows = [y + glyph["top"] for y, row in enumerate(glyph["rows"]) if "X" in row]
        bounds[code] = min(ink_rows), max(ink_rows) + 1
    if bounds["A"][1] != 0:
        raise ValueError("Expected capitals to sit on the BDF baseline")
    cap_height = -bounds["A"][0]
    top = min(start for start, _ in bounds.values())
    bottom = max(end for _, end in bounds.values())
    height = bottom - top
    if not (0 < advance <= 255 and 0 < height <= 255 and 0 < cap_height <= 255
            and -128 <= top + cap_height <= 127):
        raise ValueError("Font metrics do not fit the display descriptor")
    packed = {}
    for code, glyph in glyphs.items():
        rows = [["."] * advance for _ in range(height)]
        for y, row in enumerate(glyph["rows"]):
            for x, pixel in enumerate(row):
                if pixel == "X":
                    dest_x, dest_y = glyph["left"] + x, glyph["top"] + y - top
                    if not (0 <= dest_x < advance and 0 <= dest_y < height):
                        raise ValueError(f"Ink outside the font cell: {code!r}")
                    rows[dest_y][dest_x] = "X"
        packed[code] = ["".join(row) for row in rows]
    return packed, advance, height, cap_height, top + cap_height


def read_member(archive, filename):
    member = archive.getmember(f"{ARCHIVE_ROOT}/{filename}")
    if not member.isfile():
        raise ValueError(f"Expected a regular archive member: {filename}")
    # Never extract archive paths to the filesystem.
    return archive.extractfile(member).read().decode("ascii").replace("\r\n", "\n")


def generate(data):
    if sha256(data).hexdigest() != ARCHIVE_SHA256:
        raise ValueError(f"Archive checksum mismatch; expected Terminus Font {VERSION}")
    with tarfile.open(fileobj=BytesIO(data), mode="r:gz") as archive:
        if read_member(archive, "OFL.TXT").strip() != LICENSE.read_text().strip():
            raise ValueError("Bundled font license does not match the pinned source")
        fonts = [(name, filename, pack_font(read_bdf(read_member(archive, filename), characters)))
                 for name, filename, characters in FONTS]

    lines = [
        "/*",
        " * Generated by scripts/generate-display-font.py; do not edit pixel rows.",
        f" * Toucan Display: bitmap subset of Terminus Font {VERSION}.",
        " * Original ink, bearings and baselines are unchanged; no rasterization.",
        " * Copyright (C) 2020 Dimitar Toshkov Zhekov.",
        ' * The upstream Reserved Font Name is "Terminus Font".',
        " * SPDX-License-Identifier: OFL-1.1",
        " * License: Terminus-OFL.txt in this directory.",
        " * Source: https://terminus-font.sourceforge.net/",
        f" * Archive SHA256: {ARCHIVE_SHA256}",
        " */",
        "",
        "#pragma once",
        "",
        '#include "bitmap_font.h"',
        "",
    ]
    for name, filename, font in fonts:
        glyphs, advance, height, cap_height, y_offset = font
        lines += [f"/* {filename}; {cap_height}px capitals, {advance}px advance. */",
                  f"static const struct bitmap_glyph {name}_glyphs[] = {{"]
        for code, rows in glyphs.items():
            literal = "'" + code.replace("\\", "\\\\").replace("'", "\\'") + "'"
            pixels = " ".join(f'"{row}"' for row in rows)
            lines.append(f"    {{{literal}, {advance}, {height}, {pixels}}},")
        lines += [
            "};",
            "",
            f"static const struct bitmap_font {name}_font = {{",
            f"    .glyphs = {name}_glyphs,",
            f"    .glyph_count = sizeof({name}_glyphs) / sizeof({name}_glyphs[0]),",
            f"    .advance = {advance},",
            f"    .height = {height},",
            f"    .cap_height = {cap_height},",
            f"    .y_offset = {y_offset},",
            "};",
            "",
        ]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="Use the local pinned source tarball (offline)")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    parser.add_argument("--check", action="store_true", help="Fail if the output needs regeneration")
    args = parser.parse_args()
    try:
        if args.archive:
            data = args.archive.read_bytes()
        else:
            with urlopen(ARCHIVE_URL, timeout=30) as response:
                data = response.read()
        result = generate(data)
    except (OSError, ValueError, KeyError, tarfile.TarError) as error:
        parser.exit(1, f"{error}\n")
    if args.check:
        if not args.output.exists() or args.output.read_text() != result:
            parser.exit(1, f"Generated font is out of date: {args.output}\n")
        print(f"Generated font matches: {args.output}")
    else:
        args.output.write_text(result)
        print(args.output)


if __name__ == "__main__":
    main()
