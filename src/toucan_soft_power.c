/*
 * Per-half soft power for the Toucan2.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>

#include <hal/nrf_gpio.h>
#include <hal/nrf_power.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/poweroff.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/pm.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_SHIELD_TOUCAN_RIGHT)
/* Public API supplied by the West-managed zmk_driver_azoteq module. */
extern int tps43_set_sleep(const struct device *dev, bool sleep);
#endif

/* GPREGRET[0] belongs to the XIAO bootloader; keep our marker in GPREGRET[1]. */
#define TOUCAN_SOFT_POWER_MARKER 0xA7U
#define TOUCAN_SOFT_POWER_GPRET_INDEX 1U
#define TOUCAN_SOFT_POWER_WAKE_GPIO_PIN NRF_GPIO_PIN_MAP(0, 29)

#define POSITION_BIT(position) BIT64(position)
#define MATRIX_BIT(row, column) BIT(((row) * 6) + (column))

#define KSCAN_NODE DT_CHOSEN(zmk_kscan)
#define SOFT_POWER_WAKEUP_NODE DT_NODELABEL(toucan_soft_power_wakeup)

static const struct device *const soft_power_wakeup =
    DEVICE_DT_GET(SOFT_POWER_WAKEUP_NODE);

static const struct gpio_dt_spec matrix_rows[] = {
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN_NODE, row_gpios, 0),
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN_NODE, row_gpios, 1),
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN_NODE, row_gpios, 2),
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN_NODE, row_gpios, 3),
};

static const struct gpio_dt_spec matrix_columns[] = {
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN_NODE, col_gpios, 0),
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN_NODE, col_gpios, 1),
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN_NODE, col_gpios, 2),
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN_NODE, col_gpios, 3),
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN_NODE, col_gpios, 4),
    GPIO_DT_SPEC_GET_BY_IDX(KSCAN_NODE, col_gpios, 5),
};

/* Physical positions in the stock 42-key layout. */
#if IS_ENABLED(CONFIG_SHIELD_TOUCAN_LEFT)
#define TOUCAN_POWER_FINAL_POSITION 37U /* &mo 1 */
#define TOUCAN_POWER_TARGET_MASK                                                               \
    (POSITION_BIT(13) | POSITION_BIT(26) | POSITION_BIT(15) | POSITION_BIT(28) |              \
     POSITION_BIT(TOUCAN_POWER_FINAL_POSITION)) /* A X D V + NAV thumb */
#define TOUCAN_POWER_MATRIX_MASK                                                               \
    (MATRIX_BIT(1, 1) | MATRIX_BIT(2, 2) | MATRIX_BIT(1, 3) | MATRIX_BIT(2, 4) |               \
     MATRIX_BIT(3, 4))
#elif IS_ENABLED(CONFIG_SHIELD_TOUCAN_RIGHT)
#define TOUCAN_POWER_FINAL_POSITION 40U /* &mo 2, the mirrored thumb */
#define TOUCAN_POWER_TARGET_MASK                                                               \
    (POSITION_BIT(22) | POSITION_BIT(33) | POSITION_BIT(20) | POSITION_BIT(31) |              \
     POSITION_BIT(TOUCAN_POWER_FINAL_POSITION)) /* ; . K M + SYM thumb */
#define TOUCAN_POWER_MATRIX_MASK                                                               \
    (MATRIX_BIT(1, 4) | MATRIX_BIT(2, 3) | MATRIX_BIT(1, 2) | MATRIX_BIT(2, 1) |               \
     MATRIX_BIT(3, 1))
#else
#error "Toucan soft power requires a Toucan left or right shield"
#endif

#define TOUCAN_POWER_FINAL_BIT POSITION_BIT(TOUCAN_POWER_FINAL_POSITION)
#define TOUCAN_POWER_ENTRY_MASK (TOUCAN_POWER_TARGET_MASK & ~TOUCAN_POWER_FINAL_BIT)

BUILD_ASSERT(CONFIG_TOUCAN_SOFT_POWER_WAKE_STABLE_MS <=
                 CONFIG_TOUCAN_SOFT_POWER_WAKE_VALIDATION_MS,
             "Wake stable time must fit inside the validation interval");

static uint64_t pressed_positions;
static uint64_t wake_suppression;
static uint64_t captured_positions;

enum entry_state {
    ENTRY_IDLE,
    ENTRY_FIRST_TAP_DOWN,
    ENTRY_TAP_PRIMED,
    ENTRY_CAPTURING,
};

static enum entry_state entry_state;
static int64_t first_tap_pressed_at;
static int64_t first_tap_released_at;
static int64_t capture_started_at;
static int64_t chord_started_at;
static bool power_off_armed;
static bool power_off_pending;

static void power_off_work_handler(struct k_work *work);
static void clear_wake_suppression_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(power_off_work, power_off_work_handler);
K_WORK_DELAYABLE_DEFINE(clear_wake_suppression_work, clear_wake_suppression_work_handler);

static void clear_soft_power_marker(void) {
    nrf_power_gpregret_set(NRF_POWER, TOUCAN_SOFT_POWER_GPRET_INDEX, 0U);
}

static void clear_wake_gpio_latch(void) {
    /* GPIO LATCH is retained across System OFF wakes on nRF52840. */
    nrf_gpio_pin_latch_clear(TOUCAN_SOFT_POWER_WAKE_GPIO_PIN);
}

static void prepare_soft_power_marker(void) {
    /* Make the next System OFF wake reason unambiguous. */
    nrf_power_resetreas_clear(NRF_POWER, UINT32_MAX);
    nrf_power_gpregret_set(NRF_POWER, TOUCAN_SOFT_POWER_GPRET_INDEX,
                           TOUCAN_SOFT_POWER_MARKER);
}

static bool suspend_local_peripheral(void) {
#if IS_ENABLED(CONFIG_SHIELD_TOUCAN_LEFT)
    const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display)) {
        LOG_WRN("Display is not ready for soft power off");
        return false;
    }

    int err = display_blanking_on(display);
    if (err < 0) {
        LOG_WRN("Unable to blank display for soft power off (%d)", err);
        return false;
    }
#elif IS_ENABLED(CONFIG_SHIELD_TOUCAN_RIGHT)
    const struct device *trackpad = DEVICE_DT_GET(DT_NODELABEL(tps43_trackpad));
    if (!device_is_ready(trackpad)) {
        LOG_WRN("Trackpad is not ready for soft power off");
        return false;
    }

    int err = tps43_set_sleep(trackpad, true);
    if (err < 0) {
        LOG_WRN("Unable to suspend trackpad for soft power off (%d)", err);
        return false;
    }
#endif

    return true;
}

static void restore_local_peripheral(bool suspended) {
    if (!suspended) {
        return;
    }

#if IS_ENABLED(CONFIG_SHIELD_TOUCAN_LEFT)
    int err = display_blanking_off(DEVICE_DT_GET(DT_CHOSEN(zephyr_display)));
    if (err < 0) {
        LOG_WRN("Unable to restore display after failed soft power off (%d)", err);
    }
#elif IS_ENABLED(CONFIG_SHIELD_TOUCAN_RIGHT)
    int err = tps43_set_sleep(DEVICE_DT_GET(DT_NODELABEL(tps43_trackpad)), false);
    if (err < 0) {
        LOG_WRN("Unable to restore trackpad after failed soft power off (%d)", err);
    }
#endif
}

static void enter_soft_off(void) {
    bool peripheral_suspended = suspend_local_peripheral();
    clear_wake_gpio_latch();
    prepare_soft_power_marker();

    LOG_INF("Entering Toucan soft power off");
    int err = zmk_pm_soft_off();

    /* zmk_pm_soft_off() only returns when suspending a device failed. */
    clear_soft_power_marker();
    power_off_pending = false;
    restore_local_peripheral(peripheral_suspended);
    LOG_ERR("Unable to enter Toucan soft power off (%d)", err);
}

static int reenter_soft_off_after_rejected_wake(void) {
    if (!device_is_ready(soft_power_wakeup)) {
        return -ENODEV;
    }

    bool peripheral_suspended = suspend_local_peripheral();

    /*
     * This runs before the normal ZMK application and kscan initialization.
     * Do not call zmk_pm_soft_off() here: it walks and suspends every device,
     * including devices whose init functions have not run yet. Instead, reset
     * and resume only ZMK's dedicated wake-source device, just as the normal
     * inactivity sleep path leaves its wake-capable kscan ready before calling
     * sys_poweroff().
     */
    int err = pm_device_action_run(soft_power_wakeup, PM_DEVICE_ACTION_SUSPEND);
    if (err < 0 && err != -EALREADY) {
        restore_local_peripheral(peripheral_suspended);
        return err;
    }

    if (!pm_device_wakeup_enable(soft_power_wakeup, true)) {
        restore_local_peripheral(peripheral_suspended);
        return -ENOTSUP;
    }

    err = pm_device_action_run(soft_power_wakeup, PM_DEVICE_ACTION_RESUME);
    if (err < 0) {
        restore_local_peripheral(peripheral_suspended);
        return err;
    }

    /* All keys are released, so clear the old event after SENSE is rearmed. */
    clear_wake_gpio_latch();
    prepare_soft_power_marker();

    LOG_INF("Returning rejected Toucan wake to soft off");
    sys_poweroff();

    CODE_UNREACHABLE;
}

static void power_off_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    enter_soft_off();
}

static void clear_wake_suppression_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    wake_suppression = 0U;
}

static void disconnect_matrix(void) {
    for (size_t i = 0; i < ARRAY_SIZE(matrix_rows); i++) {
        gpio_pin_configure_dt(&matrix_rows[i], GPIO_DISCONNECTED);
    }

    for (size_t i = 0; i < ARRAY_SIZE(matrix_columns); i++) {
        gpio_pin_configure_dt(&matrix_columns[i], GPIO_DISCONNECTED);
    }
}

static int prepare_matrix_for_validation(void) {
    for (size_t i = 0; i < ARRAY_SIZE(matrix_rows); i++) {
        if (!device_is_ready(matrix_rows[i].port)) {
            return -ENODEV;
        }

        int err = gpio_pin_configure_dt(&matrix_rows[i], GPIO_INPUT);
        if (err < 0) {
            return err;
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(matrix_columns); i++) {
        if (!device_is_ready(matrix_columns[i].port)) {
            return -ENODEV;
        }

        int err = gpio_pin_configure_dt(&matrix_columns[i], GPIO_OUTPUT_INACTIVE);
        if (err < 0) {
            return err;
        }
    }

    return 0;
}

static int scan_matrix(uint32_t *pressed) {
    *pressed = 0U;

    for (size_t column = 0; column < ARRAY_SIZE(matrix_columns); column++) {
        int err = gpio_pin_set_dt(&matrix_columns[column], 1);
        if (err < 0) {
            return err;
        }

        k_busy_wait(5);

        for (size_t row = 0; row < ARRAY_SIZE(matrix_rows); row++) {
            int state = gpio_pin_get_dt(&matrix_rows[row]);
            if (state < 0) {
                gpio_pin_set_dt(&matrix_columns[column], 0);
                return state;
            }

            if (state != 0) {
                *pressed |= MATRIX_BIT(row, column);
            }
        }

        err = gpio_pin_set_dt(&matrix_columns[column], 0);
        if (err < 0) {
            return err;
        }
    }

    return 0;
}

static int validate_wake_chord(void) {
    int err = prepare_matrix_for_validation();
    if (err < 0) {
        LOG_ERR("Unable to prepare matrix for wake validation (%d)", err);
        disconnect_matrix();
        return err;
    }

    int64_t deadline = k_uptime_get() + CONFIG_TOUCAN_SOFT_POWER_WAKE_VALIDATION_MS;
    int64_t stable_since = -1;
    bool accepted = false;

    while (k_uptime_get() < deadline) {
        uint32_t matrix_state;
        err = scan_matrix(&matrix_state);
        if (err < 0) {
            LOG_ERR("Unable to scan matrix for wake validation (%d)", err);
            disconnect_matrix();
            return err;
        }

        /* Any non-chord key rejects the wake immediately. */
        if ((matrix_state & ~TOUCAN_POWER_MATRIX_MASK) != 0U) {
            break;
        }

        int64_t now = k_uptime_get();
        if (matrix_state == TOUCAN_POWER_MATRIX_MASK) {
            if (stable_since < 0) {
                stable_since = now;
            } else if (now - stable_since >= CONFIG_TOUCAN_SOFT_POWER_WAKE_STABLE_MS) {
                accepted = true;
                break;
            }
        } else {
            stable_since = -1;
        }

        k_sleep(K_MSEC(2));
    }

    disconnect_matrix();
    return accepted ? 1 : 0;
}

static int wait_for_all_keys_released(void) {
    int err = prepare_matrix_for_validation();
    if (err < 0) {
        disconnect_matrix();
        return err;
    }

    int64_t released_since = -1;

    while (true) {
        uint32_t matrix_state;
        err = scan_matrix(&matrix_state);
        if (err < 0) {
            disconnect_matrix();
            return err;
        }

        int64_t now = k_uptime_get();
        if (matrix_state == 0U) {
            if (released_since < 0) {
                released_since = now;
            } else if (now - released_since >= CONFIG_TOUCAN_SOFT_POWER_OFF_DELAY_MS) {
                disconnect_matrix();
                return 0;
            }
        } else {
            released_since = -1;
        }

        k_sleep(K_MSEC(2));
    }
}

static void reset_entry_state(void) {
    entry_state = ENTRY_IDLE;
    first_tap_pressed_at = 0;
    first_tap_released_at = 0;
    capture_started_at = 0;
    chord_started_at = -1;
}

static void expire_entry_state(int64_t timestamp) {
    int64_t elapsed;

    switch (entry_state) {
    case ENTRY_TAP_PRIMED:
        elapsed = timestamp - first_tap_released_at;
        if (elapsed < 0 || elapsed > CONFIG_TOUCAN_SOFT_POWER_ARM_WINDOW_MS) {
            reset_entry_state();
        }
        break;
    case ENTRY_CAPTURING:
        elapsed = timestamp - capture_started_at;
        if (elapsed < 0 || elapsed > CONFIG_TOUCAN_SOFT_POWER_CAPTURE_TIMEOUT_MS) {
            LOG_DBG("Toucan soft power capture timed out");
            reset_entry_state();
        }
        break;
    default:
        break;
    }
}

static void schedule_power_off_if_released(void) {
    if (pressed_positions != 0U) {
        return;
    }

    power_off_armed = false;
    power_off_pending = true;
    k_work_reschedule(&power_off_work, K_MSEC(CONFIG_TOUCAN_SOFT_POWER_OFF_DELAY_MS));
}

static int handle_entry_event(const struct zmk_position_state_changed *event, uint64_t key_bit,
                              uint64_t previously_pressed) {
    switch (entry_state) {
    case ENTRY_IDLE:
        if (event->state && event->position == TOUCAN_POWER_FINAL_POSITION &&
            previously_pressed == 0U) {
            entry_state = ENTRY_FIRST_TAP_DOWN;
            first_tap_pressed_at = event->timestamp;
        }
        return ZMK_EV_EVENT_BUBBLE;

    case ENTRY_FIRST_TAP_DOWN:
        if (event->position != TOUCAN_POWER_FINAL_POSITION) {
            /* The priming tap must be isolated, but ordinary layer use still bubbles. */
            reset_entry_state();
            return ZMK_EV_EVENT_BUBBLE;
        }

        if (!event->state) {
            int64_t duration = event->timestamp - first_tap_pressed_at;
            bool isolated = previously_pressed == TOUCAN_POWER_FINAL_BIT &&
                            pressed_positions == 0U;

            reset_entry_state();
            if (isolated && duration >= 0 &&
                duration <= CONFIG_TOUCAN_SOFT_POWER_ARM_TAP_MAX_MS) {
                entry_state = ENTRY_TAP_PRIMED;
                first_tap_released_at = event->timestamp;
                LOG_DBG("Toucan soft power tap primed");
            }
        }
        return ZMK_EV_EVENT_BUBBLE;

    case ENTRY_TAP_PRIMED:
        if (event->state && event->position == TOUCAN_POWER_FINAL_POSITION &&
            previously_pressed == 0U) {
            entry_state = ENTRY_CAPTURING;
            capture_started_at = event->timestamp;
            chord_started_at = -1;
            LOG_INF("Toucan soft power capture armed");
        } else {
            reset_entry_state();
        }
        return ZMK_EV_EVENT_BUBBLE;

    case ENTRY_CAPTURING:
        if (event->position == TOUCAN_POWER_FINAL_POSITION) {
            if (!event->state) {
                LOG_DBG("Toucan soft power capture cancelled by thumb release");
                reset_entry_state();
            }
            return ZMK_EV_EVENT_BUBBLE;
        }

        if (!event->state) {
            reset_entry_state();
            return ZMK_EV_EVENT_BUBBLE;
        }

        if ((key_bit & TOUCAN_POWER_ENTRY_MASK) == 0U ||
            (pressed_positions & ~TOUCAN_POWER_TARGET_MASK) != 0U) {
            LOG_DBG("Toucan soft power capture cancelled by another key");
            reset_entry_state();
            return ZMK_EV_EVENT_BUBBLE;
        }

        if (chord_started_at < 0) {
            chord_started_at = event->timestamp;
        }

        /* Every target key entered during capture stays private, even on timeout. */
        captured_positions |= key_bit;

        int64_t elapsed = event->timestamp - chord_started_at;
        if (elapsed < 0 || elapsed > CONFIG_TOUCAN_SOFT_POWER_CHORD_TIMEOUT_MS) {
            LOG_DBG("Toucan soft power chord timed out");
            reset_entry_state();
            return ZMK_EV_EVENT_HANDLED;
        }

        if (pressed_positions == TOUCAN_POWER_TARGET_MASK) {
            power_off_armed = true;
            reset_entry_state();
            LOG_INF("Toucan soft power chord recognized; release it to power off");
        }

        return ZMK_EV_EVENT_HANDLED;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

static int position_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);

    if (event == NULL || event->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (event->position >= 64U) {
        reset_entry_state();
        power_off_armed = false;
        return ZMK_EV_EVENT_BUBBLE;
    }

    expire_entry_state(event->timestamp);

    uint64_t key_bit = POSITION_BIT(event->position);
    uint64_t previously_pressed = pressed_positions;

    if (event->state) {
        pressed_positions |= key_bit;
    } else {
        pressed_positions &= ~key_bit;
    }

    if ((wake_suppression & key_bit) != 0U) {
        if (!event->state) {
            wake_suppression &= ~key_bit;
        }
        return ZMK_EV_EVENT_HANDLED;
    }

    if ((captured_positions & key_bit) != 0U) {
        if (!event->state) {
            captured_positions &= ~key_bit;

            if (power_off_armed) {
                schedule_power_off_if_released();
            } else if (entry_state == ENTRY_CAPTURING) {
                LOG_DBG("Toucan soft power capture cancelled by an early release");
                reset_entry_state();
            }
        }
        return ZMK_EV_EVENT_HANDLED;
    }

    if (power_off_armed) {
        if (event->state) {
            /* Any fresh press makes the completed chord ambiguous, so cancel. */
            power_off_armed = false;
            return ZMK_EV_EVENT_BUBBLE;
        }

        /* The layer-thumb press was reported normally, so report its release too. */
        schedule_power_off_if_released();
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (power_off_pending) {
        if (event->state) {
            k_work_cancel_delayable(&power_off_work);
            power_off_pending = false;
        }

        /* The pending shutdown was cancelled, so preserve the normal key pair. */
        return ZMK_EV_EVENT_BUBBLE;
    }

    return handle_entry_event(event, key_bit, previously_pressed);
}

ZMK_LISTENER(toucan_soft_power, position_state_changed_listener);
ZMK_SUBSCRIPTION(toucan_soft_power, zmk_position_state_changed);

static int toucan_soft_power_init(void) {
    uint32_t marker =
        nrf_power_gpregret_get(NRF_POWER, TOUCAN_SOFT_POWER_GPRET_INDEX);
    uint32_t reset_reason = nrf_power_resetreas_get(NRF_POWER);

    if (marker != TOUCAN_SOFT_POWER_MARKER) {
        return 0;
    }

    /*
     * A reset button, software reset, debugger, or USB wake remains an escape
     * hatch. Only a GPIO wake from our System OFF state enables the guard.
     */
    uint32_t bypass_reasons = NRF_POWER_RESETREAS_RESETPIN_MASK |
                              NRF_POWER_RESETREAS_DOG_MASK |
                              NRF_POWER_RESETREAS_SREQ_MASK |
                              NRF_POWER_RESETREAS_LOCKUP_MASK |
                              NRF_POWER_RESETREAS_LPCOMP_MASK |
                              NRF_POWER_RESETREAS_DIF_MASK |
                              NRF_POWER_RESETREAS_NFC_MASK |
                              NRF_POWER_RESETREAS_VBUS_MASK;

    if ((reset_reason & NRF_POWER_RESETREAS_OFF_MASK) == 0U ||
        (reset_reason & bypass_reasons) != 0U) {
        clear_soft_power_marker();
        return 0;
    }

    LOG_INF("Validating Toucan soft power wake chord");

    int wake_result = validate_wake_chord();
    if (wake_result > 0) {
        clear_soft_power_marker();
        wake_suppression = TOUCAN_POWER_TARGET_MASK;
        k_work_reschedule(&clear_wake_suppression_work, K_SECONDS(2));
        LOG_INF("Toucan soft power wake chord accepted");
        return 0;
    }

    if (wake_result < 0) {
        clear_soft_power_marker();
        LOG_ERR("Wake validation failed; booting normally as a recovery fallback");
        return 0;
    }

    LOG_INF("Incomplete Toucan wake chord; returning to soft off");

    int err = wait_for_all_keys_released();
    if (err < 0) {
        clear_soft_power_marker();
        LOG_ERR("Unable to wait for all keys to release (%d); booting normally", err);
        return 0;
    }

    err = reenter_soft_off_after_rejected_wake();
    clear_soft_power_marker();
    LOG_ERR("Unable to rearm rejected Toucan wake (%d); booting normally", err);

    return 0;
}

/* Validate before the display, trackpad, radios, and ZMK application initialize. */
SYS_INIT(toucan_soft_power_init, POST_KERNEL, CONFIG_TOUCAN_SOFT_POWER_INIT_PRIORITY);
