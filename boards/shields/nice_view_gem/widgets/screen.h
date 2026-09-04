#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>
#include <zephyr/kernel.h>

#include "toucan_memory.h"
#include "util.h"

#define TOUCAN_BT_PROFILE_COUNT 5

enum toucan_display_page {
    TOUCAN_DISPLAY_PAGE_BOOT,
    TOUCAN_DISPLAY_PAGE_DASHBOARD,
    TOUCAN_DISPLAY_PAGE_SLEEP,
    TOUCAN_DISPLAY_PAGE_SOFT_OFF,
    TOUCAN_DISPLAY_PAGE_UF2,
    TOUCAN_DISPLAY_PAGE_MEMORY_SET,
};

struct toucan_display_state {
    uint8_t battery_left;
    uint8_t battery_right;
    uint8_t layer_index;
    uint8_t active_profile_index;
    bool selected_usb;
    bool usb_powered;
    bool usb_hid_ready;
    bool active_profile_connected;
    bool profiles_bonded[TOUCAN_BT_PROFILE_COUNT];
    bool right_connected;
    bool right_usb_powered;
    bool caps_lock;
    bool windows_mode;
    struct toucan_memory_snapshot memory;
};

struct toucan_display_animation {
    enum toucan_display_page page;
    enum toucan_display_page transition_target;
    bool transition_active;
    bool transition_signal;
    uint8_t transition_frame;
    uint16_t transition_revealed_rows;
    int64_t transition_due;

    bool boot_to_dashboard_pending;
    int64_t boot_to_dashboard_due;

    bool layer_roll_active;
    uint8_t layer_from;
    uint8_t layer_to;
    uint8_t layer_frame;
    int64_t layer_due;

    bool pairing_blink_active;
    bool pairing_marker_visible;
    int64_t pairing_due;

    bool low_battery_blink_active;
    bool low_battery_fill_visible;
    int64_t low_battery_due;

    bool local_bolt_blink_active;
    bool right_bolt_blink_active;
    bool local_bolt_visible;
    bool right_bolt_visible;
    uint8_t local_bolt_frame;
    uint8_t right_bolt_frame;
    int64_t local_bolt_due;
    int64_t right_bolt_due;

    bool memory_flash_active;
    bool memory_flash_inverted;
    int8_t memory_flash_slot;
    int8_t pending_memory_flash_slot;
    uint8_t memory_flash_frame;
    int64_t memory_flash_due;

    bool caret_visible;
    int64_t caret_due;
};

struct zmk_widget_screen {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_color_t cbuf[SCREEN_WIDTH * SCREEN_HEIGHT];
    struct toucan_display_state state;
    struct toucan_display_animation animation;
    bool ready;
};

int zmk_widget_screen_init(struct zmk_widget_screen *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_screen_obj(struct zmk_widget_screen *widget);
