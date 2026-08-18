/*
 * Global on-demand battery voltage readout behavior for the Toucan2.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_toucan_battery_readout

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include "toucan_battery_readout.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_battery_readout_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    int err = toucan_battery_readout_request();
    if (err < 0) {
        LOG_ERR("Unable to queue Toucan battery readout (%d)", err);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_battery_readout_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api battery_readout_driver_api = {
    .binding_pressed = on_battery_readout_pressed,
    .binding_released = on_battery_readout_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define BATTERY_READOUT_INST(n)                                                                    \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &battery_readout_driver_api);

DT_INST_FOREACH_STATUS_OKAY(BATTERY_READOUT_INST)

#endif
