#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>
#include <zephyr/kernel.h>

#include "util.h"

#define TOUCAN_BT_PROFILE_COUNT 5
#define TOUCAN_MEMORY_SLOT_COUNT 3

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
    bool memory_slots_set[TOUCAN_MEMORY_SLOT_COUNT];
};

struct zmk_widget_screen {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_color_t cbuf[SCREEN_WIDTH * SCREEN_HEIGHT];
    struct toucan_display_state state;
    bool ready;
};

int zmk_widget_screen_init(struct zmk_widget_screen *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_screen_obj(struct zmk_widget_screen *widget);
