/*
 * Tests of the live parser, including provisional prefixes and raw-key rollover.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/modifiers.h>

#include "toucan_key_text.h"
#include "toucan_memory_sequence.h"

static struct toucan_memory_sequence_action input[TOUCAN_MEMORY_SEQUENCE_INPUT_CAPACITY];
static size_t input_count;
static struct {
    struct toucan_memory_sequence_action events[TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY];
    uint32_t guard;
    struct toucan_memory_sequence_preview preview[TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY];
    uint32_t preview_guard;
} output;
static struct toucan_memory_sequence_result result;

static uint16_t key(char letter) {
    if (letter >= 'A' && letter <= 'Z') {
        letter += 'a' - 'A';
    }
    assert(letter >= 'a' && letter <= 'z');
    return HID_USAGE_KEY_KEYBOARD_A + letter - 'a';
}

static void reset(void) {
    memset(input, 0, sizeof(input));
    input_count = 0U;
}

static void event(uint16_t code, bool pressed, uint8_t implicit, uint8_t explicit) {
    assert(input_count < TOUCAN_MEMORY_SEQUENCE_INPUT_CAPACITY);
    input[input_count++] = (struct toucan_memory_sequence_action){
        .usage_page = HID_USAGE_KEY,
        .keycode = code,
        .implicit_modifiers = implicit,
        .explicit_modifiers = explicit,
        .pressed = pressed,
    };
}

static void text(const char *letters) {
    for (; *letters; letters++) {
        uint8_t shift = *letters >= 'A' && *letters <= 'Z' ? MOD_LSFT : 0U;
        event(key(*letters), true, shift, 0);
        event(key(*letters), false, shift, 0);
    }
}

static void compile(void) {
    struct toucan_memory_sequence_action original[TOUCAN_MEMORY_SEQUENCE_INPUT_CAPACITY];
    memcpy(original, input, sizeof(input));
    output.guard = 0x1234abcd;
    output.preview_guard = 0xabcd1234;
    result = toucan_memory_sequence_compile(input, input_count, output.events, output.preview);
    assert(output.guard == 0x1234abcd);
    assert(output.preview_guard == 0xabcd1234);
    assert(memcmp(original, input, sizeof(input)) == 0);
    /* Re-parsing a shorter result must not expose discarded source letters. */
    const unsigned char *unused = (const void *)&output.events[result.count];
    size_t unused_size = sizeof(output.events) - result.count * sizeof(output.events[0]);
    for (size_t i = 0; i < unused_size; i++) {
        assert(unused[i] == 0U);
    }
    const struct toucan_memory_sequence_preview empty_preview = {0};
    for (size_t i = result.count; i < TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY; i++) {
        assert(memcmp(&output.preview[i], &empty_preview, sizeof(empty_preview)) == 0);
    }
}

static void action(size_t index, uint16_t code, bool pressed, uint8_t implicit, uint8_t explicit) {
    assert(index < result.count);
    const struct toucan_memory_sequence_action *actual = &output.events[index];
    assert(actual->usage_page == HID_USAGE_KEY);
    assert(actual->keycode == code);
    assert(actual->pressed == pressed);
    assert(actual->implicit_modifiers == implicit);
    assert(actual->explicit_modifiers == explicit);
}

static void taps(const char *letters, const uint8_t *modifiers) {
    compile();
    assert(!result.overflow && result.count == strlen(letters) * 2);
    for (size_t i = 0; letters[i]; i++) {
        uint8_t mods = modifiers != NULL ? modifiers[i] : 0U;
        if (letters[i] >= 'A' && letters[i] <= 'Z') {
            mods |= MOD_LSFT;
        }
        action(2 * i, key(letters[i]), true, mods, 0);
        action(2 * i + 1, key(letters[i]), false, mods, 0);
    }
}

static void unchanged(void) {
    compile();
    assert(!result.overflow && result.count == input_count);
    for (size_t i = 0; i < input_count; i++) {
        assert(output.events[i].usage_page == input[i].usage_page);
        assert(output.events[i].keycode == input[i].keycode);
        assert(output.events[i].pressed == input[i].pressed);
        assert(output.events[i].implicit_modifiers == input[i].implicit_modifiers);
        assert(output.events[i].explicit_modifiers == input[i].explicit_modifiers);
        assert(output.preview[i].implicit_modifiers == input[i].implicit_modifiers);
    }
}

static void decoded_characters(bool preview, bool caps_lock, const char *expected) {
    compile();
    assert(!result.overflow);
    struct toucan_key_text_state state = {.caps_lock = caps_lock};
    char actual[TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY + 1] = {0};
    size_t length = 0U;
    for (size_t i = 0; i < result.count; i++) {
        const struct toucan_memory_sequence_action *action = &output.events[i];
        uint8_t implicit = preview ? output.preview[i].implicit_modifiers
                                   : action->implicit_modifiers;
        char character;
        if (toucan_key_text_apply(&state, action->usage_page, action->keycode, implicit,
                                 action->explicit_modifiers, action->pressed, &character)) {
            actual[length++] = character;
        }
    }
    assert(strcmp(actual, expected) == 0);
}

static void test_named_modifier_preview(void) {
    reset();
    text("shiftc");
    decoded_characters(true, false, "c"); /* Pending Shift must not recase a partial name. */
    decoded_characters(false, false, "C");
    text("md");
    decoded_characters(true, false, ""); /* Completed names show only modifier icons. */
    event(HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, true, 0, 0);
    decoded_characters(true, false, "4"); /* Key-down preview, before save or key-up. */
    assert(result.count == 1);
    action(0, HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, true, MOD_LSFT | MOD_LGUI, 0);
    event(HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, false, 0, 0);
    decoded_characters(false, false, "$");
    assert(result.count == 2);
    action(1, HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, false, MOD_LSFT | MOD_LGUI, 0);
    text("a");
    decoded_characters(true, false, "4a");
    decoded_characters(false, false, "$a");

    /* The rule applies to letters and all US-layout punctuation, not just 4. */
    for (uint16_t code = HID_USAGE_KEY_KEYBOARD_A;
         code <= HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK; code++) {
        char plain, shifted;
        if (!toucan_keycode_to_character(code, 0, false, &plain)) {
            continue;
        }
        assert(toucan_keycode_to_character(code, MOD_LSFT, false, &shifted));
        reset();
        text("shift");
        event(code, true, 0, 0);
        event(code, false, 0, 0);
        decoded_characters(true, false, (char[]){plain, '\0'});
        decoded_characters(false, false, (char[]){shifted, '\0'});
    }

    /* Don't lose literal Shift when a name adds that same modifier bit. */
    reset();
    text("shiftcmdA");
    decoded_characters(true, false, "A");
    decoded_characters(false, false, "A");
    assert(output.preview[0].implicit_modifiers == MOD_LSFT);
    assert(output.preview[1].implicit_modifiers == MOD_LSFT);

    const uint16_t shifts[] = {
        HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT,
    };
    for (size_t i = 0; i < sizeof(shifts) / sizeof(shifts[0]); i++) {
        reset();
        text("shiftcmd");
        event(shifts[i], true, 0, 0);
        event(HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, true, 0, 0);
        event(HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, false, 0, 0);
        event(shifts[i], false, 0, 0);
        text("a");
        decoded_characters(true, false, "$a"); /* Physically entered $, not a bare 4. */
        decoded_characters(false, false, "$a");
    }

    reset();
    text("shiftcmda");
    decoded_characters(true, true, "A"); /* Capture-local Caps Lock still controls case. */
    decoded_characters(false, true, "a");
}

static void icon_order(size_t index, const uint8_t expected[TOUCAN_MEMORY_MODIFIER_COUNT]) {
    assert(index < result.count);
    assert(memcmp(output.preview[index].modifier_order, expected,
                  sizeof(output.preview[index].modifier_order)) == 0);
}

static void check_modifier_permutation(const uint8_t order[TOUCAN_MEMORY_MODIFIER_COUNT]) {
    const struct {
        const char *name;
        uint16_t keycode;
        uint8_t mask;
    } modifiers[] = {
        {"ctrl", HID_USAGE_KEY_KEYBOARD_LEFTCONTROL, MOD_LCTL},
        {"shift", HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, MOD_LSFT},
        {"alt", HID_USAGE_KEY_KEYBOARD_LEFTALT, MOD_LALT},
        {"cmd", HID_USAGE_KEY_KEYBOARD_LEFT_GUI, MOD_LGUI},
    };
    uint8_t expected[TOUCAN_MEMORY_MODIFIER_COUNT] = {0};
    uint8_t mask = 0U;
    reset();
    for (size_t i = 0; i < TOUCAN_MEMORY_MODIFIER_COUNT; i++) {
        text(modifiers[order[i]].name);
        expected[i] = modifiers[order[i]].mask;
        mask |= expected[i];
        compile();
        assert(!result.overflow && result.count == 2 * (i + 1));
        /* Completed names stay in entry order before a target exists. */
        for (size_t j = 0; j <= i; j++) {
            action(j, modifiers[order[j]].keycode, true, 0, 0);
            action(2 * i + 1 - j, modifiers[order[j]].keycode, false, 0, 0);
        }
    }

    text(modifiers[order[0]].name); /* Repeating a name must not move its icon. */
    compile();
    assert(result.count == 2 * TOUCAN_MEMORY_MODIFIER_COUNT);
    for (size_t i = 0; i < TOUCAN_MEMORY_MODIFIER_COUNT; i++) {
        action(i, modifiers[order[i]].keycode, true, 0, 0);
    }

    event(HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, true, 0, 0);
    compile();
    assert(!result.overflow && result.count == 1);
    action(0, HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, true, mask, 0);
    icon_order(0, expected); /* Entering the target must not trigger a re-sort. */
    event(HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, false, 0, 0);
    compile();
    assert(!result.overflow && result.count == 2);
    action(1, HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR, false, mask, 0);
    icon_order(1, expected);

    text("cmdshift");
    compile();
    assert(result.count == 6);
    icon_order(0, expected); /* A later chord must not reorder the previous one. */
    action(2, HID_USAGE_KEY_KEYBOARD_LEFT_GUI, true, 0, 0);
    action(3, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, true, 0, 0);
}

static void test_modifier_order(void) {
    /* All 24 orders, including every shorter prefix along the way. */
    for (uint8_t a = 0; a < TOUCAN_MEMORY_MODIFIER_COUNT; a++) {
        for (uint8_t b = 0; b < TOUCAN_MEMORY_MODIFIER_COUNT; b++) {
            if (b == a) {
                continue;
            }
            for (uint8_t c = 0; c < TOUCAN_MEMORY_MODIFIER_COUNT; c++) {
                if (c == a || c == b) {
                    continue;
                }
                for (uint8_t d = 0; d < TOUCAN_MEMORY_MODIFIER_COUNT; d++) {
                    if (d != a && d != b && d != c) {
                        check_modifier_permutation((uint8_t[]){a, b, c, d});
                    }
                }
            }
        }
    }

    reset();
    text("shiftc");
    compile();
    icon_order(0, (uint8_t[]){MOD_LSFT, 0, 0, 0});
    text("md");
    compile();
    action(0, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, true, 0, 0);
    action(1, HID_USAGE_KEY_KEYBOARD_LEFT_GUI, true, 0, 0);
    /* A physically shifted binding shares one Shift icon with the typed name. */
    event(key('a'), true, MOD_RSFT | MOD_LALT, 0);
    compile();
    icon_order(0, (uint8_t[]){MOD_LSFT, MOD_LGUI, MOD_LALT, 0});
    action(0, key('a'), true, MOD_LSFT | MOD_LGUI | MOD_RSFT | MOD_LALT, 0);
    decoded_characters(true, false, "A");

    reset();
    event(key('a'), true, MOD_RSFT | MOD_RGUI | MOD_RCTL | MOD_RALT, 0);
    unchanged(); /* Unordered compound bindings keep their established icon order. */
    icon_order(0, (uint8_t[]){MOD_LGUI, MOD_LCTL, MOD_LALT, MOD_LSFT});

    reset();
    text("shiftcmd");
    event(key('a'), true, 0, 0);
    text("altctrl");
    event(key('b'), true, 0, 0);
    event(key('a'), false, 0, 0);
    event(key('b'), false, 0, 0);
    compile();
    assert(!result.overflow && result.count == 4);
    icon_order(0, (uint8_t[]){MOD_LSFT, MOD_LGUI, 0, 0});
    icon_order(1, (uint8_t[]){MOD_LALT, MOD_LCTL, 0, 0});
    icon_order(2, (uint8_t[]){MOD_LSFT, MOD_LGUI, 0, 0});
    icon_order(3, (uint8_t[]){MOD_LALT, MOD_LCTL, 0, 0});
}

static void test_literal_symbol_preview(void) {
    const uint16_t codes[] = {
        HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION, HID_USAGE_KEY_KEYBOARD_2_AND_AT,
        HID_USAGE_KEY_KEYBOARD_3_AND_HASH, HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR,
        HID_USAGE_KEY_KEYBOARD_5_AND_PERCENT, HID_USAGE_KEY_KEYBOARD_6_AND_CARET,
        HID_USAGE_KEY_KEYBOARD_7_AND_AMPERSAND, HID_USAGE_KEY_KEYBOARD_8_AND_ASTERISK,
        HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS, HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS,
        HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE, HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS,
        HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE,
        HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE,
        HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE, HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON,
        HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE, HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE,
        HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN, HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN,
        HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK,
    };
    const char *symbols = "!@#$%^&*()_+{}|:\"~<>?";
    const uint8_t shifts[] = {MOD_LSFT, MOD_RSFT, MOD_LSFT | MOD_RSFT};
    const uint8_t no_icons[TOUCAN_MEMORY_MODIFIER_COUNT] = {0};
    assert(sizeof(codes) / sizeof(codes[0]) == strlen(symbols));
    for (size_t s = 0; s < sizeof(shifts) / sizeof(shifts[0]); s++) {
        reset();
        for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
            event(codes[i], true, shifts[s], 0);
            event(codes[i], false, shifts[s], 0);
        }
        unchanged(); /* Only icon metadata changes, never the captured key events. */
        for (size_t i = 0; i < result.count; i++) {
            icon_order(i, no_icons);
        }
        decoded_characters(true, false, symbols);
        decoded_characters(true, true, symbols);
        decoded_characters(false, false, symbols);
    }

    /* Named modifiers stay visible, even when Shift overlaps the symbol's Shift. */
    const struct {
        const char *names;
        uint8_t modifiers;
        uint8_t order[TOUCAN_MEMORY_MODIFIER_COUNT];
    } cases[] = {
        {"", 0, {0}},
        {"cmd", MOD_LGUI, {MOD_LGUI}},
        {"shiftcmd", MOD_LSFT | MOD_LGUI, {MOD_LSFT, MOD_LGUI}},
        {"cmdshift", MOD_LGUI | MOD_LSFT, {MOD_LGUI, MOD_LSFT}},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset();
        text(cases[i].names);
        event(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, true, MOD_LSFT, 0);
        compile(); /* Correct immediately on key-down, not only after release/save. */
        assert(!result.overflow && result.count == 1);
        icon_order(0, cases[i].order);
        action(0, HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, true, MOD_LSFT | cases[i].modifiers, 0);
        event(key('a'), true, 0, 0);
        event(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, false, MOD_LSFT, 0);
        event(key('a'), false, 0, 0);
        compile();
        assert(!result.overflow && result.count == 4);
        action(2, HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, false, MOD_LSFT | cases[i].modifiers, 0);
        action(1, key('a'), true, 0, 0);
        action(3, key('a'), false, 0, 0);
        icon_order(0, cases[i].order);
        icon_order(2, cases[i].order); /* Rollover release must not re-add hidden Shift. */
        icon_order(1, no_icons);
        icon_order(3, no_icons);
        assert(output.preview[0].implicit_modifiers == MOD_LSFT);
        assert(output.preview[2].implicit_modifiers == MOD_LSFT);
        decoded_characters(true, false, "+a");
        decoded_characters(false, false, "+a");
    }

    reset();
    uint8_t modifiers = MOD_RSFT | MOD_RGUI | MOD_RCTL | MOD_RALT;
    event(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, true, modifiers, 0);
    event(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, false, modifiers, 0);
    unchanged();
    icon_order(0, (uint8_t[]){MOD_LGUI, MOD_LCTL, MOD_LALT, 0});
    icon_order(1, (uint8_t[]){MOD_LGUI, MOD_LCTL, MOD_LALT, 0});
}

static void test_deliberate_shift_preview(void) {
    /* Shift remains meaningful on letter shortcuts, controls and unknown usages. */
    const uint16_t codes[] = {
        HID_USAGE_KEY_KEYBOARD_Z, HID_USAGE_KEY_KEYBOARD_TAB,
        HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, HID_USAGE_KEY_KEYBOARD_SPACEBAR,
        HID_USAGE_KEY_KEYBOARD_LEFTARROW,
    };
    const uint8_t shift_icon[TOUCAN_MEMORY_MODIFIER_COUNT] = {MOD_LSFT};
    const uint8_t no_icons[TOUCAN_MEMORY_MODIFIER_COUNT] = {0};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        reset();
        event(codes[i], true, MOD_LSFT, 0);
        event(codes[i], false, MOD_LSFT, 0);
        unchanged();
        icon_order(0, shift_icon);
        icon_order(1, shift_icon);
    }
    reset();
    event(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, true, MOD_LSFT, 0);
    event(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, false, MOD_LSFT, 0);
    input[0].usage_page = input[1].usage_page = HID_USAGE_CONSUMER;
    unchanged(); /* A matching numeric usage on another page is not a symbol. */
    icon_order(0, shift_icon);
    icon_order(1, shift_icon);

    reset();
    event(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, true, MOD_LSFT, MOD_RSFT);
    event(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, false, MOD_LSFT, MOD_RSFT);
    unchanged(); /* Explicit Shift is never hidden by an overlapping implicit Shift. */
    icon_order(0, shift_icon);
    icon_order(1, shift_icon);

    const uint16_t physical_shifts[] = {
        HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT,
    };
    for (size_t i = 0; i < sizeof(physical_shifts) / sizeof(physical_shifts[0]); i++) {
        reset();
        event(physical_shifts[i], true, 0, 0);
        event(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, true, MOD_LSFT, 0);
        event(HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, false, MOD_LSFT, 0);
        event(physical_shifts[i], false, 0, 0);
        unchanged(); /* Physical Shift remains a separate action/icon in the renderer. */
        action(0, physical_shifts[i], true, 0, 0);
        action(3, physical_shifts[i], false, 0, 0);
        icon_order(1, no_icons);
        icon_order(2, no_icons);
        decoded_characters(true, false, "+");
    }
}

static void test_live_prefixes(void) {
    reset();
    text("c");
    taps("c", NULL);
    text("t");
    taps("ct", NULL);
    text("r");
    taps("ctr", NULL);
    event(key('l'), true, 0, 0);
    compile(); /* Recognition must happen on key-down, not after key-up/save. */
    assert(result.count == 2 && !result.overflow);
    action(0, HID_USAGE_KEY_KEYBOARD_LEFTCONTROL, true, 0, 0);
    action(1, HID_USAGE_KEY_KEYBOARD_LEFTCONTROL, false, 0, 0);
    event(key('l'), false, 0, 0);
    text("s");
    taps("s", (uint8_t[]){MOD_LCTL});
    text("hif");
    taps("shif", (uint8_t[]){MOD_LCTL, 0, 0, 0});
    event(key('t'), true, 0, 0);
    compile();
    assert(result.count == 4 && !result.overflow);
    action(0, HID_USAGE_KEY_KEYBOARD_LEFTCONTROL, true, 0, 0);
    action(1, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, true, 0, 0);
    action(2, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false, 0, 0);
    action(3, HID_USAGE_KEY_KEYBOARD_LEFTCONTROL, false, 0, 0);
    event(key('t'), false, 0, 0);
    text("p");
    taps("p", (uint8_t[]){MOD_LCTL | MOD_LSFT});
    text("a");
    taps("pa", (uint8_t[]){MOD_LCTL | MOD_LSFT, 0});
}

static void test_names_and_case(void) {
    const struct {
        const char *input;
        const char *keys;
        uint8_t modifiers[4];
    } cases[] = {
        {"ctrls", "s", {MOD_LCTL}},
        {"shifta", "a", {MOD_LSFT}},
        {"altx", "x", {MOD_LALT}},
        {"cmdc", "c", {MOD_LGUI}},
        {"ctrlshiftaltcmdx", "x", {MOD_LCTL | MOD_LSFT | MOD_LALT | MOD_LGUI}},
        {"ctrlsctrlc", "sc", {MOD_LCTL, MOD_LCTL}},
        {"ctrlctrls", "s", {MOD_LCTL}},
        {"CTRLs", "s", {MOD_LCTL}},
        {"ctrlS", "S", {MOD_LCTL}},
        {"aBcmdcD", "aBcD", {0, 0, MOD_LGUI, 0}},
        {"shifted", "ed", {MOD_LSFT, 0}},
        {"ctrlsa", "sa", {MOD_LCTL, 0}},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset();
        text(cases[i].input);
        taps(cases[i].keys, cases[i].modifiers);
    }
    reset();
    text("control");
    unchanged(); /* Only the documented short names are reserved. */

    reset();
    text("cmdshift");
    compile();
    assert(!result.overflow && result.count == 4);
    action(0, HID_USAGE_KEY_KEYBOARD_LEFT_GUI, true, 0, 0);
    action(1, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, true, 0, 0);
    action(2, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false, 0, 0);
    action(3, HID_USAGE_KEY_KEYBOARD_LEFT_GUI, false, 0, 0);
}

static void test_raw_chords_and_boundaries(void) {
    const uint16_t modifiers[] = {
        HID_USAGE_KEY_KEYBOARD_LEFTCONTROL,
        HID_USAGE_KEY_KEYBOARD_RIGHTALT,
        HID_USAGE_KEY_KEYBOARD_LEFT_GUI,
    };
    for (size_t i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); i++) {
        reset();
        event(modifiers[i], true, 0, 0);
        text("ctrlshiftpdel");
        event(modifiers[i], false, 0, 0);
        unchanged();
    }
    reset();
    event(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, true, 0, MOD_LCTL);
    text("ctrl");
    event(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false, 0, MOD_LCTL);
    unchanged();

    reset();
    text("ct");
    event(HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, true, 0, 0);
    event(HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, false, 0, 0);
    text("rl");
    unchanged();

    reset();
    text("ct");
    event(HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT, true, 0, 0);
    text("rl");
    event(HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT, false, 0, 0);
    unchanged();

    reset();
    text("ctrl");
    event(HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT, true, 0, 0);
    text("p");
    event(HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT, false, 0, 0);
    compile();
    assert(!result.overflow && result.count == 4);
    action(0, HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT, true, 0, 0);
    action(1, key('p'), true, MOD_LCTL, 0);
    action(2, key('p'), false, MOD_LCTL, 0);
    action(3, HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT, false, 0, 0);

    const uint16_t targets[] = {
        HID_USAGE_KEY_KEYBOARD_SPACEBAR, HID_USAGE_KEY_KEYBOARD_RETURN_ENTER,
        HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE, HID_USAGE_KEY_KEYBOARD_ESCAPE,
    };
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
        reset();
        text("ctrl");
        event(targets[i], true, 0, 0);
        event(targets[i], false, 0, 0);
        compile();
        assert(!result.overflow && result.count == 2);
        action(0, targets[i], true, MOD_LCTL, 0);
        action(1, targets[i], false, MOD_LCTL, 0);
    }
}

static void test_rollovers(void) {
    reset();
    event(key('c'), true, 0, 0);
    event(key('t'), true, 0, 0);
    event(key('c'), false, 0, 0);
    event(key('r'), true, 0, 0);
    event(key('t'), false, 0, 0);
    event(key('l'), true, 0, 0);
    event(key('s'), true, 0, 0);
    event(key('r'), false, 0, 0);
    event(key('l'), false, 0, 0);
    event(key('s'), false, 0, 0);
    taps("s", (uint8_t[]){MOD_LCTL});

    reset();
    text("ctrl");
    event(key('s'), true, 0, 0);
    event(key('b'), true, 0, 0);
    event(key('s'), false, 0, 0);
    event(key('b'), false, 0, 0);
    compile();
    assert(!result.overflow && result.count == 4);
    action(0, key('s'), true, MOD_LCTL, 0);
    action(1, key('b'), true, 0, 0);
    action(2, key('s'), false, MOD_LCTL, 0);
    action(3, key('b'), false, 0, 0);

    reset();
    text("ctrl");
    event(key('s'), true, 0, 0);
    event(key('s'), true, 0, 0);
    event(key('s'), false, 0, 0);
    event(key('s'), false, 0, 0);
    compile();
    assert(!result.overflow && result.count == 4);
    action(0, key('s'), true, MOD_LCTL, 0);
    action(1, key('s'), true, 0, 0);
    action(2, key('s'), false, 0, 0);
    action(3, key('s'), false, MOD_LCTL, 0);
}

static void delete_tap(size_t index, uint8_t modifiers,
                       const uint8_t order[TOUCAN_MEMORY_MODIFIER_COUNT]) {
    action(index, HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD, true, modifiers, 0);
    action(index + 1, HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD, false, modifiers, 0);
    icon_order(index, order);
    icon_order(index + 1, order);
    assert(output.preview[index].implicit_modifiers == 0U);
    assert(output.preview[index + 1].implicit_modifiers == 0U);
}

static void test_del_shorthand(void) {
    const uint8_t no_modifiers[TOUCAN_MEMORY_MODIFIER_COUNT] = {0};
    reset();
    text("d");
    taps("d", NULL);
    text("e");
    taps("de", NULL);
    event(key('l'), true, 0, 0);
    compile(); /* Complete on key-down, without waiting for key-up or save. */
    assert(!result.overflow && result.count == 2);
    delete_tap(0, 0, no_modifiers);
    event(key('l'), false, 0, 0);
    text("del");
    compile();
    assert(!result.overflow && result.count == 4);
    delete_tap(0, 0, no_modifiers);
    delete_tap(2, 0, no_modifiers); /* Repeated names are separate Delete taps. */

    const struct {
        const char *input;
        uint8_t modifiers;
        uint8_t order[TOUCAN_MEMORY_MODIFIER_COUNT];
    } cases[] = {
        {"del", 0, {0}},
        {"DEL", 0, {0}},
        {"DeL", 0, {0}},
        {"cmddel", MOD_LGUI, {MOD_LGUI}},
        {"shiftcmddel", MOD_LSFT | MOD_LGUI, {MOD_LSFT, MOD_LGUI}},
        {"cmdshiftdel", MOD_LGUI | MOD_LSFT, {MOD_LGUI, MOD_LSFT}},
        {"ctrlaltdel", MOD_LCTL | MOD_LALT, {MOD_LCTL, MOD_LALT}},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset();
        text(cases[i].input);
        text("a");
        compile();
        assert(!result.overflow && result.count == 4);
        delete_tap(0, cases[i].modifiers, cases[i].order);
        action(2, key('a'), true, 0, 0); /* Delete consumes the pending modifiers. */
        action(3, key('a'), false, 0, 0);
    }

    reset();
    text("model");
    compile(); /* Like modifier names, del is reserved even inside words. */
    assert(!result.overflow && result.count == 6);
    delete_tap(4, 0, no_modifiers);
    decoded_characters(true, false, "mo");

    reset();
    text("delctrl");
    compile();
    assert(!result.overflow && result.count == 4);
    delete_tap(0, 0, no_modifiers);
    action(2, HID_USAGE_KEY_KEYBOARD_LEFTCONTROL, true, 0, 0);
    action(3, HID_USAGE_KEY_KEYBOARD_LEFTCONTROL, false, 0, 0);

    reset();
    text("de");
    event(HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, true, 0, 0);
    event(HID_USAGE_KEY_KEYBOARD_RETURN_ENTER, false, 0, 0);
    text("l");
    unchanged(); /* A name cannot cross another key or physical modifier change. */
    reset();
    text("d");
    event(HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT, true, 0, 0);
    text("el");
    event(HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT, false, 0, 0);
    unchanged();

    reset();
    event(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, true, 0, 0);
    text("del");
    event(HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false, 0, 0);
    compile();
    assert(!result.overflow && result.count == 4);
    action(0, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, true, 0, 0);
    delete_tap(1, 0, no_modifiers);
    action(3, HID_USAGE_KEY_KEYBOARD_LEFTSHIFT, false, 0, 0);

    reset();
    event(key('d'), true, 0, 0);
    event(key('e'), true, 0, 0);
    event(key('d'), false, 0, 0);
    event(key('l'), true, 0, 0);
    event(key('a'), true, 0, 0);
    event(key('e'), false, 0, 0);
    event(key('l'), false, 0, 0);
    event(key('a'), false, 0, 0);
    compile();
    assert(!result.overflow && result.count == 4);
    delete_tap(0, 0, no_modifiers);
    action(2, key('a'), true, 0, 0);
    action(3, key('a'), false, 0, 0);

    /* Synthetic taps must never steal a release belonging to a real input key,
     * including a physically held Delete key with the same usage. */
    const uint16_t held_keys[] = {HID_USAGE_KEY_KEYBOARD_A, HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD};
    for (size_t i = 0; i < sizeof(held_keys) / sizeof(held_keys[0]); i++) {
        reset();
        event(held_keys[i], true, 0, 0);
        text("cmddel");
        event(held_keys[i], false, 0, 0);
        compile();
        assert(!result.overflow && result.count == 4);
        action(0, held_keys[i], true, 0, 0);
        delete_tap(1, MOD_LGUI, (uint8_t[]){MOD_LGUI, 0, 0, 0});
        action(3, held_keys[i], false, 0, 0);
        icon_order(3, no_modifiers);
    }

    reset();
    for (size_t i = 0; i < 31; i++) {
        text("del");
    }
    text("de");
    compile();
    assert(result.overflow);
    text("l");
    compile(); /* Completing del can bring provisional output back within the limit. */
    assert(!result.overflow && result.count == TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY);
    delete_tap(62, 0, no_modifiers);
    text("del");
    compile();
    assert(result.overflow && result.count == TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY);
}

static void test_bounds(void) {
    reset();
    for (int i = 0; i < 32; i++) {
        text("a");
    }
    unchanged();
    text("b");
    compile();
    assert(result.overflow && result.count == TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY);

    reset();
    for (int i = 0; i < 30; i++) {
        text("z");
    }
    text("ctr");
    compile();
    assert(result.overflow);
    text("ls");
    compile(); /* Completing a name may make a provisional overflow fit again. */
    assert(!result.overflow && result.count == 62);
    action(60, key('s'), true, MOD_LCTL, 0);
    action(61, key('s'), false, MOD_LCTL, 0);

    reset();
    for (int i = 0; i < 32; i++) {
        text("ctrlshiftaltcmdx");
    }
    assert(input_count == TOUCAN_MEMORY_SEQUENCE_INPUT_CAPACITY);
    compile();
    assert(!result.overflow && result.count == 64);
    for (int i = 0; i < 64; i++) {
        action(i, key('x'), (i % 2) == 0, MOD_LCTL | MOD_LSFT | MOD_LALT | MOD_LGUI, 0);
    }
    result = toucan_memory_sequence_compile(input, input_count + 1, output.events, output.preview);
    assert(result.overflow && output.guard == 0x1234abcd && output.preview_guard == 0xabcd1234);
}

int main(void) {
    test_live_prefixes();
    test_names_and_case();
    test_raw_chords_and_boundaries();
    test_rollovers();
    test_named_modifier_preview();
    test_modifier_order();
    test_literal_symbol_preview();
    test_deliberate_shift_preview();
    test_del_shorthand();
    test_bounds();
    puts("PASS: del shorthand, modifier order, literal symbols, playback, rollover, and bounds");
    return 0;
}
