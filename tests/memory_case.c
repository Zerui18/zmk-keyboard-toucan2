/*
 * Host tests for the exact character decoder linked into capture and preview.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/modifiers.h>

#include "bitmap_font.h"
#include "toucan_key_text.h"

static void test_case_sensitive_glyph_lookup(void) {
    const struct bitmap_glyph glyphs[] = {
        {'A', 1, 1, "X"},
        {'a', 1, 2, "." "X"},
        {'B', 1, 1, "X"},
        {'?', 1, 1, "X"},
    };
    const size_t count = sizeof(glyphs) / sizeof(glyphs[0]);
    assert(find_glyph(glyphs, count, 'A') == &glyphs[0]);
    assert(find_glyph(glyphs, count, 'a') == &glyphs[1]);
    /* Unsupported lowercase must use the fallback, not its uppercase partner. */
    assert(find_glyph(glyphs, count, 'b') == &glyphs[3]);
    assert(find_glyph(glyphs, count, '~') == &glyphs[3]);
    assert(find_glyph(glyphs, count - 1, '~') == NULL);
}

static void test_alphabet(void) {
    const uint8_t shifts[] = {0, MOD_LSFT, MOD_RSFT, MOD_LSFT | MOD_RSFT};
    for (int caps = 0; caps < 2; caps++) {
        for (size_t s = 0; s < sizeof(shifts); s++) {
            for (int i = 0; i < 26; i++) {
                char character = '\0';
                assert(toucan_keycode_to_character(HID_USAGE_KEY_KEYBOARD_A + i,
                                                   shifts[s] | MOD_LCTL | MOD_LGUI,
                                                   caps, &character));
                char expected = ((shifts[s] != 0) != caps ? 'A' : 'a') + i;
                assert(character == expected);
            }
        }
    }
}

static void test_punctuation(void) {
    const struct {
        uint16_t keycode;
        char plain;
        char shifted;
    } cases[] = {
        {HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION, '1', '!'},
        {HID_USAGE_KEY_KEYBOARD_2_AND_AT, '2', '@'},
        {HID_USAGE_KEY_KEYBOARD_3_AND_HASH, '3', '#'},
        {HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, '4', '$'},
        {HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT, '5', '%'},
        {HID_USAGE_KEY_KEYBOARD_6_AND_CARET, '6', '^'},
        {HID_USAGE_KEY_KEYBOARD_7_AND_AMPERSAND, '7', '&'},
        {HID_USAGE_KEY_KEYBOARD_8_AND_ASTERISK, '8', '*'},
        {HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS, '9', '('},
        {HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS, '0', ')'},
        {HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE, '-', '_'},
        {HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, '=', '+'},
        {HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE, '[', '{'},
        {HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE, ']', '}'},
        {HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE, '\\', '|'},
        {HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON, ';', ':'},
        {HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE, '\'', '"'},
        {HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE, '`', '~'},
        {HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN, ',', '<'},
        {HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN, '.', '>'},
        {HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK, '/', '?'},
        {HID_USAGE_KEY_KEYBOARD_SPACEBAR, ' ', ' '},
        {HID_USAGE_KEY_KEYBOARD_TAB, '\t', '\t'},
        {HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, '\n', '\n'},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (int caps = 0; caps < 2; caps++) {
            char character = '\0';
            assert(toucan_keycode_to_character(cases[i].keycode, 0, caps, &character));
            assert(character == cases[i].plain);
            assert(toucan_keycode_to_character(cases[i].keycode, MOD_RSFT, caps, &character));
            assert(character == cases[i].shifted);
        }
    }
}

static void event(struct toucan_key_text_state *state, uint16_t keycode, uint8_t implicit,
                  uint8_t explicit, bool pressed, char expected) {
    char character = '\0';
    bool printable = toucan_key_text_apply(state, HID_USAGE_KEY, keycode, implicit, explicit,
                                           pressed, &character);
    assert(printable == (expected != '\0'));
    if (printable) {
        assert(character == expected);
    }
}

static void test_sequence(void) {
    struct toucan_key_text_state state = {0};
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, true, 'a');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, 0, 0, true, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, true, 'A');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_B, 0, 0, true, 'B');
    event(&state, HID_USAGE_KEY_KEYBOARD_B, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_C, 0, 0, true, 'c');
    event(&state, HID_USAGE_KEY_KEYBOARD_C, 0, 0, false, '\0');

    /* Implicit Shift only applies to that action, not the next character. */
    event(&state, HID_USAGE_KEY_KEYBOARD_D, MOD_LSFT, 0, true, 'D');
    event(&state, HID_USAGE_KEY_KEYBOARD_D, MOD_LSFT, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_E, 0, 0, true, 'e');
    event(&state, HID_USAGE_KEY_KEYBOARD_E, 0, 0, false, '\0');

    /* Releasing one Shift must not release the other. */
    event(&state, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, 0, 0, true, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT, 0, 0, true, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, true, 'A');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT, 0, 0, false, '\0');

    /* Compound Ctrl+Shift overlaps a separately held left Shift. */
    event(&state, HID_USAGE_KEY_KEYBOARD_LEFTCONTROL, 0, MOD_LSFT, true, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, 0, 0, true, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_LEFTCONTROL, 0, MOD_LSFT, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, true, 'A');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, true, 'a');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, false, '\0');

    /* Shifted punctuation follows the same held-modifier state as letters. */
    event(&state, HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT, 0, 0, true, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION, 0, 0, true, '!');
    event(&state, HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE, 0, 0, true, '"');
    event(&state, HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT, 0, 0, false, '\0');

    struct toucan_key_text_state empty = {0};
    assert(memcmp(&state, &empty, sizeof(state)) == 0);
}

static void test_sequence_caps_lock(void) {
    struct toucan_key_text_state state = {.caps_lock = true};
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, true, 'A');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, MOD_RSFT, 0, true, 'a');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, MOD_RSFT, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_CAPS_LOCK, 0, 0, true, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_CAPS_LOCK, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, true, 'a');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_CAPS_LOCK, 0, 0, true, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_CAPS_LOCK, 0, 0, false, '\0');
    assert(state.caps_lock);
}

static void test_nonprinting_events(void) {
    struct toucan_key_text_state state = {0};
    char character = '\0';
    assert(!toucan_key_text_apply(&state, HID_USAGE_CONSUMER, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT,
                                  0, MOD_LSFT, true, &character));
    event(&state, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, true, 'a');
    event(&state, HID_USAGE_KEY_KEYBOARD_A, 0, 0, false, '\0');
    event(&state, HID_USAGE_KEY_KEYBOARD_ESCAPE, 0, 0, true, '\0');
}

int main(void) {
    test_case_sensitive_glyph_lookup();
    test_alphabet();
    test_punctuation();
    test_sequence();
    test_sequence_caps_lock();
    test_nonprinting_events();
    puts("PASS: case-sensitive glyph lookup, TEXT decoding, and SEQ modifier state");
    return 0;
}
