/*
 * US-layout character decoding shared by memory capture and its preview.
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/modifiers.h>

#include "toucan_key_text.h"

static uint8_t modifier_mask(uint16_t keycode) {
    if (keycode < HID_USAGE_KEY_KEYBOARD_LEFTCONTROL ||
        keycode > HID_USAGE_KEY_KEYBOARD_RIGHT_GUI) {
        return 0U;
    }

    return 1U << (keycode - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
}

bool toucan_keycode_to_character(uint16_t keycode, uint8_t modifiers, bool caps_lock,
                                char *character) {
    bool shifted = (modifiers & (MOD_LSFT | MOD_RSFT)) != 0U;

    if (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z) {
        *character = (char)(((shifted != caps_lock) ? 'A' : 'a') +
                            keycode - HID_USAGE_KEY_KEYBOARD_A);
        return true;
    }

    if (keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
        keycode <= HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS) {
        static const char normal[] = "123456789";
        static const char shifted_chars[] = "!@#$%^&*(";
        size_t index = keycode - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION;
        *character = shifted ? shifted_chars[index] : normal[index];
        return true;
    }
    if (keycode == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
        *character = shifted ? ')' : '0';
        return true;
    }

    switch (keycode) {
    case HID_USAGE_KEY_KEYBOARD_RETURN_ENTER:
        *character = '\n';
        return true;
    case HID_USAGE_KEY_KEYBOARD_TAB:
        *character = '\t';
        return true;
    case HID_USAGE_KEY_KEYBOARD_SPACEBAR:
        *character = ' ';
        return true;
    case HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE:
        *character = shifted ? '_' : '-';
        return true;
    case HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS:
        *character = shifted ? '+' : '=';
        return true;
    case HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE:
        *character = shifted ? '{' : '[';
        return true;
    case HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE:
        *character = shifted ? '}' : ']';
        return true;
    case HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE:
        *character = shifted ? '|' : '\\';
        return true;
    case HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON:
        *character = shifted ? ':' : ';';
        return true;
    case HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE:
        *character = shifted ? '"' : '\'';
        return true;
    case HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE:
        *character = shifted ? '~' : '`';
        return true;
    case HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN:
        *character = shifted ? '<' : ',';
        return true;
    case HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN:
        *character = shifted ? '>' : '.';
        return true;
    case HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK:
        *character = shifted ? '?' : '/';
        return true;
    default:
        return false;
    }
}

bool toucan_key_text_apply(struct toucan_key_text_state *state, uint16_t usage_page,
                          uint16_t keycode, uint8_t implicit_modifiers,
                          uint8_t explicit_modifiers, bool pressed, char *character) {
    if (usage_page != HID_USAGE_KEY) {
        return false;
    }

    uint8_t modifier = modifier_mask(keycode);
    if (modifier != 0U) {
        /* Count overlapping physical and compound modifiers independently. */
        modifier |= explicit_modifiers;
        for (int i = 0; i < 8; i++) {
            if ((modifier & (1U << i)) == 0U) {
                continue;
            }
            if (pressed) {
                state->modifier_counts[i]++;
            } else if (state->modifier_counts[i] > 0U) {
                state->modifier_counts[i]--;
            }
        }
        return false;
    }
    if (!pressed) {
        return false;
    }
    if (keycode == HID_USAGE_KEY_KEYBOARD_CAPS_LOCK) {
        state->caps_lock = !state->caps_lock;
        return false;
    }

    uint8_t modifiers = implicit_modifiers | explicit_modifiers | toucan_key_text_modifiers(state);
    return toucan_keycode_to_character(keycode, modifiers, state->caps_lock, character);
}

uint8_t toucan_key_text_modifiers(const struct toucan_key_text_state *state) {
    uint8_t modifiers = 0U;
    for (int i = 0; i < 8; i++) {
        if (state->modifier_counts[i] > 0U) {
            modifiers |= 1U << i;
        }
    }
    return modifiers;
}
