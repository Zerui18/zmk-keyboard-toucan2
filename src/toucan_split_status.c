/*
 * Explicit right-half link and USB-power status for the Toucan2 display.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <zmk/event_manager.h>
#include <zmk/usb.h>

#include "toucan_split_protocol.h"
#include "toucan_split_status.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

ZMK_EVENT_IMPL(toucan_split_status_changed);

static atomic_t right_connected;
static atomic_t right_usb_powered;

bool toucan_split_right_is_connected(void) {
    return atomic_get(&right_connected) != 0;
}

bool toucan_split_right_is_usb_powered(void) {
    return atomic_get(&right_usb_powered) != 0;
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#include <zephyr/bluetooth/conn.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>

#define TRACKPAD_SPLIT_NODE DT_NODELABEL(trackpad_split)

static void split_status_event_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    raise_toucan_split_status_changed((struct toucan_split_status_changed){
        .connected = toucan_split_right_is_connected(),
        .usb_powered = toucan_split_right_is_usb_powered(),
    });
}

K_WORK_DEFINE(split_status_event_work, split_status_event_work_handler);

static void notify_split_status(void) {
    (void)k_work_submit(&split_status_event_work);
}

static bool is_split_connection(struct bt_conn *conn) {
    struct bt_conn_info info;

    return bt_conn_get_info(conn, &info) == 0 && info.type == BT_CONN_TYPE_LE &&
           info.role == BT_CONN_ROLE_CENTRAL;
}

static void split_connection_connected(struct bt_conn *conn, uint8_t err) {
    if (err != 0U || !is_split_connection(conn)) {
        return;
    }

    atomic_set(&right_connected, 1);
    notify_split_status();
}

static void split_connection_disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);

    if (!is_split_connection(conn)) {
        return;
    }

    atomic_set(&right_connected, 0);
    atomic_set(&right_usb_powered, 0);
    notify_split_status();
}

BT_CONN_CB_DEFINE(toucan_split_status_conn_callbacks) = {
    .connected = split_connection_connected,
    .disconnected = split_connection_disconnected,
};

static void right_power_input_handler(struct input_event *event) {
    if (event->type != INPUT_EV_MSC || event->code != TOUCAN_INPUT_MSC_RIGHT_POWER ||
        (event->value != 0 && event->value != 1)) {
        return;
    }

    atomic_set(&right_usb_powered, event->value);
    notify_split_status();
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(TRACKPAD_SPLIT_NODE), right_power_input_handler);

#else /* split peripheral */

#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/peripheral.h>
#include <zmk/split/transport/types.h>
#include <zmk/workqueue.h>

#define TRACKPAD_SPLIT_NODE DT_NODELABEL(trackpad_split)
#define RIGHT_POWER_RELAY_RETRY_COUNT 10
#define RIGHT_POWER_RELAY_RETRY_MS 500
#define RIGHT_POWER_RELAY_CONNECT_DELAY_MS 250

static atomic_t peripheral_link_connected;
static atomic_t relay_retries_remaining;

static void relay_right_power_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (atomic_get(&peripheral_link_connected) == 0) {
        return;
    }

    struct zmk_split_transport_peripheral_event event = {
        .type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_INPUT_EVENT,
        .data = {.input_event = {
                     .reg = DT_REG_ADDR(TRACKPAD_SPLIT_NODE),
                     .sync = false,
                     .type = INPUT_EV_MSC,
                     .code = TOUCAN_INPUT_MSC_RIGHT_POWER,
                     .value = zmk_usb_is_powered() ? 1 : 0,
                 }},
    };

    int err = zmk_split_peripheral_report_event(&event);
    if (err >= 0) {
        atomic_set(&relay_retries_remaining, 0);
        return;
    }

    int retries = atomic_get(&relay_retries_remaining);
    if (retries > 0 && atomic_get(&peripheral_link_connected) != 0) {
        atomic_dec(&relay_retries_remaining);
        (void)k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(),
                                          k_work_delayable_from_work(work),
                                          K_MSEC(RIGHT_POWER_RELAY_RETRY_MS));
    } else if (err != -ENOTCONN) {
        LOG_WRN("Unable to relay Toucan right USB-power status (%d)", err);
    }
}

K_WORK_DELAYABLE_DEFINE(relay_right_power_work, relay_right_power_work_handler);

static void queue_right_power_relay(k_timeout_t delay) {
    atomic_set(&relay_retries_remaining, RIGHT_POWER_RELAY_RETRY_COUNT);
    int err = k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(),
                                          &relay_right_power_work, delay);
    if (err < 0) {
        LOG_WRN("Unable to queue Toucan right USB-power status (%d)", err);
    }
}

static int right_power_source_event_handler(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_changed *split_ev =
        as_zmk_split_peripheral_status_changed(eh);

    if (split_ev != NULL) {
        atomic_set(&peripheral_link_connected, split_ev->connected ? 1 : 0);
        if (split_ev->connected) {
            queue_right_power_relay(K_MSEC(RIGHT_POWER_RELAY_CONNECT_DELAY_MS));
        } else {
            atomic_set(&relay_retries_remaining, 0);
            (void)k_work_cancel_delayable(&relay_right_power_work);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (as_zmk_usb_conn_state_changed(eh) != NULL) {
        queue_right_power_relay(K_NO_WAIT);
        return ZMK_EV_EVENT_BUBBLE;
    }

    return -ENOTSUP;
}

ZMK_LISTENER(toucan_right_power_source, right_power_source_event_handler);
ZMK_SUBSCRIPTION(toucan_right_power_source, zmk_split_peripheral_status_changed);
ZMK_SUBSCRIPTION(toucan_right_power_source, zmk_usb_conn_state_changed);

#endif
