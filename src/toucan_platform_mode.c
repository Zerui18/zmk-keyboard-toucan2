/*
 * Persistent macOS/Windows shortcut mapping for the Toucan2.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/workqueue.h>

#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/ble.h>
#endif

#include "toucan_platform_mode.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define SETTINGS_KEY "toucan_platform/modes"
#define SETTINGS_VERSION 1U
#define ACTIVE_PLATFORM_POSITIONS 64U

static atomic_t windows_mode;
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#if IS_ENABLED(CONFIG_ZMK_BLE)
#define PLATFORM_BLE_PROFILE_COUNT ZMK_BLE_PROFILE_COUNT
#else
#define PLATFORM_BLE_PROFILE_COUNT 0
#endif
#define PLATFORM_MODE_COUNT (1 + PLATFORM_BLE_PROFILE_COUNT)

/* Stable on-disk order: version, USB, then BLE slots 0..N. All fields are bytes. */
struct persisted_platform_modes {
    uint8_t version;
    uint8_t windows[PLATFORM_MODE_COUNT];
};

static struct persisted_platform_modes platform_modes = {.version = SETTINGS_VERSION};
static size_t loaded_mode_count;
static uint8_t legacy_windows_mode;
static int selected_mode_slot;
K_MUTEX_DEFINE(platform_mode_mutex);

static uint32_t active_command_keycodes[ACTIVE_PLATFORM_POSITIONS];
static bool active_command_valid[ACTIVE_PLATFORM_POSITIONS];
static uint32_t active_app_switch_modifiers[ACTIVE_PLATFORM_POSITIONS];
static bool active_app_switch_valid[ACTIVE_PLATFORM_POSITIONS];
#endif

ZMK_EVENT_IMPL(toucan_platform_mode_changed);

bool toucan_platform_is_windows(void) {
    return atomic_get(&windows_mode) != 0;
}

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static int endpoint_mode_slot(struct zmk_endpoint_instance endpoint) {
    switch (endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        return 0;
#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_TRANSPORT_BLE:
        if (endpoint.ble.profile_index >= 0 &&
            endpoint.ble.profile_index < PLATFORM_BLE_PROFILE_COUNT) {
            return 1 + endpoint.ble.profile_index;
        }
        break;
#endif
    default:
        break;
    }
    return -EINVAL;
}

static void publish_platform_mode_locked(void) {
    bool next_mode = platform_modes.windows[selected_mode_slot] != 0U;
    if (atomic_set(&windows_mode, next_mode) != next_mode) {
        /* The getter is atomic, so synchronous listeners can query it without the mutex.
         * Keep publication serialized with endpoint changes and preference toggles. */
        raise_toucan_platform_mode_changed(
            (struct toucan_platform_mode_changed){.windows = next_mode});
    }
}

static uint32_t resolve_command_keycode(uint32_t requested_keycode) {
    if (requested_keycode == 0U || requested_keycode == LGUI) {
        return toucan_platform_is_windows() ? LCTRL : LGUI;
    }

    if (requested_keycode == RGUI) {
        return toucan_platform_is_windows() ? RCTRL : RGUI;
    }

    uint32_t command_modifier = toucan_platform_is_windows() ? MOD_LCTL : MOD_LGUI;
    /* APPLY_MODS does not parenthesize its modifier argument before shifting it. */
    return APPLY_MODS((SELECT_MODS(requested_keycode) | command_modifier),
                      STRIP_MODS(requested_keycode));
}

static uint32_t resolve_app_switch_modifier(void) {
    return toucan_platform_is_windows() ? LALT : LGUI;
}

static void save_platform_mode_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    struct persisted_platform_modes saved;
    k_mutex_lock(&platform_mode_mutex, K_FOREVER);
    saved = platform_modes;
    k_mutex_unlock(&platform_mode_mutex);

    /* Save the whole table, never the endpoint that happens to be active when work runs.
     * Coalesced toggles on different profiles must all survive a restart. */
    int err = settings_save_one(SETTINGS_KEY, &saved, sizeof(saved));
    if (err < 0) {
        LOG_WRN("Unable to persist Toucan platform modes (%d)", err);
    }
}

K_WORK_DEFINE(save_platform_mode_work, save_platform_mode_work_handler);

static int platform_mode_settings_set(const char *name, size_t length,
                                      settings_read_cb read_cb, void *cb_arg) {
    const char *next;

    if (settings_name_steq(name, "windows", &next) && next == NULL) {
        /* Legacy global mode is a fallback only; load order must not override new choices. */
        if (length != sizeof(uint8_t)) {
            return -EINVAL;
        }
        uint8_t saved_mode;
        int err = read_cb(cb_arg, &saved_mode, sizeof(saved_mode));
        if (err < 0) {
            return err;
        }
        if (err != (int)sizeof(saved_mode) || saved_mode > 1U) {
            return -EINVAL;
        }
        k_mutex_lock(&platform_mode_mutex, K_FOREVER);
        legacy_windows_mode = saved_mode;
        k_mutex_unlock(&platform_mode_mutex);
        return 0;
    }

    if (!settings_name_steq(name, "modes", &next) || next != NULL) {
        return -ENOENT;
    }
    /* BLE profile IDs are uint8_t. Accept a valid prefix when firmware adds/removes slots. */
    if (length < 2U || length > 2U + UINT8_MAX + 1U) {
        return -EINVAL;
    }

    struct persisted_platform_modes saved;
    size_t read_size = MIN(length, sizeof(saved));
    int err = read_cb(cb_arg, &saved, read_size);
    if (err < 0) {
        return err;
    }
    if (err != (int)read_size || saved.version != SETTINGS_VERSION) {
        return -EINVAL;
    }
    size_t count = read_size - sizeof(saved.version);
    for (size_t i = 0; i < count; i++) {
        if (saved.windows[i] > 1U) {
            return -EINVAL;
        }
    }

    k_mutex_lock(&platform_mode_mutex, K_FOREVER);
    memcpy(platform_modes.windows, saved.windows, count);
    loaded_mode_count = count;
    k_mutex_unlock(&platform_mode_mutex);
    return 0;
}

static int platform_mode_settings_commit(void) {
    /* Serialize the endpoint query too: a newer endpoint event must not be
     * overwritten by a snapshot taken before acquiring this mutex. */
    k_mutex_lock(&platform_mode_mutex, K_FOREVER);
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (endpoint.transport == ZMK_TRANSPORT_BLE) {
        /* Endpoint initialization precedes settings loading. BLE restores its selected
         * profile without emitting a change event, so its loaded index is authoritative. */
        endpoint.ble.profile_index = zmk_ble_active_profile_index();
    }
#endif
    int slot = endpoint_mode_slot(endpoint);

    for (size_t i = loaded_mode_count; i < PLATFORM_MODE_COUNT; i++) {
        platform_modes.windows[i] = legacy_windows_mode;
    }
    loaded_mode_count = PLATFORM_MODE_COUNT;
    if (slot >= 0) {
        selected_mode_slot = slot;
    }
    publish_platform_mode_locked();
    k_mutex_unlock(&platform_mode_mutex);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(toucan_platform, "toucan_platform", NULL,
                               platform_mode_settings_set, platform_mode_settings_commit, NULL);

static int platform_endpoint_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    k_mutex_lock(&platform_mode_mutex, K_FOREVER);
    /* A concurrently dispatched event may carry an older endpoint than the live one. */
    int slot = endpoint_mode_slot(zmk_endpoints_selected());
    if (slot >= 0) {
        selected_mode_slot = slot;
        publish_platform_mode_locked();
    }
    k_mutex_unlock(&platform_mode_mutex);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(toucan_platform_endpoint, platform_endpoint_listener);
ZMK_SUBSCRIPTION(toucan_platform_endpoint, zmk_endpoint_changed);
#endif

#define DT_DRV_COMPAT zmk_behavior_toucan_command_key

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_command_key_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint32_t encoded_keycode = resolve_command_keycode(binding->param1);

    if (event.position < ACTIVE_PLATFORM_POSITIONS) {
        active_command_keycodes[event.position] = encoded_keycode;
        active_command_valid[event.position] = true;
    }

    return raise_zmk_keycode_state_changed_from_encoded(encoded_keycode, true,
                                                        event.timestamp);
#else
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
#endif
}

static int on_command_key_released(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint32_t encoded_keycode = resolve_command_keycode(binding->param1);

    if (event.position < ACTIVE_PLATFORM_POSITIONS &&
        active_command_valid[event.position]) {
        encoded_keycode = active_command_keycodes[event.position];
        active_command_valid[event.position] = false;
    }

    return raise_zmk_keycode_state_changed_from_encoded(encoded_keycode, false,
                                                        event.timestamp);
#else
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
#endif
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata command_key_param_values[] = {
    {
        .display_name = "Key",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_HID_USAGE,
    },
};

static const struct behavior_parameter_metadata_set command_key_metadata_set[] = {{
    .param1_values = command_key_param_values,
    .param1_values_len = ARRAY_SIZE(command_key_param_values),
}};

static const struct behavior_parameter_metadata command_key_metadata = {
    .sets = command_key_metadata_set,
    .sets_len = ARRAY_SIZE(command_key_metadata_set),
};
#endif

static const struct behavior_driver_api command_key_driver_api = {
    .binding_pressed = on_command_key_pressed,
    .binding_released = on_command_key_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &command_key_metadata,
#endif
};

#define COMMAND_KEY_INST(n)                                                                    \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                            \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &command_key_driver_api);

DT_INST_FOREACH_STATUS_OKAY(COMMAND_KEY_INST)

#endif

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT zmk_behavior_toucan_app_switch_layer

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_app_switch_layer_pressed(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint32_t modifier = resolve_app_switch_modifier();

    if (event.position < ACTIVE_PLATFORM_POSITIONS) {
        active_app_switch_modifiers[event.position] = modifier;
        active_app_switch_valid[event.position] = true;
    }

    int err =
        raise_zmk_keycode_state_changed_from_encoded(modifier, true, event.timestamp);
    if (err < 0) {
        if (event.position < ACTIVE_PLATFORM_POSITIONS) {
            active_app_switch_valid[event.position] = false;
        }
        return err;
    }

    err = zmk_keymap_layer_activate(binding->param1);
    if (err < 0) {
        (void)raise_zmk_keycode_state_changed_from_encoded(modifier, false, event.timestamp);
        if (event.position < ACTIVE_PLATFORM_POSITIONS) {
            active_app_switch_valid[event.position] = false;
        }
    }

    return err;
#else
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
#endif
}

static int on_app_switch_layer_released(struct zmk_behavior_binding *binding,
                                        struct zmk_behavior_binding_event event) {
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint32_t modifier = resolve_app_switch_modifier();

    if (event.position < ACTIVE_PLATFORM_POSITIONS &&
        active_app_switch_valid[event.position]) {
        modifier = active_app_switch_modifiers[event.position];
        active_app_switch_valid[event.position] = false;
    }

    int layer_err = zmk_keymap_layer_deactivate(binding->param1);
    int modifier_err =
        raise_zmk_keycode_state_changed_from_encoded(modifier, false, event.timestamp);
    return layer_err < 0 ? layer_err : modifier_err;
#else
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
#endif
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata app_switch_layer_param_values[] = {
    {
        .display_name = "Layer",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_LAYER_ID,
    },
};

static const struct behavior_parameter_metadata_set app_switch_layer_metadata_set[] = {{
    .param1_values = app_switch_layer_param_values,
    .param1_values_len = ARRAY_SIZE(app_switch_layer_param_values),
}};

static const struct behavior_parameter_metadata app_switch_layer_metadata = {
    .sets = app_switch_layer_metadata_set,
    .sets_len = ARRAY_SIZE(app_switch_layer_metadata_set),
};
#endif

static const struct behavior_driver_api app_switch_layer_driver_api = {
    .binding_pressed = on_app_switch_layer_pressed,
    .binding_released = on_app_switch_layer_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &app_switch_layer_metadata,
#endif
};

#define APP_SWITCH_LAYER_INST(n)                                                               \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                            \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &app_switch_layer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(APP_SWITCH_LAYER_INST)

#endif

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT zmk_behavior_toucan_platform_toggle

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_platform_toggle_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    k_mutex_lock(&platform_mode_mutex, K_FOREVER);
    platform_modes.windows[selected_mode_slot] ^= 1U;
    bool next_mode = platform_modes.windows[selected_mode_slot] != 0U;
    publish_platform_mode_locked();
    k_mutex_unlock(&platform_mode_mutex);

    int err = k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(),
                                     &save_platform_mode_work);
    if (err < 0) {
        LOG_WRN("Unable to queue Toucan platform mode save (%d)", err);
    }

    LOG_INF("Toucan shortcut mode: %s", next_mode ? "Windows" : "macOS");
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_platform_toggle_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api platform_toggle_driver_api = {
    .binding_pressed = on_platform_toggle_pressed,
    .binding_released = on_platform_toggle_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define PLATFORM_TOGGLE_INST(n)                                                                \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                            \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &platform_toggle_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PLATFORM_TOGGLE_INST)

#endif
