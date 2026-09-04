/*
 * Toucan UF2 entry wrapper. The left display gets time to reveal its UF2 page
 * before reboot; the screenless right half reboots immediately.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_toucan_bootloader

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/reset.h>
#include <zmk/behavior.h>

#include "toucan_bootloader.h"
#include "toucan_display_hooks.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static atomic_t reboot_pending;

__weak uint32_t toucan_display_prepare_uf2(void) {
    return 0U;
}

static void reboot_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    sys_reboot(RST_UF2);
}

K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_handler);

int toucan_bootloader_request(void) {
    if (!atomic_cas(&reboot_pending, 0, 1)) {
        return -EALREADY;
    }

    uint32_t delay_ms = toucan_display_prepare_uf2();
    int result = k_work_reschedule(&reboot_work, K_MSEC(delay_ms));
    if (result < 0) {
        atomic_clear(&reboot_pending);
        return result;
    }

    return 0;
}

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_bootloader_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    int err = toucan_bootloader_request();
    if (err < 0 && err != -EALREADY) {
        LOG_ERR("Unable to queue UF2 reboot (%d)", err);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_bootloader_released(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api bootloader_driver_api = {
    .binding_pressed = on_bootloader_pressed,
    .binding_released = on_bootloader_released,
    .locality = BEHAVIOR_LOCALITY_EVENT_SOURCE,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define BOOTLOADER_INST(n)                                                                         \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &bootloader_driver_api);

DT_INST_FOREACH_STATUS_OKAY(BOOTLOADER_INST)

#endif
