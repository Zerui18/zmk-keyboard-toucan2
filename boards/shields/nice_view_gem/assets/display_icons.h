/*
 * Native one-bit icons. Each X/. cell is one LCD pixel.
 * Text is generated separately in generated_fonts.h.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "bitmap_font.h"

static const struct bitmap_glyph bolt_icon = {
    .width = 8,
    .height = 12,
    .pixels =
        ".....XXX"
        "...XXXXX"
        "...XXX.."
        ".XXXXX.."
        "XXXXXXXX"
        "XXXXXXXX"
        "....XXX."
        "....XX.."
        "..XXX..."
        ".XXX...."
        "XXX....."
        "XX......",
};

static const struct bitmap_glyph cross_icon = {
    .width = 10,
    .height = 10,
    .pixels =
        "XX......XX"
        "XXX....XXX"
        ".XXX..XXX."
        "..XX..XX.."
        "....XX...."
        "....XX...."
        "..XX..XX.."
        ".XXX..XXX."
        "XXX....XXX"
        "XX......XX",
};

static const struct bitmap_glyph apple_icon = {
    .width = 16,
    .height = 16,
    .pixels =
        "..........XX...."
        ".........XXX...."
        "........XXX....."
        "................"
        "....XXX..XXX...."
        "..XXXXXXXXXXXX.."
        ".XXXXXXXXXXXXXX."
        "XXXXXXXXXXXXX..."
        "XXXXXXXXXXXX...."
        "XXXXXXXXXXXX...."
        "XXXXXXXXXXXXX..."
        ".XXXXXXXXXXXXXX."
        ".XXXXXXXXXXXXXX."
        "..XXXXXXXXXXXX.."
        "...XXXXX.XXXX..."
        "....XXX...XX....",
};

static const struct bitmap_glyph windows_icon = {
    .width = 16,
    .height = 16,
    .pixels =
        "XXXXXXX..XXXXXXX"
        "XXXXXXX..XXXXXXX"
        "XXXXXXX..XXXXXXX"
        "XXXXXXX..XXXXXXX"
        "XXXXXXX..XXXXXXX"
        "XXXXXXX..XXXXXXX"
        "XXXXXXX..XXXXXXX"
        "................"
        "................"
        "XXXXXXX..XXXXXXX"
        "XXXXXXX..XXXXXXX"
        "XXXXXXX..XXXXXXX"
        "XXXXXXX..XXXXXXX"
        "XXXXXXX..XXXXXXX"
        "XXXXXXX..XXXXXXX"
        "XXXXXXX..XXXXXXX",
};

static const struct bitmap_glyph command_icon = {
    .width = 16,
    .height = 16,
    .pixels =
        ".XXXX......XXXX."
        "XXXXXX....XXXXXX"
        "XX..XX....XX..XX"
        "XX..XX....XX..XX"
        "XXXXXXXXXXXXXXXX"
        ".XXXXXXXXXXXXXX."
        "....XX....XX...."
        "....XX....XX...."
        "....XX....XX...."
        "....XX....XX...."
        ".XXXXXXXXXXXXXX."
        "XXXXXXXXXXXXXXXX"
        "XX..XX....XX..XX"
        "XX..XX....XX..XX"
        "XXXXXX....XXXXXX"
        ".XXXX......XXXX.",
};

static const struct bitmap_glyph shift_icon = {
    .width = 16,
    .height = 16,
    .pixels =
        ".......XX......."
        "......XXXX......"
        ".....XXXXXX....."
        "....XXX..XXX...."
        "...XXX....XXX..."
        "..XXX......XXX.."
        ".XXX........XXX."
        "XXX..........XXX"
        "XXXXXX....XXXXXX"
        "....XX....XX...."
        "....XX....XX...."
        "....XX....XX...."
        "....XX....XX...."
        "....XXXXXXXX...."
        "....XXXXXXXX...."
        "................",
};

static const struct bitmap_glyph control_icon = {
    .width = 16,
    .height = 16,
    .pixels =
        "................"
        "................"
        ".......XX......."
        "......XXXX......"
        ".....XXXXXX....."
        "....XXX..XXX...."
        "...XXX....XXX..."
        "..XXX......XXX.."
        ".XXX........XXX."
        "XXX..........XXX"
        "................"
        "................"
        "................"
        "................"
        "................"
        "................",
};

static const struct bitmap_glyph option_icon = {
    .width = 16,
    .height = 16,
    .pixels =
        "XXXXXX......XXXX"
        "XXXXXX......XXXX"
        "....XXX........."
        ".....XXX........"
        "......XXX......."
        ".......XXX......"
        "........XXX....."
        ".........XXX...."
        "..........XXXXXX"
        "...........XXXXX"
        "................"
        "................"
        "................"
        "................"
        "................"
        "................",
};
