#!/usr/bin/env python3
"""Render a specimen from the firmware's STATUS glyphs, without font dependencies."""

import argparse
import ast
from html import escape
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
GLYPH_SOURCE = ROOT / "boards/shields/nice_view_gem/assets/generated_fonts.h"
ICON_SOURCE = ROOT / "boards/shields/nice_view_gem/assets/display_icons.h"


def read_glyphs(source=GLYPH_SOURCE, table_name="status_glyphs"):
    """Read the literal pixel rows; fail rather than silently invent missing pixels."""
    text = source.read_text()
    match = re.search(rf"{re.escape(table_name)}\[\] = \{{(.*?)\n\}};", text, re.S)
    assert match, f"Missing glyph table: {table_name}"
    table = match.group(1)
    pattern = r"""\{\s*('(?:\\.|[^'\\])*'),\s*(\d+),\s*(\d+),\s*((?:"[.X]+"\s*)+)\}"""
    glyphs = {}
    for literal, width, height, pixels in re.findall(pattern, table):
        code = ast.literal_eval(literal)
        rows = re.findall(r'"([.X]+)"', pixels)
        assert code not in glyphs, f"Duplicate glyph: {code!r}"
        assert len(rows) == int(height), f"Wrong height: {code!r}"
        assert all(len(row) == int(width) for row in rows), f"Wrong width: {code!r}"
        glyphs[code] = rows
    assert len(glyphs) == len(re.findall(r"^\s*\{'", table, re.M)), "Unrecognized glyph declaration"
    return glyphs


def read_font(name="status_font", source=GLYPH_SOURCE):
    text = source.read_text()
    match = re.search(rf"bitmap_font {re.escape(name)} = \{{(.*?)\n\}};", text, re.S)
    assert match, f"Missing font: {name}"
    body = match.group(1)
    table = re.search(r"\.glyphs = (\w+)", body).group(1)
    return {
        "glyphs": read_glyphs(source, table),
        **{field: int(re.search(rf"\.{field} = (-?\d+)", body).group(1))
           for field in ("advance", "height", "cap_height", "y_offset")},
    }


def read_icon(name, source=ICON_SOURCE):
    text = source.read_text()
    match = re.search(rf"bitmap_glyph {re.escape(name)} = \{{(.*?)\n\}};", text, re.S)
    assert match, f"Missing icon: {name}"
    body = match.group(1)
    width = int(re.search(r"\.width = (\d+)", body).group(1))
    height = int(re.search(r"\.height = (\d+)", body).group(1))
    rows = re.findall(r'"([.X]+)"', body)
    assert len(rows) == height, f"Wrong height: {name}"
    assert all(len(row) == width for row in rows), f"Wrong width: {name}"
    return rows


def specimen(font):
    parts = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="960" height="590" '
        'viewBox="0 0 960 590">',
        '<rect width="960" height="590" fill="#faf9f6"/>',
    ]

    def caption(x, y, text, size=14):
        parts.append(
            f'<text x="{x}" y="{y}" fill="#555" font-family="sans-serif" '
            f'font-size="{size}">{escape(text)}</text>'
        )

    def draw(x, y, text, scale, font=font):
        glyphs = font["glyphs"]
        y += font["y_offset"] * scale
        for code in text:
            if code == " ":
                x += font["advance"] * scale
                continue
            rows = glyphs[code]
            for row, pixels in enumerate(rows):
                for col, pixel in enumerate(pixels):
                    if pixel == "X":
                        parts.append(
                            f'<rect x="{x + col * scale}" y="{y + row * scale}" '
                            f'width="{scale}" height="{scale}" fill="#181818"/>'
                        )
            x += font["advance"] * scale

    caption(24, 32, "Toucan Display — native Terminus bitmaps", 22)
    caption(24, 58, "Supplied bitmap sizes · original pixels preserved · no rasterization or scaling")
    draw(24, 85, "Aa Bb Cc Dd Ee Ff Gg Hh Ii Jj Kk Ll Mm", 2)
    draw(24, 155, "Nn Oo Pp Qq Rr Ss Tt Uu Vv Ww Xx Yy Zz", 2)
    caption(24, 238, "Mixed-case specimen · enlarged 2× for inspection")
    draw(24, 258, "The quick brown fox jumps", 2)
    draw(24, 302, "over the lazy dog. AaZz iIlL 01", 2)
    caption(24, 370, "Punctuation · native corners and diagonals")
    draw(24, 392, "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", 2)
    caption(24, 472, "Memory content · 1× source pixels, as on the LCD")
    draw(24, 492, "Hello World! It's AaZz.", 1)
    draw(24, 518, "g j p q y  a b c d e f", 1)
    draw(430, 492, "TOUCAN", 1, read_font("title_font"))
    draw(580, 490, "BASE NAV", 2, read_font("layer_font"))
    caption(24, 572, "Generated from firmware pixel rows — specimen, not a device screenshot")
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Output SVG path")
    args = parser.parse_args()
    args.output.write_text(specimen(read_font()))
    print(args.output)


if __name__ == "__main__":
    main()
