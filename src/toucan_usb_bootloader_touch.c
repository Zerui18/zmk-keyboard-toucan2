/*
 * Copyright (c) 2026 Zerui Chen
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include <dt-bindings/zmk/reset.h>

#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

LOG_MODULE_REGISTER(toucan_usb_bootloader_touch, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(DT_HAS_CHOSEN(zmk_studio_rpc_uart),
             "TOUCAN_USB_BOOTLOADER_TOUCH requires zmk,studio-rpc-uart");

#define TOUCH_UART_NODE DT_CHOSEN(zmk_studio_rpc_uart)
#define TOUCH_FIRST_BAUD_RATE 1200U
#define TOUCH_SECOND_BAUD_RATE 2400U
#define TOUCH_POLL_INTERVAL K_MSEC(20)
#define TOUCH_ARM_TIMEOUT_MS 2000

enum touch_state {
    TOUCH_IDLE,
    TOUCH_FIRST_DTR_HIGH,
    TOUCH_FIRST_DTR_LOW,
    TOUCH_SECOND_DTR_HIGH,
    TOUCH_BLOCKED,
};

static const struct device *const touch_uart = DEVICE_DT_GET(TOUCH_UART_NODE);
static struct k_work_delayable touch_poll_work;
static enum touch_state touch_state;
static bool touch_initialized;
static int64_t touch_started_at;

static void schedule_next_poll(void) {
    k_work_reschedule(&touch_poll_work, TOUCH_POLL_INTERVAL);
}

static void reset_touch(void) {
    touch_state = TOUCH_IDLE;
    touch_started_at = 0;
}

static void block_touch(void) {
    touch_state = TOUCH_BLOCKED;
}

static void touch_poll(struct k_work *work) {
    uint32_t baud_rate;
    uint32_t dtr;
    int64_t now;

    ARG_UNUSED(work);

    if (zmk_usb_get_conn_state() == ZMK_USB_CONN_NONE) {
        reset_touch();
        return;
    }

    if (!device_is_ready(touch_uart) ||
        uart_line_ctrl_get(touch_uart, UART_LINE_CTRL_BAUD_RATE, &baud_rate) < 0 ||
        uart_line_ctrl_get(touch_uart, UART_LINE_CTRL_DTR, &dtr) < 0) {
        reset_touch();
        schedule_next_poll();
        return;
    }

    now = k_uptime_get();

    if (touch_state != TOUCH_IDLE && touch_state != TOUCH_BLOCKED &&
        now - touch_started_at > TOUCH_ARM_TIMEOUT_MS) {
        block_touch();
    }

    switch (touch_state) {
    case TOUCH_IDLE:
        if (baud_rate == TOUCH_FIRST_BAUD_RATE && dtr != 0U) {
            touch_state = TOUCH_FIRST_DTR_HIGH;
            touch_started_at = now;
        }
        break;

    case TOUCH_FIRST_DTR_HIGH:
        if (baud_rate == TOUCH_FIRST_BAUD_RATE && dtr == 0U) {
            touch_state = TOUCH_FIRST_DTR_LOW;
        } else if (baud_rate != TOUCH_FIRST_BAUD_RATE) {
            block_touch();
        }
        break;

    case TOUCH_FIRST_DTR_LOW:
        if (baud_rate == TOUCH_SECOND_BAUD_RATE && dtr != 0U) {
            touch_state = TOUCH_SECOND_DTR_HIGH;
        } else if (dtr != 0U || (baud_rate != TOUCH_FIRST_BAUD_RATE &&
                                baud_rate != TOUCH_SECOND_BAUD_RATE)) {
            block_touch();
        }
        break;

    case TOUCH_SECOND_DTR_HIGH:
        if (baud_rate == TOUCH_SECOND_BAUD_RATE && dtr == 0U) {
            LOG_INF("guarded 1200/2400-baud touch received; entering UF2 bootloader");
            sys_reboot(RST_UF2);
        } else if (baud_rate != TOUCH_SECOND_BAUD_RATE) {
            block_touch();
        }
        break;

    case TOUCH_BLOCKED:
        /* A timed-out or malformed attempt must release DTR before retrying. */
        if (dtr == 0U) {
            reset_touch();
        }
        break;
    }

    schedule_next_poll();
}

static int toucan_usb_conn_listener(const zmk_event_t *event) {
    const struct zmk_usb_conn_state_changed *changed = as_zmk_usb_conn_state_changed(event);

    if (changed == NULL || !touch_initialized) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    reset_touch();
    if (changed->conn_state == ZMK_USB_CONN_NONE) {
        k_work_cancel_delayable(&touch_poll_work);
    } else {
        k_work_reschedule(&touch_poll_work, K_NO_WAIT);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(toucan_usb_bootloader_touch, toucan_usb_conn_listener);
ZMK_SUBSCRIPTION(toucan_usb_bootloader_touch, zmk_usb_conn_state_changed);

static int toucan_usb_bootloader_touch_init(void) {
    k_work_init_delayable(&touch_poll_work, touch_poll);
    touch_initialized = true;
    k_work_schedule(&touch_poll_work, K_MSEC(250));
    return 0;
}

SYS_INIT(toucan_usb_bootloader_touch_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
