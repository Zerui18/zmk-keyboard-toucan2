/*
 * Persistent five-slot macro memory shared with the Toucan display.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zmk/event_manager.h>

#include "toucan_memory_sequence.h"

#define TOUCAN_MEMORY_SLOT_COUNT 5
#define TOUCAN_MEMORY_TEXT_CAPACITY 64

enum toucan_memory_slot_type {
    TOUCAN_MEMORY_SLOT_EMPTY,
    TOUCAN_MEMORY_SLOT_SEQUENCE,
    TOUCAN_MEMORY_SLOT_TEXT,
};

enum toucan_memory_capture_mode {
    TOUCAN_MEMORY_CAPTURE_SEQUENCE,
    TOUCAN_MEMORY_CAPTURE_TEXT,
};

struct toucan_memory_capture_snapshot {
    bool active;
    uint8_t slot;
    uint8_t mode;
    bool initial_caps_lock;
    bool sequence_overflow;
    uint8_t sequence_action_count;
    struct toucan_memory_sequence_action
        sequence[TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY];
    struct toucan_memory_sequence_preview sequence_preview[TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY];
    char text[TOUCAN_MEMORY_TEXT_CAPACITY + 1];
};

struct toucan_memory_snapshot {
    uint8_t slot_types[TOUCAN_MEMORY_SLOT_COUNT];
    struct toucan_memory_capture_snapshot capture;
};

struct toucan_memory_state_changed {
    int8_t slot;
    bool saved;
};

ZMK_EVENT_DECLARE(toucan_memory_state_changed);

void toucan_memory_get_snapshot(struct toucan_memory_snapshot *snapshot);
