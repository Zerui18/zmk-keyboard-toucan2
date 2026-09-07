/*
 * US-layout character decoding shared by memory capture and its preview.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

bool toucan_keycode_to_character(uint16_t keycode, uint8_t modifiers, bool caps_lock,
                                char *character);

/* Transient capture/preview state, not part of the persisted sequence format. */
struct toucan_key_text_state {
    uint8_t modifier_counts[8];
    bool caps_lock;
};

uint8_t toucan_key_text_modifiers(const struct toucan_key_text_state *state);

/* Consume every event, including releases. Returns a character only on key-down.
 * Caps Lock is simulated locally because capture never sends its keys to the host. */
bool toucan_key_text_apply(struct toucan_key_text_state *state, uint16_t usage_page,
                          uint16_t keycode, uint8_t implicit_modifiers,
                          uint8_t explicit_modifiers, bool pressed, char *character);
