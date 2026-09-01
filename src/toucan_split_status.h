/*
 * Explicit right-half status for the Toucan2 central display.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

#include <zmk/event_manager.h>

struct toucan_split_status_changed {
    bool connected;
    bool usb_powered;
};

ZMK_EVENT_DECLARE(toucan_split_status_changed);

bool toucan_split_right_is_connected(void);
bool toucan_split_right_is_usb_powered(void);
