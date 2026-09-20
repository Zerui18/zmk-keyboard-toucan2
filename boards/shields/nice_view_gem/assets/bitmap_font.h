/*
 * One-bit glyphs used by the Toucan canvas renderer.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

struct bitmap_glyph {
    char code;
    uint8_t width;
    uint8_t height;
    const char *pixels;
};

/*
 * Monospaced native pixels. Text positions refer to the top of a capital;
 * y_offset preserves the font's shared baseline and taller punctuation.
 */
struct bitmap_font {
    const struct bitmap_glyph *glyphs;
    size_t glyph_count;
    uint8_t advance;
    uint8_t height;
    uint8_t cap_height;
    int8_t y_offset;
};

/* Exact lookup: literal content must never silently change case. */
static inline const struct bitmap_glyph *find_glyph(const struct bitmap_glyph *glyphs,
                                                   size_t glyph_count, char code) {
    for (size_t i = 0; i < glyph_count; i++) {
        if (glyphs[i].code == code) {
            return &glyphs[i];
        }
    }
    for (size_t i = 0; i < glyph_count; i++) {
        if (glyphs[i].code == '?') {
            return &glyphs[i];
        }
    }
    return NULL;
}
