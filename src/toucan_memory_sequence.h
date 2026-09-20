/*
 * Live compilation of typed modifier/key names into ordinary recorded key events.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY 64
/* Room for all four modifier names plus a single target key for each of 32 taps.
 * Spelling out a named target uses additional raw events within this fixed budget. */
#define TOUCAN_MEMORY_SEQUENCE_INPUT_CAPACITY (TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY * 16)
#define TOUCAN_MEMORY_MODIFIER_COUNT 4

/* Persisted layout: keep this compatible with memory settings version 3. */
struct toucan_memory_sequence_action {
    uint16_t usage_page;
    uint16_t keycode;
    uint8_t implicit_modifiers;
    uint8_t explicit_modifiers;
    bool pressed;
};

/* Capture-only presentation metadata, never persisted with the playback actions. */
struct toucan_memory_sequence_preview {
    /* Original input modifiers, before named modifiers are applied to playback. */
    uint8_t implicit_modifiers;
    /* Unique icon masks in entry order, using left-side bits; unused entries are zero.
     * Modifiers from a single compound binding have no entry order and follow names
     * in the existing GUI/Ctrl/Alt/Shift order. */
    uint8_t modifier_order[TOUCAN_MEMORY_MODIFIER_COUNT];
};

struct toucan_memory_sequence_result {
    uint8_t count;
    bool overflow;
};

/* Recompile the complete input on every event: a provisional "ctrls" may later
 * become "ctrlshift". Input is never modified. Output and preview must each have
 * ACTION_CAPACITY entries and must not overlap each other or input. Write directly
 * into caller-owned buffers to keep presentation data off the event-thread stack.
 * Overflowed output is preview-only, not savable. */
struct toucan_memory_sequence_result toucan_memory_sequence_compile(
    const struct toucan_memory_sequence_action *input, size_t input_count,
    struct toucan_memory_sequence_action *output, struct toucan_memory_sequence_preview *preview);
