/*
 * On-demand raw battery voltage readout for the Toucan2.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "toucan_battery_estimator.h"
#include "toucan_battery_readout.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define TRACKPAD_SPLIT_NODE DT_NODELABEL(trackpad_split)
#define READOUT_EVENT_MAGIC 0x54440000U
#define EVENT_MAGIC_MASK 0xFFFF0000U
#define EVENT_VOLTAGE_MASK 0x0000FFFFU
#define MIN_VALID_MV 2500U
#define MAX_VALID_MV 4500U

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#include <dt-bindings/zmk/keys.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/usb.h>

#define TYPE_START_DELAY_MS 3000
#define TYPE_RETRY_INTERVAL_MS 500
#define TYPE_TIMEOUT_MS 90000
#define KEY_PRESS_MS 15
#define KEY_GAP_MS 15
#define TYPE_THREAD_STACK_SIZE 1536
#define READOUT_QUEUE_DEPTH 8

enum readout_side {
    READOUT_SIDE_LEFT,
    READOUT_SIDE_RIGHT,
};

struct readout_item {
    uint16_t millivolts;
    uint8_t side;
};

K_MSGQ_DEFINE(readout_queue, sizeof(struct readout_item), READOUT_QUEUE_DEPTH, 4);

static int enqueue_readout(enum readout_side side, uint16_t millivolts) {
    struct readout_item item = {
        .millivolts = millivolts,
        .side = side,
    };

    int err = k_msgq_put(&readout_queue, &item, K_NO_WAIT);
    if (err < 0) {
        LOG_WRN("Dropping Toucan battery readout; output queue is full");
    }

    return err;
}

static bool selected_endpoint_ready(void) {
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();

    switch (endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        return zmk_usb_is_hid_ready();
    case ZMK_TRANSPORT_BLE:
        return zmk_ble_active_profile_is_connected();
    default:
        return false;
    }
}

static uint32_t keycode_for_character(char character) {
    static const uint32_t number_keycodes[] = {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9};

    if (character >= '0' && character <= '9') {
        return number_keycodes[character - '0'];
    }

    switch (character) {
    case 'e':
        return E;
    case 'f':
        return F;
    case 'g':
        return G;
    case 'h':
        return H;
    case 'i':
        return I;
    case 'l':
        return L;
    case 'm':
        return M;
    case 'r':
        return R;
    case 't':
        return T;
    case 'v':
        return V;
    case '=':
        return EQUAL;
    case '\n':
        return ENTER;
    default:
        return 0U;
    }
}

static int type_character(char character) {
    uint32_t keycode = keycode_for_character(character);
    if (keycode == 0U) {
        return -EINVAL;
    }

    int err = raise_zmk_keycode_state_changed_from_encoded(keycode, true, k_uptime_get());
    k_sleep(K_MSEC(KEY_PRESS_MS));

    int release_err =
        raise_zmk_keycode_state_changed_from_encoded(keycode, false, k_uptime_get());
    k_sleep(K_MSEC(KEY_GAP_MS));

    return err < 0 ? err : release_err;
}

static int type_voltage(const char *side, uint16_t millivolts) {
    char line[24];
    int length = snprintf(line, sizeof(line), "%s=%umv\n", side, (unsigned int)millivolts);
    if (length < 0 || length >= (int)sizeof(line)) {
        return -ENOMEM;
    }

    for (int i = 0; i < length; i++) {
        int err = type_character(line[i]);
        if (err < 0) {
            return err;
        }
    }

    return 0;
}

static void battery_readout_input_handler(struct input_event *event) {
    if (event->type != INPUT_EV_MSC || event->code != INPUT_MSC_SCAN) {
        return;
    }

    uint32_t payload = (uint32_t)event->value;
    uint32_t magic = payload & EVENT_MAGIC_MASK;
    if (magic != READOUT_EVENT_MAGIC) {
        return;
    }

    uint16_t millivolts = payload & EVENT_VOLTAGE_MASK;
    if (millivolts < MIN_VALID_MV || millivolts > MAX_VALID_MV) {
        return;
    }

    (void)enqueue_readout(READOUT_SIDE_RIGHT, millivolts);
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(TRACKPAD_SPLIT_NODE), battery_readout_input_handler);

static void battery_readout_type_thread(void *unused_a, void *unused_b, void *unused_c) {
    ARG_UNUSED(unused_a);
    ARG_UNUSED(unused_b);
    ARG_UNUSED(unused_c);

    k_sleep(K_MSEC(TYPE_START_DELAY_MS));

    while (true) {
        struct readout_item item;
        (void)k_msgq_get(&readout_queue, &item, K_FOREVER);

        int64_t deadline = k_uptime_get() + TYPE_TIMEOUT_MS;
        while (!selected_endpoint_ready() && k_uptime_get() < deadline) {
            k_sleep(K_MSEC(TYPE_RETRY_INTERVAL_MS));
        }

        if (!selected_endpoint_ready()) {
            LOG_WRN("Dropping Toucan battery readout; no HID endpoint became ready");
            continue;
        }

        const char *side = item.side == READOUT_SIDE_LEFT ? "left" : "right";
        int err = type_voltage(side, item.millivolts);
        if (err < 0) {
            LOG_WRN("Unable to type %s battery readout (%d)", side, err);
        }
    }
}

K_THREAD_DEFINE(toucan_battery_readout_type_thread, TYPE_THREAD_STACK_SIZE,
                battery_readout_type_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
                0);

static int publish_on_demand_voltage(uint16_t raw_millivolts) {
    return enqueue_readout(READOUT_SIDE_LEFT, raw_millivolts);
}

#else

#include <zmk/split/peripheral.h>
#include <zmk/split/transport/types.h>
#include <zmk/workqueue.h>

static atomic_t pending_readout_millivolts;

static void battery_readout_relay_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    uint16_t raw_millivolts = (uint16_t)atomic_get(&pending_readout_millivolts);

    struct zmk_split_transport_peripheral_event event = {
        .type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_INPUT_EVENT,
        .data = {.input_event = {
                     .reg = DT_REG_ADDR(TRACKPAD_SPLIT_NODE),
                     .sync = false,
                     .type = INPUT_EV_MSC,
                     .code = INPUT_MSC_SCAN,
                     .value = READOUT_EVENT_MAGIC | raw_millivolts,
        }},
    };

    int err = zmk_split_peripheral_report_event(&event);
    if (err < 0) {
        LOG_WRN("Unable to relay Toucan battery readout (%d)", err);
    }
}

K_WORK_DEFINE(battery_readout_relay_work, battery_readout_relay_work_handler);

static int publish_on_demand_voltage(uint16_t raw_millivolts) {
    atomic_set(&pending_readout_millivolts, raw_millivolts);

    int err = k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(),
                                     &battery_readout_relay_work);
    return err < 0 ? err : 0;
}

#endif

int toucan_battery_readout_request(void) {
    uint16_t raw_millivolts;
    int err = toucan_battery_estimator_get_raw_millivolts(&raw_millivolts);

    /* A key press this early is unlikely, but retain a live-read fallback. */
    if (err == -EAGAIN) {
        err = toucan_battery_estimator_read_raw_millivolts(&raw_millivolts);
    }

    if (err < 0) {
        return err;
    }

    return publish_on_demand_voltage(raw_millivolts);
}
