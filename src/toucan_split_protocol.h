/*
 * Toucan-private messages carried by a ZMK input-split characteristic.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* Outside Zephyr's defined MSC code range, so normal input cannot collide. */
#define TOUCAN_INPUT_MSC_PACKED_XY 0x7F01U
