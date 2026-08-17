#!/usr/bin/env python3
"""Reflow Toucan keymap bindings to match the physical split layout.

Each keymap layer is expected to contain exactly 42 bindings, ordered as:

     0  1  2  3  4  5       6  7  8  9 10 11
    12 13 14 15 16 17      18 19 20 21 22 23
    24 25 26 27 28 29      30 31 32 33 34 35
             36 37 38      39 40 41

Bindings are parsed as complete ZMK behavior expressions (for example,
``&bt BT_SEL 0``), so parameter counts and existing line wrapping do not
affect the result.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import stat
import sys
import tempfile


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_KEYMAP = REPO_ROOT / "config" / "toucan.keymap"

EXPECTED_BINDINGS = 42
SPLIT_COLUMN = 6
SPLIT_GAP = "    "
ROW_INDENT = "    "

# Entries are binding indexes; None represents empty space above/beside the
# thumb clusters. Keeping every row on the same twelve-column grid makes the
# starts of bindings line up with their physical columns.
PHYSICAL_ROWS: tuple[tuple[int | None, ...], ...] = (
    tuple(range(0, 12)),
    tuple(range(12, 24)),
    tuple(range(24, 36)),
    (None, None, None, 36, 37, 38, 39, 40, 41, None, None, None),
)


class FormatError(ValueError):
    """Raised when safely reflowing a keymap would be ambiguous."""


def _masked_source(source: str) -> str:
    """Blank comments and strings while retaining offsets and newlines."""

    result = list(source)
    index = 0
    state = "code"
    quote = ""

    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""

        if state == "code":
            if char == "/" and following == "/":
                result[index] = result[index + 1] = " "
                index += 2
                state = "line_comment"
                continue
            if char == "/" and following == "*":
                result[index] = result[index + 1] = " "
                index += 2
                state = "block_comment"
                continue
            if char in {'"', "'"}:
                quote = char
                result[index] = " "
                index += 1
                state = "string"
                continue
            index += 1
            continue

        if state == "line_comment":
            if char in "\r\n":
                state = "code"
            else:
                result[index] = " "
            index += 1
            continue

        if state == "block_comment":
            if char == "*" and following == "/":
                result[index] = result[index + 1] = " "
                index += 2
                state = "code"
            else:
                if char not in "\r\n":
                    result[index] = " "
                index += 1
            continue

        # Quoted string or character literal.
        result[index] = " " if char not in "\r\n" else char
        if char == "\\" and following:
            if following not in "\r\n":
                result[index + 1] = " "
            index += 2
        elif char == quote:
            index += 1
            state = "code"
        else:
            index += 1

    if state == "block_comment":
        raise FormatError("unterminated block comment")
    if state == "string":
        raise FormatError("unterminated quoted string")

    return "".join(result)


def _contains_comment(source: str) -> bool:
    """Return whether source has a real comment outside a quoted string."""

    quote = ""
    index = 0
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""

        if quote:
            if char == "\\" and following:
                index += 2
                continue
            if char == quote:
                quote = ""
            index += 1
            continue

        if char in {'"', "'"}:
            quote = char
        elif char == "/" and following in {"/", "*"}:
            return True
        index += 1

    return False


def _collapse_whitespace(expression: str) -> str:
    """Collapse whitespace outside quotes without changing quoted values."""

    output: list[str] = []
    pending_space = False
    quote = ""
    index = 0

    while index < len(expression):
        char = expression[index]

        if quote:
            output.append(char)
            if char == "\\" and index + 1 < len(expression):
                output.append(expression[index + 1])
                index += 2
                continue
            if char == quote:
                quote = ""
            index += 1
            continue

        if char in {'"', "'"}:
            if pending_space and output:
                output.append(" ")
            pending_space = False
            quote = char
            output.append(char)
        elif char.isspace():
            pending_space = True
        else:
            if pending_space and output:
                output.append(" ")
            pending_space = False
            output.append(char)
        index += 1

    return "".join(output).strip()


def _parse_bindings(contents: str, line: int) -> list[str]:
    if _contains_comment(contents):
        raise FormatError(
            f"line {line}: comments inside a bindings array are ambiguous; "
            "move them immediately above the bindings property"
        )

    if re.search(r"(?m)^\s*#", contents):
        raise FormatError(
            f"line {line}: preprocessor directives inside a bindings array "
            "cannot be safely reflowed"
        )

    masked = _masked_source(contents)
    starts = [
        match.start()
        for match in re.finditer(r"(?:^|(?<=[\s,]))&[A-Za-z_][A-Za-z0-9_]*", masked)
    ]

    if not starts:
        raise FormatError(f"line {line}: bindings array contains no ZMK behaviors")
    if contents[: starts[0]].strip():
        raise FormatError(f"line {line}: unexpected text before the first binding")

    bindings = [
        _collapse_whitespace(contents[start:end])
        for start, end in zip(starts, starts[1:] + [len(contents)])
    ]

    if len(bindings) != EXPECTED_BINDINGS:
        raise FormatError(
            f"line {line}: found {len(bindings)} bindings; "
            f"the Toucan physical layout requires {EXPECTED_BINDINGS}"
        )

    return bindings


def _render_bindings(bindings: list[str], property_indent: str, newline: str) -> str:
    widths = [0] * 12
    for row in PHYSICAL_ROWS:
        for column, binding_index in enumerate(row):
            if binding_index is not None:
                widths[column] = max(widths[column], len(bindings[binding_index]))

    rendered_rows: list[str] = []
    for row in PHYSICAL_ROWS:
        final_column = max(index for index, value in enumerate(row) if value is not None)
        cells: list[str] = []

        for column in range(final_column + 1):
            binding_index = row[column]
            value = "" if binding_index is None else bindings[binding_index]
            cells.append(value.ljust(widths[column]))

            if column < final_column:
                cells.append(SPLIT_GAP if column + 1 == SPLIT_COLUMN else " ")

        rendered_rows.append(property_indent + ROW_INDENT + "".join(cells).rstrip())

    return newline + newline.join(rendered_rows) + newline + property_indent


def format_keymap(source: str) -> str:
    """Return source with every Toucan keymap bindings array reflowed."""

    masked = _masked_source(source)
    newline = "\r\n" if "\r\n" in source else "\n"
    property_pattern = re.compile(r"\bbindings\s*=\s*<")
    closing_pattern = re.compile(r">\s*;")
    replacements: list[tuple[int, int, str]] = []
    cursor = 0

    while match := property_pattern.search(masked, cursor):
        opening = match.end() - 1
        closing = closing_pattern.search(masked, opening + 1)
        if closing is None:
            line = source.count("\n", 0, match.start()) + 1
            raise FormatError(f"line {line}: bindings array has no closing >;")

        line_start = source.rfind("\n", 0, match.start()) + 1
        indentation_match = re.match(r"[ \t]*", source[line_start:match.start()])
        property_indent = indentation_match.group(0) if indentation_match else ""
        line = source.count("\n", 0, match.start()) + 1
        bindings = _parse_bindings(source[opening + 1 : closing.start()], line)
        replacement = _render_bindings(bindings, property_indent, newline)
        replacements.append((opening + 1, closing.start(), replacement))
        cursor = closing.end()

    if not replacements:
        raise FormatError("no bindings arrays found")

    formatted = source
    for start, end, replacement in reversed(replacements):
        formatted = formatted[:start] + replacement + formatted[end:]
    return formatted


def _atomic_write(path: Path, contents: str) -> None:
    mode = stat.S_IMODE(path.stat().st_mode)
    temporary_name = ""
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="",
            dir=path.parent,
            prefix=f".{path.name}.",
            delete=False,
        ) as temporary:
            temporary.write(contents)
            temporary_name = temporary.name
        os.chmod(temporary_name, mode)
        os.replace(temporary_name, path)
    finally:
        if temporary_name and os.path.exists(temporary_name):
            os.unlink(temporary_name)


def _arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        metavar="KEYMAP",
        nargs="*",
        type=Path,
        help=f"keymap files to format (default: {DEFAULT_KEYMAP.relative_to(REPO_ROOT)})",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="do not write; fail if any keymap needs formatting",
    )
    return parser.parse_args()


def main() -> int:
    args = _arguments()
    paths = args.paths or [DEFAULT_KEYMAP]
    results: list[tuple[Path, str, bool]] = []

    try:
        for path in paths:
            source = path.read_text(encoding="utf-8")
            formatted = format_keymap(source)
            results.append((path, formatted, formatted != source))
    except (OSError, UnicodeError, FormatError) as error:
        print(f"format-keymap: {error}", file=sys.stderr)
        return 2

    changed = [path for path, _, needs_change in results if needs_change]
    if args.check:
        for path in changed:
            print(f"needs formatting: {path}")
        return 1 if changed else 0

    for path, formatted, needs_change in results:
        if needs_change:
            _atomic_write(path, formatted)
            print(f"formatted: {path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
