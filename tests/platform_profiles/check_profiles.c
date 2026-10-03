/*
 * Native test of production per-host platform selection and settings.
 * Only transport discovery and storage hardware are replaced with test doubles.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <dt-bindings/zmk/keys.h>
#include <zmk/behavior.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/workqueue.h>

#include "toucan_platform_mode.h"

#define MODE_KEY "toucan_platform/modes"
#define LEGACY_KEY "toucan_platform/windows"
#define MODE_BYTES (2 + ZMK_BLE_PROFILE_COUNT)

/* Reproduce boot ordering: endpoint cached profile 0 before BLE restored profile 3. */
static struct zmk_endpoint_instance selected = {
    .transport = ZMK_TRANSPORT_BLE, .ble = {.profile_index = 0},
};
static uint8_t active_profile = 3;
static const char *test_case;
static uint8_t stored[MODE_BYTES];
static size_t stored_length;
static bool reload_saved;
static bool block_save;
static bool switch_during_query;
static size_t mode_events;
static uint32_t expected_key;
static bool expected_pressed;
static bool awaiting_key;
K_SEM_DEFINE(save_entered, 0, 1);
K_SEM_DEFINE(save_continue, 0, 1);
K_SEM_DEFINE(endpoint_switch_requested, 0, 1);
K_SEM_DEFINE(endpoint_switch_started, 0, 1);
K_SEM_DEFINE(endpoint_switch_done, 0, 1);

struct zmk_endpoint_instance __wrap_zmk_endpoints_selected(void) {
    struct zmk_endpoint_instance snapshot = selected;
    if (switch_during_query) {
        switch_during_query = false;
        k_sem_give(&endpoint_switch_requested);
        assert(k_sem_take(&endpoint_switch_started, K_SECONDS(2)) == 0);
    }
    return snapshot;
}
int zmk_ble_active_profile_index(void) { return active_profile; }

static void switch_endpoint_thread(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    k_sem_take(&endpoint_switch_requested, K_FOREVER);
    active_profile = 2;
    selected = (struct zmk_endpoint_instance){
        .transport = ZMK_TRANSPORT_BLE, .ble = {.profile_index = 2},
    };
    k_sem_give(&endpoint_switch_started);
    raise_zmk_endpoint_changed((struct zmk_endpoint_changed){.endpoint = selected});
    k_sem_give(&endpoint_switch_done);
}

/* Higher priority than the kscan test callback: publish fully, or block on the
 * production mutex, before the interrupted endpoint getter returns its snapshot. */
K_THREAD_DEFINE(endpoint_switch_thread, 2048, switch_endpoint_thread, NULL, NULL, NULL,
                CONFIG_SYSTEM_WORKQUEUE_PRIORITY - 1, 0, 0);

struct stored_value {
    const uint8_t *bytes;
    size_t length;
};

static ssize_t read_value(void *arg, void *out, size_t length) {
    const struct stored_value *value = arg;
    size_t count = MIN(length, value->length);
    memcpy(out, value->bytes, count);
    return count;
}

static void load_value(const struct settings_load_arg *arg, const char *key,
                       const uint8_t *bytes, size_t length) {
    struct stored_value value = {.bytes = bytes, .length = length};
    assert(settings_call_set_handler(key, length, read_value, &value, arg) == 0);
}

static int load_settings(struct settings_store *store, const struct settings_load_arg *arg) {
    ARG_UNUSED(store);
    if (reload_saved) {
        assert(stored_length == sizeof(stored));
        load_value(arg, MODE_KEY, stored, stored_length);
        return 0;
    }
    uint8_t legacy = 1;
    const uint8_t modes[] = {1, 0, 0, 0, 0, 1, 0};
    if (strcmp(test_case, "legacy") == 0) {
        load_value(arg, LEGACY_KEY, &legacy, sizeof(legacy));
    } else if (strcmp(test_case, "shorter") == 0) {
        /* An earlier build knew only USB and BLE 0; newly added profiles use the fallback. */
        load_value(arg, MODE_KEY, modes, 3);
        load_value(arg, LEGACY_KEY, &legacy, sizeof(legacy));
    } else {
        legacy = 0;
        if (strcmp(test_case, "legacy-first") == 0) {
            load_value(arg, LEGACY_KEY, &legacy, sizeof(legacy));
            load_value(arg, MODE_KEY, modes, sizeof(modes));
        } else {
            assert(strcmp(test_case, "new-first") == 0);
            load_value(arg, MODE_KEY, modes, sizeof(modes));
            load_value(arg, LEGACY_KEY, &legacy, sizeof(legacy));
        }
    }
    return 0;
}

static int save_settings(struct settings_store *store, const char *key,
                         const char *value, size_t length) {
    ARG_UNUSED(store);
    assert(strcmp(key, MODE_KEY) == 0);
    assert(length == sizeof(stored));
    if (block_save) {
        block_save = false;
        k_sem_give(&save_entered);
        assert(k_sem_take(&save_continue, K_SECONDS(2)) == 0);
    }
    memcpy(stored, value, length);
    stored_length = length;
    return 0;
}

static const struct settings_store_itf storage_api = {
    .csi_load = load_settings,
    .csi_save = save_settings,
};
static struct settings_store storage = {.cs_itf = &storage_api};

static int init_test_storage(void) {
    test_case = getenv("TOUCAN_TEST_CASE");
    assert(test_case != NULL);
    assert(settings_subsys_init() == 0);
    settings_src_register(&storage);
    settings_dst_register(&storage);
    return 0;
}
SYS_INIT(init_test_storage, APPLICATION, 80);

static void select_host(int profile) {
    if (profile < 0) {
        selected = (struct zmk_endpoint_instance){.transport = ZMK_TRANSPORT_USB};
    } else {
        active_profile = profile;
        selected = (struct zmk_endpoint_instance){
            .transport = ZMK_TRANSPORT_BLE, .ble = {.profile_index = profile},
        };
    }
    raise_zmk_endpoint_changed((struct zmk_endpoint_changed){.endpoint = selected});
}

static void invoke(const char *behavior, uint32_t param, uint32_t position, bool pressed,
                   uint32_t output) {
    struct zmk_behavior_binding binding = {.behavior_dev = behavior, .param1 = param};
    struct zmk_behavior_binding_event event = {
        .position = position, .timestamp = k_uptime_get(),
    };
    expected_key = output;
    expected_pressed = pressed;
    awaiting_key = output != 0;
    assert(zmk_behavior_invoke_binding(&binding, event, pressed) == 0);
    assert(!awaiting_key);
}

static void toggle(void) {
    invoke(DEVICE_DT_NAME(DT_NODELABEL(platform_mode)), 0, 2, true, 0);
    invoke(DEVICE_DT_NAME(DT_NODELABEL(platform_mode)), 0, 2, false, 0);
}

static void assert_modes(const uint8_t modes[1 + ZMK_BLE_PROFILE_COUNT]) {
    for (int slot = 0; slot <= ZMK_BLE_PROFILE_COUNT; slot++) {
        select_host(slot - 1);
        assert(toucan_platform_is_windows() == (modes[slot] != 0));
    }
}

static void run_checks(void) {
    assert(toucan_platform_is_windows()); /* Loaded BLE 3, not the stale cached BLE 0. */
    if (strcmp(test_case, "legacy") == 0) {
        assert_modes((uint8_t[]){1, 1, 1, 1, 1, 1});
    } else if (strcmp(test_case, "shorter") == 0) {
        assert_modes((uint8_t[]){0, 0, 1, 1, 1, 1});
    } else {
        assert_modes((uint8_t[]){0, 0, 0, 0, 1, 0});
    }

    const uint8_t empty[] = {1, 0, 0, 0, 0, 0, 0};
    assert(settings_runtime_set(MODE_KEY, empty, sizeof(empty)) == 0);
    assert(settings_runtime_commit("toucan_platform") == 0);
    assert_modes(&empty[1]);
    size_t events = mode_events;
    select_host(1);
    select_host(1); /* Duplicate events and same-mode host changes need no UI notification. */
    select_host(-1);
    assert(mode_events == events);

    /* Keep the first write in flight while other hosts are toggled and work coalesces. */
    block_save = true;
    select_host(0);
    toggle();
    assert(toucan_platform_is_windows());
    assert(k_sem_take(&save_entered, K_SECONDS(2)) == 0);
    select_host(1);
    assert(!toucan_platform_is_windows());
    toggle();
    select_host(-1);
    assert(!toucan_platform_is_windows());
    toggle();
    active_profile = 4; /* A BLE profile change while output remains USB must not affect USB. */
    assert(toucan_platform_is_windows());
    k_sem_give(&save_continue);
    assert(k_work_queue_drain(zmk_workqueue_lowprio_work_q(), false) >= 0);
    const uint8_t saved[] = {1, 1, 1, 1, 0, 0, 0};
    assert(stored_length == sizeof(saved) && memcmp(stored, saved, sizeof(saved)) == 0);
    assert_modes(&saved[1]);

    /* Real storage load/commit path restores exactly what the deferred writer saved. */
    assert(settings_runtime_set(MODE_KEY, empty, sizeof(empty)) == 0);
    assert(settings_runtime_commit("toucan_platform") == 0);
    assert_modes(&empty[1]);
    reload_saved = true;
    selected = (struct zmk_endpoint_instance){
        .transport = ZMK_TRANSPORT_BLE, .ble = {.profile_index = 4},
    };
    active_profile = 1;
    assert(settings_load_subtree("toucan_platform") == 0);
    assert(toucan_platform_is_windows()); /* Again ignore the stale endpoint index at load. */
    assert_modes(&saved[1]);

    /* A malformed value must not poison the table or overwrite explicit macOS choices. */
    uint8_t bad[sizeof(saved)];
    memcpy(bad, saved, sizeof(bad));
    bad[0] = 2;
    assert(settings_runtime_set(MODE_KEY, bad, sizeof(bad)) == -EINVAL);
    bad[0] = 1;
    bad[1] = 2;
    assert(settings_runtime_set(MODE_KEY, bad, sizeof(bad)) == -EINVAL);
    assert(settings_runtime_set(MODE_KEY, saved, 1) == -EINVAL);
    assert(settings_runtime_set(MODE_KEY "/extra", saved, sizeof(saved)) == -ENOENT);
    const uint8_t legacy = 1;
    assert(settings_runtime_set(LEGACY_KEY, &legacy, sizeof(legacy)) == 0);
    assert(settings_runtime_commit("toucan_platform") == 0);
    assert_modes(&saved[1]);

    select_host(-1);
    events = mode_events;
    selected = (struct zmk_endpoint_instance){
        .transport = ZMK_TRANSPORT_BLE, .ble = {.profile_index = -1},
    };
    raise_zmk_endpoint_changed((struct zmk_endpoint_changed){.endpoint = selected});
    selected.ble.profile_index = ZMK_BLE_PROFILE_COUNT;
    raise_zmk_endpoint_changed((struct zmk_endpoint_changed){.endpoint = selected});
    assert(toucan_platform_is_windows() && mode_events == events);
    select_host(-1);

    /* A boot/settings refresh must not overwrite a newer endpoint event. */
    switch_during_query = true; /* USB/Windows -> BLE 2/macOS during the getter. */
    assert(settings_runtime_commit("toucan_platform") == 0);
    assert(k_sem_take(&endpoint_switch_done, K_SECONDS(2)) == 0);
    assert(!toucan_platform_is_windows());
    events = mode_events;
    raise_zmk_endpoint_changed((struct zmk_endpoint_changed){
        .endpoint = {.transport = ZMK_TRANSPORT_USB},
    }); /* A delayed USB notification must not overwrite the live BLE choice. */
    assert(!toucan_platform_is_windows() && mode_events == events);
    toggle();
    assert(k_work_queue_drain(zmk_workqueue_lowprio_work_q(), false) >= 0);
    const uint8_t changed_ble2[] = {1, 1, 1, 1, 1, 0, 0};
    assert(memcmp(stored, changed_ble2, sizeof(changed_ble2)) == 0);
    toggle();
    assert(k_work_queue_drain(zmk_workqueue_lowprio_work_q(), false) >= 0);
    assert(memcmp(stored, saved, sizeof(saved)) == 0);

    /* Future presses follow the selected host; releases retain the original chord. */
    select_host(2); /* macOS */
    invoke(DEVICE_DT_NAME(DT_NODELABEL(cmd_key)), LS(Z), 1, true, LG(LS(Z)));
    select_host(0); /* Windows */
    invoke(DEVICE_DT_NAME(DT_NODELABEL(cmd_key)), LS(Z), 1, false, LG(LS(Z)));
    invoke(DEVICE_DT_NAME(DT_NODELABEL(cmd_key)), LS(Z), 1, true, LC(LS(Z)));
    select_host(2);
    invoke(DEVICE_DT_NAME(DT_NODELABEL(cmd_key)), LS(Z), 1, false, LC(LS(Z)));
    invoke(DEVICE_DT_NAME(DT_NODELABEL(app_switch)), 1, 3, true, LGUI);
    select_host(0);
    invoke(DEVICE_DT_NAME(DT_NODELABEL(app_switch)), 1, 3, false, LGUI);
    invoke(DEVICE_DT_NAME(DT_NODELABEL(app_switch)), 1, 3, true, LALT);
    select_host(2);
    invoke(DEVICE_DT_NAME(DT_NODELABEL(app_switch)), 1, 3, false, LALT);
    printf("PASS: per-host platform modes, migration, persistence, coalesced saves, "
           "and held releases (%s)\n", test_case);
}

static int key_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *event = as_zmk_keycode_state_changed(eh);
    if (event->usage_page == HID_USAGE_KEY && event->keycode == ZMK_HID_USAGE_ID(F12)) {
        if (event->state) {
            run_checks();
        }
        return ZMK_EV_EVENT_BUBBLE;
    }
    assert(awaiting_key);
    assert(event->usage_page == ZMK_HID_USAGE_PAGE(expected_key));
    assert(event->keycode == ZMK_HID_USAGE_ID(expected_key));
    assert(event->implicit_modifiers == SELECT_MODS(expected_key));
    assert(event->explicit_modifiers == 0U);
    assert(event->state == expected_pressed);
    awaiting_key = false;
    return ZMK_EV_EVENT_BUBBLE;
}

static int mode_listener(const zmk_event_t *eh) {
    const struct toucan_platform_mode_changed *event = as_toucan_platform_mode_changed(eh);
    assert(event->windows == toucan_platform_is_windows());
    mode_events++;
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(toucan_test_profile_keys, key_listener);
ZMK_SUBSCRIPTION(toucan_test_profile_keys, zmk_keycode_state_changed);
ZMK_LISTENER(toucan_test_profile_modes, mode_listener);
ZMK_SUBSCRIPTION(toucan_test_profile_modes, toucan_platform_mode_changed);
