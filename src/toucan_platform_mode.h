/*
 * Public state exposed by the Toucan2 platform-mode behavior.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

#include <zmk/event_manager.h>

struct toucan_platform_mode_changed {
    bool windows;
};

ZMK_EVENT_DECLARE(toucan_platform_mode_changed);

bool toucan_platform_is_windows(void);
