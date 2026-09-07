#!/usr/bin/env python3
"""Render a specimen from the firmware's STATUS glyphs, without font dependencies."""

import argparse
import ast
from html import escape
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SCREEN_SOURCE = ROOT / "boards/shields/nice_view_gem/widgets/screen.c"


def read_glyphs(source=SCREEN_SOURCE):
    """Read the literal pixel rows; fail rather than silently invent missing pixels."""
    text = source.read_text()
    table = re.search(r"status_glyphs\[\] = \{(.*?)\n\};", text, re.S).group(1)
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


def specimen(glyphs):
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

    def draw(x, y, text, scale):
        for code in text:
            if code == " ":
                x += 3 * scale
                continue
            rows = glyphs[code]
            for row, pixels in enumerate(rows):
                for col, pixel in enumerate(pixels):
                    if pixel == "X":
                        parts.append(
                            f'<rect x="{x + col * scale}" y="{y + row * scale}" '
                            f'width="{scale}" height="{scale}" fill="#181818"/>'
                        )
            x += (len(rows[0]) + 1) * scale

    caption(24, 32, "Toucan STATUS — lowercase extension", 22)
    caption(24, 58, "Original capitals unchanged · 4 px x-height · 2 px descenders · one-pixel strokes")
    draw(24, 85, "Aa Bb Cc Dd Ee Ff Gg Hh Ii Jj Kk Ll Mm", 5)
    draw(24, 155, "Nn Oo Pp Qq Rr Ss Tt Uu Vv Ww Xx Yy Zz", 5)
    caption(24, 238, "Mixed-case specimen · enlarged 4×")
    draw(24, 258, "The quick brown fox jumps", 4)
    draw(24, 302, "over the lazy dog. AaZz iIlL 01", 4)
    caption(24, 370, "Punctuation · existing glyphs retained, missing ASCII completed")
    draw(24, 392, "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", 4)
    caption(24, 472, "Memory content scale · 2× source pixels, as on the LCD")
    draw(24, 492, "Hello World! It's AaZz.", 2)
    draw(24, 518, "g j p q y  a b c d e f", 2)
    caption(24, 572, "Generated from screen.c — font specimen, not a device screenshot")
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Output SVG path")
    args = parser.parse_args()
    args.output.write_text(specimen(read_glyphs()))
    print(args.output)


if __name__ == "__main__":
    main()
