/*
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

int toucan_battery_estimator_get_raw_millivolts(uint16_t *raw_millivolts);
int toucan_battery_estimator_read_raw_millivolts(uint16_t *raw_millivolts);
