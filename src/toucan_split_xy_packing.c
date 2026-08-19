/*
 * Reduce Toucan right-to-left trackpad traffic by packing each X/Y pair into
 * one input-split notification. The central reconstructs ordinary REL events
 * before ZMK's normal trackpad listener sees them.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_toucan_split_xy_packer

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>

#include "toucan_split_protocol.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct toucan_split_xy_packer_data {
    int16_t pending_x;
    bool has_pending_x;
};

static int toucan_split_xy_packer_handle_event(
    const struct device *dev, struct input_event *event, uint32_t param1, uint32_t param2,
    struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    struct toucan_split_xy_packer_data *data = dev->data;

    if (event->code == INPUT_REL_X) {
        data->pending_x = (int16_t)CLAMP(event->value, INT16_MIN, INT16_MAX);
        data->has_pending_x = true;

        /* The following Y event will carry both axes across the split. */
        return ZMK_INPUT_PROC_STOP;
    }

    if (event->code != INPUT_REL_Y || !data->has_pending_x) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    int16_t y = (int16_t)CLAMP(event->value, INT16_MIN, INT16_MAX);
    uint32_t packed = ((uint32_t)(uint16_t)data->pending_x << 16) | (uint16_t)y;

    data->has_pending_x = false;
    event->type = INPUT_EV_MSC;
    event->code = TOUCAN_INPUT_MSC_PACKED_XY;
    event->value = (int32_t)packed;
    /* The reconstructed Y event supplies the sync on the central. Keeping the
     * transport envelope unsynchronised also prevents ZMK's normal listener
     * from sending an empty mouse report for this private MSC event. */
    event->sync = false;

    return ZMK_INPUT_PROC_CONTINUE;
}

static const struct zmk_input_processor_driver_api toucan_split_xy_packer_driver_api = {
    .handle_event = toucan_split_xy_packer_handle_event,
};

#define TOUCAN_SPLIT_XY_PACKER_INST(n)                                                            \
    static struct toucan_split_xy_packer_data toucan_split_xy_packer_data_##n;                    \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &toucan_split_xy_packer_data_##n, NULL, POST_KERNEL,     \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                    \
                          &toucan_split_xy_packer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TOUCAN_SPLIT_XY_PACKER_INST)

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#define TRACKPAD_SPLIT_NODE DT_NODELABEL(trackpad_split)

static void toucan_split_xy_unpack_handler(struct input_event *event) {
    if (event->type != INPUT_EV_MSC || event->code != TOUCAN_INPUT_MSC_PACKED_XY) {
        return;
    }

    uint32_t packed = (uint32_t)event->value;
    int16_t x = (int16_t)(packed >> 16);
    int16_t y = (int16_t)(packed & UINT16_MAX);

    int err = input_report(event->dev, INPUT_EV_REL, INPUT_REL_X, x, false, K_NO_WAIT);
    if (err < 0) {
        LOG_WRN("Unable to unpack Toucan split X event (%d)", err);
        return;
    }

    err = input_report(event->dev, INPUT_EV_REL, INPUT_REL_Y, y, true, K_NO_WAIT);
    if (err < 0) {
        LOG_WRN("Unable to unpack Toucan split Y event (%d)", err);
    }
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(TRACKPAD_SPLIT_NODE), toucan_split_xy_unpack_handler);

#endif
