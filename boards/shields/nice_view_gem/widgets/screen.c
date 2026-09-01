/*
 * Toucan2 144x168 static dashboard.
 *
 * The framebuffer is updated directly so LVGL can invalidate only the rows
 * belonging to a changed band. The Sharp memory LCD still transmits full-width
 * rows, but an idle dashboard causes no display traffic.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/hid_indicators.h>
#include <zmk/keymap.h>
#include <zmk/split/central.h>
#include <zmk/usb.h>

#include "screen.h"
#include "sleep.h"
#include "toucan_platform_mode.h"
#include "toucan_split_status.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(ZMK_BLE_PROFILE_COUNT >= TOUCAN_BT_PROFILE_COUNT,
             "Toucan dashboard requires five host BLE profiles");

enum dashboard_band {
    DASHBOARD_BAND_POWER = BIT(0),
    DASHBOARD_BAND_LAYER = BIT(1),
    DASHBOARD_BAND_BT = BIT(2),
    DASHBOARD_BAND_MEMORY = BIT(3),
    DASHBOARD_BAND_SYSTEM = BIT(4),
};

/* The reference puts the power bolts at y=2, one row above the nominal band. */
#define POWER_DIRTY_Y 2
#define POWER_DIRTY_HEIGHT 29
#define POWER_CONTENT_Y 3
#define LAYER_Y 44
#define LAYER_HEIGHT 22
#define BT_Y 79
#define BT_HEIGHT 20
#define MEMORY_Y 112
#define MEMORY_HEIGHT 20
#define SYSTEM_Y 145
#define SYSTEM_HEIGHT 16

struct bitmap_glyph {
    char code;
    uint8_t width;
    uint8_t height;
    const char *pixels;
};

/* 5x6 STATUS font from the display handoff. */
static const struct bitmap_glyph status_glyphs[] = {
    {'0', 5, 6, ".XXX." "X...X" "X...X" "X...X" "X...X" ".XXX."},
    {'1', 5, 6, "..X.." ".XX.." "..X.." "..X.." "..X.." ".XXX."},
    {'2', 5, 6, ".XXX." "X...X" "...X." "..X.." ".X..." "XXXXX"},
    {'3', 5, 6, "XXXX." "....X" "..XX." "....X" "X...X" ".XXX."},
    {'4', 5, 6, "..XX." ".X.X." "X..X." "XXXXX" "...X." "...X."},
    {'5', 5, 6, "XXXXX" "X...." "XXXX." "....X" "X...X" ".XXX."},
    {'6', 5, 6, ".XXX." "X...." "XXXX." "X...X" "X...X" ".XXX."},
    {'7', 5, 6, "XXXXX" "....X" "...X." "..X.." ".X..." ".X..."},
    {'8', 5, 6, ".XXX." "X...X" ".XXX." "X...X" "X...X" ".XXX."},
    {'9', 5, 6, ".XXX." "X...X" "X...X" ".XXXX" "....X" ".XXX."},
    {'A', 5, 6, ".XXX." "X...X" "X...X" "XXXXX" "X...X" "X...X"},
    {'B', 5, 6, "XXXX." "X...X" "XXXX." "X...X" "X...X" "XXXX."},
    {'C', 5, 6, ".XXXX" "X...." "X...." "X...." "X...." ".XXXX"},
    {'D', 5, 6, "XXXX." "X...X" "X...X" "X...X" "X...X" "XXXX."},
    {'E', 5, 6, "XXXXX" "X...." "XXXX." "X...." "X...." "XXXXX"},
    {'F', 5, 6, "XXXXX" "X...." "XXXX." "X...." "X...." "X...."},
    {'G', 5, 6, ".XXXX" "X...." "X.XXX" "X...X" "X...X" ".XXXX"},
    {'H', 5, 6, "X...X" "X...X" "XXXXX" "X...X" "X...X" "X...X"},
    {'I', 3, 6, "XXX" ".X." ".X." ".X." ".X." "XXX"},
    {'J', 5, 6, "...XX" "....X" "....X" "....X" "X...X" ".XXX."},
    {'K', 5, 6, "X...X" "X..X." "XXX.." "X..X." "X...X" "X...X"},
    {'L', 5, 6, "X...." "X...." "X...." "X...." "X...." "XXXXX"},
    {'M', 5, 6, "X...X" "XX.XX" "X.X.X" "X...X" "X...X" "X...X"},
    {'N', 5, 6, "X...X" "XX..X" "X.X.X" "X..XX" "X...X" "X...X"},
    {'O', 5, 6, ".XXX." "X...X" "X...X" "X...X" "X...X" ".XXX."},
    {'P', 5, 6, "XXXX." "X...X" "X...X" "XXXX." "X...." "X...."},
    {'Q', 5, 6, ".XXX." "X...X" "X...X" "X.X.X" "X..X." ".XX.X"},
    {'R', 5, 6, "XXXX." "X...X" "X...X" "XXXX." "X..X." "X...X"},
    {'S', 5, 6, ".XXXX" "X...." ".XXX." "....X" "....X" "XXXX."},
    {'T', 5, 6, "XXXXX" "..X.." "..X.." "..X.." "..X.." "..X.."},
    {'U', 5, 6, "X...X" "X...X" "X...X" "X...X" "X...X" ".XXX."},
    {'V', 5, 6, "X...X" "X...X" "X...X" ".X.X." ".X.X." "..X.."},
    {'W', 5, 6, "X...X" "X...X" "X...X" "X.X.X" "XX.XX" "X...X"},
    {'X', 5, 6, "X...X" ".X.X." "..X.." "..X.." ".X.X." "X...X"},
    {'Y', 5, 6, "X...X" ".X.X." "..X.." "..X.." "..X.." "..X.."},
    {'Z', 5, 6, "XXXXX" "....X" "...X." "..X.." ".X..." "XXXXX"},
    {'!', 1, 6, "X" "X" "X" "X" "." "X"},
    {'?', 4, 6, "XXX." "...X" "..X." "..X." "...." "..X."},
};

/* 4x5 LAYER font from the display handoff, plus digits for fallback L<n>. */
static const struct bitmap_glyph layer_glyphs[] = {
    {'0', 4, 5, ".XX." "X..X" "X..X" "X..X" ".XX."},
    {'1', 3, 5, ".X." "XX." ".X." ".X." "XXX"},
    {'2', 4, 5, "XXX." "...X" ".XX." "X..." "XXXX"},
    {'3', 4, 5, "XXX." "...X" ".XX." "...X" "XXX."},
    {'4', 4, 5, "X..X" "X..X" "XXXX" "...X" "...X"},
    {'5', 4, 5, "XXXX" "X..." "XXX." "...X" "XXX."},
    {'6', 4, 5, ".XX." "X..." "XXX." "X..X" ".XX."},
    {'7', 4, 5, "XXXX" "...X" "..X." ".X.." ".X.."},
    {'8', 4, 5, ".XX." "X..X" ".XX." "X..X" ".XX."},
    {'9', 4, 5, ".XX." "X..X" ".XXX" "...X" ".XX."},
    {'A', 4, 5, ".XX." "X..X" "XXXX" "X..X" "X..X"},
    {'B', 4, 5, "XXX." "X..X" "XXX." "X..X" "XXX."},
    {'C', 4, 5, ".XXX" "X..." "X..." "X..." ".XXX"},
    {'D', 4, 5, "XXX." "X..X" "X..X" "X..X" "XXX."},
    {'E', 4, 5, "XXXX" "X..." "XXX." "X..." "XXXX"},
    {'G', 4, 5, ".XXX" "X..." "X.XX" "X..X" ".XXX"},
    {'L', 4, 5, "X..." "X..." "X..." "X..." "XXXX"},
    {'M', 5, 5, "X...X" "XX.XX" "X.X.X" "X...X" "X...X"},
    {'N', 4, 5, "X..X" "XX.X" "X.XX" "X..X" "X..X"},
    {'O', 4, 5, ".XX." "X..X" "X..X" "X..X" ".XX."},
    {'P', 4, 5, "XXX." "X..X" "XXX." "X..." "X..."},
    {'S', 4, 5, ".XXX" "X..." ".XX." "...X" "XXX."},
    {'T', 3, 5, "XXX" ".X." ".X." ".X." ".X."},
    {'U', 4, 5, "X..X" "X..X" "X..X" "X..X" ".XX."},
    {'V', 3, 5, "X.X" "X.X" "X.X" "X.X" ".X."},
    {'Y', 3, 5, "X.X" "X.X" ".X." ".X." ".X."},
    {'?', 4, 5, "XXX." "...X" ".XX." "...." ".X.."},
};

static const struct bitmap_glyph bolt_icon = {
    .width = 4,
    .height = 6,
    .pixels = "..XX" ".XX." "XXXX" "..X." ".X.." "X...",
};

static const struct bitmap_glyph cross_icon = {
    .width = 5,
    .height = 5,
    .pixels = "X...X" ".X.X." "..X.." ".X.X." "X...X",
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

static void set_pixel(struct zmk_widget_screen *widget, int x, int y, bool ink) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) {
        return;
    }

    widget->cbuf[y * SCREEN_WIDTH + x] = ink ? LVGL_FOREGROUND : LVGL_BACKGROUND;
}

static void fill_rect(struct zmk_widget_screen *widget, int x, int y, int width, int height,
                      bool ink) {
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            set_pixel(widget, x + col, y + row, ink);
        }
    }
}

static void horizontal_line(struct zmk_widget_screen *widget, int x, int y, int width, bool ink) {
    fill_rect(widget, x, y, width, 1, ink);
}

static void vertical_line(struct zmk_widget_screen *widget, int x, int y, int height, bool ink) {
    fill_rect(widget, x, y, 1, height, ink);
}

static void outline_rect(struct zmk_widget_screen *widget, int x, int y, int width, int height) {
    horizontal_line(widget, x, y, width, true);
    horizontal_line(widget, x, y + height - 1, width, true);
    vertical_line(widget, x, y, height, true);
    vertical_line(widget, x + width - 1, y, height, true);
}

static void dither_rect(struct zmk_widget_screen *widget, int x, int y, int width, int height) {
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            if (((x + col + y + row) & 1) == 0) {
                set_pixel(widget, x + col, y + row, true);
            }
        }
    }
}

static void clear_rows(struct zmk_widget_screen *widget, int y, int height) {
    fill_rect(widget, 0, y, SCREEN_WIDTH, height, false);
}

static void clear_framebuffer(struct zmk_widget_screen *widget) {
    clear_rows(widget, 0, SCREEN_HEIGHT);
}

static const struct bitmap_glyph *find_glyph(const struct bitmap_glyph *glyphs,
                                              size_t glyph_count, char code) {
    for (size_t i = 0; i < glyph_count; i++) {
        if (glyphs[i].code == code) {
            return &glyphs[i];
        }
    }

    for (size_t i = 0; i < glyph_count; i++) {
        if (glyphs[i].code == '?') {
            return &glyphs[i];
        }
    }

    return NULL;
}

static void draw_bitmap(struct zmk_widget_screen *widget, int x, int y,
                        const struct bitmap_glyph *glyph, int scale) {
    for (int row = 0; row < glyph->height; row++) {
        for (int col = 0; col < glyph->width; col++) {
            if (glyph->pixels[row * glyph->width + col] == 'X') {
                fill_rect(widget, x + col * scale, y + row * scale, scale, scale, true);
            }
        }
    }
}

static int text_width(const char *text, int scale, const struct bitmap_glyph *glyphs,
                      size_t glyph_count) {
    int width = 0;

    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor == ' ') {
            width += 3 * scale;
            continue;
        }

        const struct bitmap_glyph *glyph = find_glyph(glyphs, glyph_count, *cursor);
        if (glyph != NULL) {
            width += (glyph->width + 1) * scale;
        }
    }

    return MAX(0, width - scale);
}

static int draw_text(struct zmk_widget_screen *widget, int x, int y, const char *text, int scale,
                     const struct bitmap_glyph *glyphs, size_t glyph_count) {
    int cursor_x = x;

    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor == ' ') {
            cursor_x += 3 * scale;
            continue;
        }

        const struct bitmap_glyph *glyph = find_glyph(glyphs, glyph_count, *cursor);
        if (glyph != NULL) {
            draw_bitmap(widget, cursor_x, y, glyph, scale);
            cursor_x += (glyph->width + 1) * scale;
        }
    }

    return MAX(0, cursor_x - x - scale);
}

#define STATUS_TEXT_WIDTH(text, scale)                                                         \
    text_width((text), (scale), status_glyphs, ARRAY_SIZE(status_glyphs))
#define DRAW_STATUS_TEXT(widget, x, y, text, scale)                                            \
    draw_text((widget), (x), (y), (text), (scale), status_glyphs, ARRAY_SIZE(status_glyphs))
#define LAYER_TEXT_WIDTH(text, scale)                                                          \
    text_width((text), (scale), layer_glyphs, ARRAY_SIZE(layer_glyphs))
#define DRAW_LAYER_TEXT(widget, x, y, text, scale)                                             \
    draw_text((widget), (x), (y), (text), (scale), layer_glyphs, ARRAY_SIZE(layer_glyphs))

static void draw_signal_bars(struct zmk_widget_screen *widget, int x, int y) {
    fill_rect(widget, x, y + 6, 3, 4, true);
    fill_rect(widget, x + 4, y + 3, 3, 7, true);
    fill_rect(widget, x + 8, y, 3, 10, true);
}

static void draw_battery_bar(struct zmk_widget_screen *widget, int x, uint8_t percentage,
                             bool connected) {
    outline_rect(widget, x, 18, 62, 12);

    if (!connected) {
        dither_rect(widget, x + 2, 20, 58, 8);
        return;
    }

    int fill_width = (MIN(percentage, 100U) * 58U + 50U) / 100U;
    if (fill_width > 0) {
        fill_rect(widget, x + 2, 20, fill_width, 8, true);
    }
}

static void draw_power_band(struct zmk_widget_screen *widget) {
    const struct toucan_display_state *state = &widget->state;
    char host_label[6];

    clear_rows(widget, POWER_DIRTY_Y, POWER_DIRTY_HEIGHT);

    if (state->selected_usb) {
        snprintf(host_label, sizeof(host_label), "USB%s", state->usb_hid_ready ? "" : "!");
    } else {
        uint8_t profile = MIN(state->active_profile_index, TOUCAN_BT_PROFILE_COUNT - 1);
        bool bonded = state->profiles_bonded[profile];
        char suffix = state->active_profile_connected && bonded ? '\0' : (bonded ? '!' : '?');

        if (suffix == '\0') {
            snprintf(host_label, sizeof(host_label), "BT%u", profile + 1U);
        } else {
            snprintf(host_label, sizeof(host_label), "BT%u%c", profile + 1U, suffix);
        }
    }

    int host_width = DRAW_STATUS_TEXT(widget, 4, POWER_CONTENT_Y, host_label, 2);
    if (state->usb_powered) {
        draw_bitmap(widget, 4 + host_width + 6, 2, &bolt_icon, 2);
    }
    draw_battery_bar(widget, 4, state->battery_left, true);

    if (state->right_connected) {
        draw_signal_bars(widget, 76, POWER_CONTENT_Y);
        if (state->right_usb_powered) {
            draw_bitmap(widget, 93, 2, &bolt_icon, 2);
        }
    } else {
        draw_bitmap(widget, 76, POWER_CONTENT_Y, &cross_icon, 2);
        DRAW_STATUS_TEXT(widget, 90, POWER_CONTENT_Y, "OFF", 2);
    }
    draw_battery_bar(widget, 76, state->battery_right, state->right_connected);
}

static const char *layer_name(uint8_t layer_index, char *fallback, size_t fallback_size) {
    static const char *const names[] = {
        "BASE", "SYM", "NAV", "CLP", "EDT", "MOU", "BT", "SYS", "DNG",
    };

    if (layer_index < ARRAY_SIZE(names)) {
        return names[layer_index];
    }

    snprintf(fallback, fallback_size, "L%u", layer_index);
    return fallback;
}

static void draw_layer_band(struct zmk_widget_screen *widget) {
    char fallback[6];
    const char *name = layer_name(widget->state.layer_index, fallback, sizeof(fallback));
    int width = LAYER_TEXT_WIDTH(name, 4);
    int x = (SCREEN_WIDTH - width + 1) / 2;

    clear_rows(widget, LAYER_Y, LAYER_HEIGHT);
    DRAW_LAYER_TEXT(widget, x, LAYER_Y, name, 4);
}

static void draw_dotted_underline(struct zmk_widget_screen *widget, int x, int y, int width) {
    for (int offset = 0; offset < width; offset += 2) {
        set_pixel(widget, x + offset, y, true);
        set_pixel(widget, x + 1 + offset, y + 1, true);
    }
}

static void draw_bt_band(struct zmk_widget_screen *widget) {
    clear_rows(widget, BT_Y, BT_HEIGHT);
    DRAW_STATUS_TEXT(widget, 4, 81, "BT", 2);

    for (int i = 0; i < TOUCAN_BT_PROFILE_COUNT; i++) {
        int slot_x = 42 + i * 20;

        if (widget->state.profiles_bonded[i]) {
            char digit[] = {(char)('1' + i), '\0'};
            DRAW_STATUS_TEXT(widget, slot_x + 4, 81, digit, 2);
        } else {
            fill_rect(widget, slot_x + 8, 86, 2, 2, true);
        }

        if (i == widget->state.active_profile_index) {
            if (widget->state.active_profile_connected &&
                widget->state.profiles_bonded[i]) {
                fill_rect(widget, slot_x + 2, 95, 14, 3, true);
            } else {
                draw_dotted_underline(widget, slot_x + 2, 95, 14);
            }
        }
    }
}

static void draw_memory_band(struct zmk_widget_screen *widget) {
    clear_rows(widget, MEMORY_Y, MEMORY_HEIGHT);
    DRAW_STATUS_TEXT(widget, 4, 114, "MEM", 2);

    for (int i = 0; i < TOUCAN_MEMORY_SLOT_COUNT; i++) {
        int slot_x = 42 + i * 20;

        if (widget->state.memory_slots_set[i]) {
            char digit[] = {(char)('1' + i), '\0'};
            DRAW_STATUS_TEXT(widget, slot_x + 4, 114, digit, 2);
        } else {
            fill_rect(widget, slot_x + 8, 119, 2, 2, true);
        }
    }
}

static void draw_system_band(struct zmk_widget_screen *widget) {
    clear_rows(widget, SYSTEM_Y, SYSTEM_HEIGHT);

    if (widget->state.caps_lock) {
        DRAW_STATUS_TEXT(widget, 3, 147, "CAPS", 2);
    }

    const char *platform = widget->state.windows_mode ? "WIN" : "MAC";
    int width = STATUS_TEXT_WIDTH(platform, 2);
    DRAW_STATUS_TEXT(widget, 141 - width, 147, platform, 2);
}

static void invalidate_rows(struct zmk_widget_screen *widget, int y, int height) {
    lv_area_t object_area;
    lv_obj_get_coords(widget->obj, &object_area);

    lv_area_t dirty_area = {
        .x1 = object_area.x1,
        .x2 = object_area.x1 + SCREEN_WIDTH - 1,
        .y1 = object_area.y1 + y,
        .y2 = object_area.y1 + y + height - 1,
    };
    lv_obj_invalidate_area(widget->obj, &dirty_area);
}

static void render_bands(struct zmk_widget_screen *widget, uint8_t bands) {
    if (!widget->ready || is_sleep_screen_active()) {
        return;
    }

    if ((bands & DASHBOARD_BAND_POWER) != 0U) {
        draw_power_band(widget);
        invalidate_rows(widget, POWER_DIRTY_Y, POWER_DIRTY_HEIGHT);
    }
    if ((bands & DASHBOARD_BAND_LAYER) != 0U) {
        draw_layer_band(widget);
        invalidate_rows(widget, LAYER_Y, LAYER_HEIGHT);
    }
    if ((bands & DASHBOARD_BAND_BT) != 0U) {
        draw_bt_band(widget);
        invalidate_rows(widget, BT_Y, BT_HEIGHT);
    }
    if ((bands & DASHBOARD_BAND_MEMORY) != 0U) {
        draw_memory_band(widget);
        invalidate_rows(widget, MEMORY_Y, MEMORY_HEIGHT);
    }
    if ((bands & DASHBOARD_BAND_SYSTEM) != 0U) {
        draw_system_band(widget);
        invalidate_rows(widget, SYSTEM_Y, SYSTEM_HEIGHT);
    }
}

static void render_full_dashboard(struct zmk_widget_screen *widget) {
    if (!widget->ready || is_sleep_screen_active()) {
        return;
    }

    clear_framebuffer(widget);
    draw_power_band(widget);
    draw_layer_band(widget);
    draw_bt_band(widget);
    draw_memory_band(widget);
    draw_system_band(widget);
    lv_obj_invalidate(widget->obj);
}

struct local_battery_state {
    uint8_t level;
};

static struct local_battery_state local_battery_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *event =
        eh != NULL ? as_zmk_battery_state_changed(eh) : NULL;

    return (struct local_battery_state){
        .level = event != NULL ? event->state_of_charge : zmk_battery_state_of_charge(),
    };
}

static void local_battery_update_cb(struct local_battery_state state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.battery_left = state.level;
        render_bands(widget, DASHBOARD_BAND_POWER);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(toucan_display_local_battery, struct local_battery_state,
                            local_battery_update_cb, local_battery_get_state);
ZMK_SUBSCRIPTION(toucan_display_local_battery, zmk_battery_state_changed);

struct peripheral_battery_state {
    uint8_t level;
};

static struct peripheral_battery_state peripheral_battery_get_state(const zmk_event_t *eh) {
    uint8_t level = 0U;
    const struct zmk_peripheral_battery_state_changed *event =
        eh != NULL ? as_zmk_peripheral_battery_state_changed(eh) : NULL;

    if (event != NULL && event->source == 0U) {
        level = event->state_of_charge;
    } else {
        (void)zmk_split_central_get_peripheral_battery_level(0, &level);
    }

    return (struct peripheral_battery_state){.level = level};
}

static void peripheral_battery_update_cb(struct peripheral_battery_state state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.battery_right = state.level;
        render_bands(widget, DASHBOARD_BAND_POWER);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(toucan_display_peripheral_battery,
                            struct peripheral_battery_state,
                            peripheral_battery_update_cb, peripheral_battery_get_state);
ZMK_SUBSCRIPTION(toucan_display_peripheral_battery, zmk_peripheral_battery_state_changed);

struct host_state {
    bool selected_usb;
    bool usb_powered;
    bool usb_hid_ready;
    uint8_t active_profile_index;
    bool active_profile_connected;
    bool profiles_bonded[TOUCAN_BT_PROFILE_COUNT];
};

static struct host_state host_get_state(const zmk_event_t *eh) {
    ARG_UNUSED(eh);

    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();
    int active_profile = zmk_ble_active_profile_index();
    struct host_state state = {
        .selected_usb = endpoint.transport == ZMK_TRANSPORT_USB,
        .usb_powered = zmk_usb_is_powered(),
        .usb_hid_ready = zmk_usb_is_hid_ready(),
        .active_profile_index = CLAMP(active_profile, 0, TOUCAN_BT_PROFILE_COUNT - 1),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
    };

    for (int i = 0; i < TOUCAN_BT_PROFILE_COUNT; i++) {
        state.profiles_bonded[i] = !zmk_ble_profile_is_open(i);
    }

    return state;
}

static void host_update_cb(struct host_state state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.selected_usb = state.selected_usb;
        widget->state.usb_powered = state.usb_powered;
        widget->state.usb_hid_ready = state.usb_hid_ready;
        widget->state.active_profile_index = state.active_profile_index;
        widget->state.active_profile_connected = state.active_profile_connected;
        memcpy(widget->state.profiles_bonded, state.profiles_bonded,
               sizeof(widget->state.profiles_bonded));
        render_bands(widget, DASHBOARD_BAND_POWER | DASHBOARD_BAND_BT);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(toucan_display_host, struct host_state, host_update_cb,
                            host_get_state);
ZMK_SUBSCRIPTION(toucan_display_host, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(toucan_display_host, zmk_usb_conn_state_changed);
ZMK_SUBSCRIPTION(toucan_display_host, zmk_ble_active_profile_changed);

struct layer_state {
    uint8_t index;
};

static struct layer_state layer_get_state(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    return (struct layer_state){.index = zmk_keymap_highest_layer_active()};
}

static void layer_update_cb(struct layer_state state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.layer_index = state.index;
        render_bands(widget, DASHBOARD_BAND_LAYER);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(toucan_display_layer, struct layer_state, layer_update_cb,
                            layer_get_state);
ZMK_SUBSCRIPTION(toucan_display_layer, zmk_layer_state_changed);

static struct toucan_split_status_changed split_status_get_state(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    return (struct toucan_split_status_changed){
        .connected = toucan_split_right_is_connected(),
        .usb_powered = toucan_split_right_is_usb_powered(),
    };
}

static void split_status_update_cb(struct toucan_split_status_changed state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.right_connected = state.connected;
        widget->state.right_usb_powered = state.usb_powered;
        render_bands(widget, DASHBOARD_BAND_POWER);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(toucan_display_split_status, struct toucan_split_status_changed,
                            split_status_update_cb, split_status_get_state);
ZMK_SUBSCRIPTION(toucan_display_split_status, toucan_split_status_changed);

struct caps_state {
    bool enabled;
};

static struct caps_state caps_get_state(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *event =
        eh != NULL ? as_zmk_hid_indicators_changed(eh) : NULL;
    zmk_hid_indicators_t indicators =
        event != NULL ? event->indicators : zmk_hid_indicators_get_current_profile();

    return (struct caps_state){
        .enabled = (indicators & BIT(HID_USAGE_LED_CAPS_LOCK - 1U)) != 0U,
    };
}

static void caps_update_cb(struct caps_state state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.caps_lock = state.enabled;
        render_bands(widget, DASHBOARD_BAND_SYSTEM);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(toucan_display_caps, struct caps_state, caps_update_cb,
                            caps_get_state);
ZMK_SUBSCRIPTION(toucan_display_caps, zmk_hid_indicators_changed);

struct platform_state {
    bool windows;
};

static struct platform_state platform_get_state(const zmk_event_t *eh) {
    const struct toucan_platform_mode_changed *event =
        eh != NULL ? as_toucan_platform_mode_changed(eh) : NULL;

    return (struct platform_state){
        .windows = event != NULL ? event->windows : toucan_platform_is_windows(),
    };
}

static void platform_update_cb(struct platform_state state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.windows_mode = state.windows;
        render_bands(widget, DASHBOARD_BAND_SYSTEM);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(toucan_display_platform, struct platform_state, platform_update_cb,
                            platform_get_state);
ZMK_SUBSCRIPTION(toucan_display_platform, toucan_platform_mode_changed);

static void draw_sleep_page(struct zmk_widget_screen *widget) {
    clear_framebuffer(widget);
    draw_sleep_screen(widget->obj);
    lv_obj_invalidate(widget->obj);
}

static int display_activity_event_handler(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *event = as_zmk_activity_state_changed(eh);
    if (event == NULL) {
        return -ENOTSUP;
    }

    if (event->state == ZMK_ACTIVITY_SLEEP) {
        set_sleep_screen_active(true);

        struct zmk_widget_screen *widget;
        SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { draw_sleep_page(widget); }

        lv_task_handler();
        lv_refr_now(NULL);
    } else if (event->state == ZMK_ACTIVITY_ACTIVE && is_sleep_screen_active()) {
        set_sleep_screen_active(false);

        struct zmk_widget_screen *widget;
        SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { render_full_dashboard(widget); }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(toucan_display_activity, display_activity_event_handler);
ZMK_SUBSCRIPTION(toucan_display_activity, zmk_activity_state_changed);

int zmk_widget_screen_init(struct zmk_widget_screen *widget, lv_obj_t *parent) {
    widget->obj = lv_canvas_create(parent);
    lv_canvas_set_buffer(widget->obj, widget->cbuf, SCREEN_WIDTH, SCREEN_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(widget->obj, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    clear_framebuffer(widget);

    /* Visual-only v1 placeholders; no memory controls or persistence yet. */
    widget->state.memory_slots_set[0] = true;
    widget->state.memory_slots_set[1] = true;
    widget->state.memory_slots_set[2] = false;

    sys_slist_append(&widgets, &widget->node);
    toucan_display_local_battery_init();
    toucan_display_peripheral_battery_init();
    toucan_display_host_init();
    toucan_display_layer_init();
    toucan_display_split_status_init();
    toucan_display_caps_init();
    toucan_display_platform_init();

    widget->ready = true;
    render_full_dashboard(widget);
    return 0;
}

lv_obj_t *zmk_widget_screen_obj(struct zmk_widget_screen *widget) {
    return widget->obj;
}
