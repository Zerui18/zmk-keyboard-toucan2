/*
 * Toucan2 144x168 event-driven dashboard and retained-image pages.
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
#include <limits.h>
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

#include "../assets/generated_fonts.h"
#include "../assets/display_icons.h"
#include "screen.h"
#include "toucan_display_hooks.h"
#include "toucan_key_text.h"
#include "toucan_memory.h"
#include "toucan_platform_mode.h"
#include "toucan_soft_power.h"
#include "toucan_split_status.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(ZMK_BLE_PROFILE_COUNT >= TOUCAN_BT_PROFILE_COUNT,
             "Toucan dashboard requires five host BLE profiles");

enum dashboard_band {
    DASHBOARD_BAND_POWER = BIT(0),
    DASHBOARD_BAND_LAYER = BIT(1),
    DASHBOARD_BAND_MEMORY = BIT(2),
    DASHBOARD_BAND_BT = BIT(3),
    DASHBOARD_BAND_SYSTEM = BIT(4),
};

/* The v2 reference has an 8 px top/side inset; bolts begin one row above PWR. */
#define CANVAS_PADDING 8
#define POWER_DIRTY_Y 7
#define POWER_DIRTY_HEIGHT 29
#define POWER_CONTENT_Y 8
#define LAYER_Y 48
#define LAYER_HEIGHT 22
#define MEMORY_Y 82
#define MEMORY_HEIGHT 20
#define BT_Y 114
#define BT_HEIGHT 20
#define SYSTEM_Y 146
#define SYSTEM_HEIGHT 16

#define PAGE_TRANSITION_FRAMES 7
#define PAGE_TRANSITION_FRAME_MS 83
#define PAGE_TRANSITION_TOTAL_MS (PAGE_TRANSITION_FRAMES * PAGE_TRANSITION_FRAME_MS)
#define PAGE_TRANSITION_GUARD_MS 50
#define BOOT_HOLD_MS 450
#define PAIRING_BLINK_MS 500
#define LOW_BATTERY_BLINK_MS 1000
#define LOW_BATTERY_PERCENT 15
#define BOLT_BLINK_FRAMES 6
#define BOLT_BLINK_FRAME_MS 250
#define MEMORY_FLASH_FRAMES 4
#define MEMORY_FLASH_FRAME_MS 150
#define CARET_BLINK_MS 530
/* Tall punctuation can extend above the capitals; leave one clear row per line. */
#define CONTENT_LINE_PITCH (status_font.height + 1)
#define CONTENT_GLYPH_HEIGHT MAX(status_font.height + status_font.y_offset, 16)
#define CONTENT_BOTTOM 148 /* Leave the TEXT counter at y=154 unobstructed. */

#ifndef TOUCAN_FIRMWARE_VERSION
#define TOUCAN_FIRMWARE_VERSION "0.3"
#endif

#ifndef TOUCAN_GIT_SHA
#define TOUCAN_GIT_SHA "UNKNOWN"
#endif

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

static void draw_disc(struct zmk_widget_screen *widget, int center_x, int center_y, int radius,
                      bool ink) {
    int threshold = radius * radius + radius;

    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x * x + y * y <= threshold) {
                set_pixel(widget, center_x + x, center_y + y, ink);
            }
        }
    }
}

static void draw_ring(struct zmk_widget_screen *widget, int center_x, int center_y, int radius,
                      int thickness) {
    int inner = radius - thickness;
    int outer_threshold = radius * radius + radius;
    int inner_threshold = inner * inner + inner;

    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int distance = x * x + y * y;
            if (distance <= outer_threshold && distance > inner_threshold) {
                set_pixel(widget, center_x + x, center_y + y, true);
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

static void draw_bitmap(struct zmk_widget_screen *widget, int x, int y,
                        const struct bitmap_glyph *glyph, bool ink) {
    for (int row = 0; row < glyph->height; row++) {
        for (int col = 0; col < glyph->width; col++) {
            if (glyph->pixels[row * glyph->width + col] == 'X') {
                set_pixel(widget, x + col, y + row, ink);
            }
        }
    }
}

static int text_width(const char *text, const struct bitmap_font *font) {
    return strlen(text) * font->advance;
}

static int draw_text(struct zmk_widget_screen *widget, int x, int y, const char *text,
                     const struct bitmap_font *font) {
    int cursor_x = x;

    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor != ' ') {
            const struct bitmap_glyph *glyph = find_glyph(font->glyphs, font->glyph_count, *cursor);
            if (glyph != NULL) {
                draw_bitmap(widget, cursor_x, y + font->y_offset, glyph, true);
            }
        }
        cursor_x += font->advance;
    }

    return cursor_x - x;
}

#define SMALL_TEXT_WIDTH(text) text_width((text), &small_font)
#define DRAW_SMALL_TEXT(widget, x, y, text) \
    draw_text((widget), (x), (y), (text), &small_font)
#define STATUS_TEXT_WIDTH(text) text_width((text), &status_font)
#define DRAW_STATUS_TEXT(widget, x, y, text) \
    draw_text((widget), (x), (y), (text), &status_font)
#define TITLE_TEXT_WIDTH(text) text_width((text), &title_font)
#define DRAW_TITLE_TEXT(widget, x, y, text) \
    draw_text((widget), (x), (y), (text), &title_font)
#define LAYER_TEXT_WIDTH(text) text_width((text), &layer_font)
#define DRAW_LAYER_TEXT(widget, x, y, text) \
    draw_text((widget), (x), (y), (text), &layer_font)

static void draw_signal_bars(struct zmk_widget_screen *widget, int x, int y) {
    fill_rect(widget, x, y + 6, 3, 4, true);
    fill_rect(widget, x + 4, y + 3, 3, 7, true);
    fill_rect(widget, x + 8, y, 3, 10, true);
}

static void draw_battery_bar(struct zmk_widget_screen *widget, int x, uint8_t percentage,
                             bool connected) {
    outline_rect(widget, x, 23, 60, 12);

    if (!connected) {
        dither_rect(widget, x + 2, 25, 56, 8);
        return;
    }

    bool low = percentage <= LOW_BATTERY_PERCENT;
    if (low && widget->animation.low_battery_blink_active &&
        !widget->animation.low_battery_fill_visible) {
        return;
    }

    int fill_width = (MIN(percentage, 100U) * 56U + 50U) / 100U;
    if (fill_width > 0) {
        fill_rect(widget, x + 2, 25, fill_width, 8, true);
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

    int host_width = DRAW_STATUS_TEXT(widget, CANVAS_PADDING, POWER_CONTENT_Y, host_label);
    if (state->usb_powered && widget->animation.local_bolt_visible) {
        draw_bitmap(widget, CANVAS_PADDING + host_width + 6, 7, &bolt_icon, true);
    }
    draw_battery_bar(widget, CANVAS_PADDING, state->battery_left, true);

    if (state->right_connected) {
        draw_signal_bars(widget, 76, POWER_CONTENT_Y);
        if (state->right_usb_powered && widget->animation.right_bolt_visible) {
            draw_bitmap(widget, 93, 7, &bolt_icon, true);
        }
    } else {
        draw_bitmap(widget, 76, POWER_CONTENT_Y, &cross_icon, true);
        DRAW_STATUS_TEXT(widget, 90, POWER_CONTENT_Y, "OFF");
    }
    draw_battery_bar(widget, 76, state->battery_right, state->right_connected);
}

static const char *layer_name(uint8_t layer_index, char *fallback, size_t fallback_size) {
    static const char *const names[] = {
        "BASE", "SYM", "NAV", "CLP", "EDT", "APP", "MOU", "SYS", "FN", "DNG",
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
    int width = LAYER_TEXT_WIDTH(name);
    int x = (SCREEN_WIDTH - width + 1) / 2;

    clear_rows(widget, LAYER_Y, LAYER_HEIGHT);
    DRAW_LAYER_TEXT(widget, x, LAYER_Y, name);
}

static void draw_dotted_underline(struct zmk_widget_screen *widget, int x, int y, int width) {
    for (int offset = 0; offset < width; offset += 2) {
        set_pixel(widget, x + offset, y, true);
        set_pixel(widget, x + 1 + offset, y + 1, true);
    }
}

static void draw_bt_band(struct zmk_widget_screen *widget) {
    clear_rows(widget, BT_Y, BT_HEIGHT);
    DRAW_STATUS_TEXT(widget, CANVAS_PADDING, BT_Y + 2, "BT");

    for (int i = 0; i < TOUCAN_BT_PROFILE_COUNT; i++) {
        int slot_x = 38 + i * 20;

        if (widget->state.profiles_bonded[i]) {
            char digit[] = {(char)('1' + i), '\0'};
            DRAW_STATUS_TEXT(widget, slot_x + 4, BT_Y + 2, digit);
        } else {
            fill_rect(widget, slot_x + 8, BT_Y + 7, 2, 2, true);
        }

        if (!widget->state.selected_usb && i == widget->state.active_profile_index) {
            if (widget->state.active_profile_connected &&
                widget->state.profiles_bonded[i]) {
                fill_rect(widget, slot_x + 2, BT_Y + 16, 14, 3, true);
            } else if (widget->animation.pairing_marker_visible) {
                draw_dotted_underline(widget, slot_x + 2, BT_Y + 16, 14);
            }
        }
    }
}

static void draw_memory_band(struct zmk_widget_screen *widget) {
    clear_rows(widget, MEMORY_Y, MEMORY_HEIGHT);
    DRAW_STATUS_TEXT(widget, CANVAS_PADDING, MEMORY_Y + 2, "ME");

    for (int i = 0; i < TOUCAN_MEMORY_SLOT_COUNT; i++) {
        int slot_x = 38 + i * 20;
        uint8_t type = widget->state.memory.slot_types[i];
        bool inverted = widget->animation.memory_flash_active &&
                        widget->animation.memory_flash_inverted &&
                        widget->animation.memory_flash_slot == i;

        if (inverted) {
            fill_rect(widget, slot_x, MEMORY_Y, 14, 16, true);
        }

        if (type != TOUCAN_MEMORY_SLOT_EMPTY) {
            char digit[] = {(char)('1' + i), '\0'};
            if (inverted) {
                const struct bitmap_glyph *glyph =
                    find_glyph(status_font.glyphs, status_font.glyph_count, digit[0]);
                if (glyph != NULL) {
                    draw_bitmap(widget, slot_x + 4, MEMORY_Y + 2 + status_font.y_offset,
                                glyph, false);
                }
            } else {
                DRAW_STATUS_TEXT(widget, slot_x + 4, MEMORY_Y + 2, digit);
            }

            if (type == TOUCAN_MEMORY_SLOT_SEQUENCE && !inverted) {
                fill_rect(widget, slot_x + 15, MEMORY_Y + 1, 2, 4, true);
            }
        } else if (!inverted) {
            fill_rect(widget, slot_x + 8, MEMORY_Y + 7, 2, 2, true);
        }
    }
}

static void draw_system_band(struct zmk_widget_screen *widget) {
    clear_rows(widget, SYSTEM_Y, SYSTEM_HEIGHT);

    if (widget->state.caps_lock) {
        DRAW_STATUS_TEXT(widget, CANVAS_PADDING, 148, "CAPS");
    }

    const struct bitmap_glyph *platform =
        widget->state.windows_mode ? &windows_icon : &apple_icon;
    int x = SCREEN_WIDTH - CANVAS_PADDING - platform->width;
    int y = SYSTEM_Y + (SYSTEM_HEIGHT - platform->height) / 2;
    draw_bitmap(widget, x, y, platform, true);
}

static void invalidate_rows(struct zmk_widget_screen *widget, int y, int height) {
    if (height <= 0) {
        return;
    }

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

static enum toucan_display_page effective_page(const struct zmk_widget_screen *widget) {
    return widget->animation.transition_active ? widget->animation.transition_target
                                               : widget->animation.page;
}

static void render_bands(struct zmk_widget_screen *widget, uint8_t bands) {
    if (!widget->ready || effective_page(widget) != TOUCAN_DISPLAY_PAGE_DASHBOARD) {
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
    if ((bands & DASHBOARD_BAND_MEMORY) != 0U) {
        draw_memory_band(widget);
        invalidate_rows(widget, MEMORY_Y, MEMORY_HEIGHT);
    }
    if ((bands & DASHBOARD_BAND_BT) != 0U) {
        draw_bt_band(widget);
        invalidate_rows(widget, BT_Y, BT_HEIGHT);
    }
    if ((bands & DASHBOARD_BAND_SYSTEM) != 0U) {
        draw_system_band(widget);
        invalidate_rows(widget, SYSTEM_Y, SYSTEM_HEIGHT);
    }
}

static void draw_sleep_page(struct zmk_widget_screen *widget) {
    /* Split and host state can go stale while this retained image is visible. */
    const int center_x = SCREEN_WIDTH / 2;
    const int center_y = SCREEN_HEIGHT / 2;

    draw_disc(widget, center_x, center_y, 24, true);
    draw_disc(widget, center_x + 9, center_y - 7, 21, false);
}

static bool wake_position_is_set(uint8_t position, const uint8_t *positions, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (positions[i] == position) {
            return true;
        }
    }
    return false;
}

static void draw_wake_key(struct zmk_widget_screen *widget, int x, int y, bool active) {
    if (active) {
        fill_rect(widget, x, y, 5, 5, true);
    } else {
        fill_rect(widget, x + 2, y + 2, 2, 2, true);
    }
}

static void draw_soft_off_page(struct zmk_widget_screen *widget) {
    draw_ring(widget, 72, 38, 17, 4);
    fill_rect(widget, 66, 19, 13, 10, false);
    fill_rect(widget, 70, 22, 4, 14, true);

    int width = TITLE_TEXT_WIDTH("OFF");
    DRAW_TITLE_TEXT(widget, (SCREEN_WIDTH - width + 1) / 2, 68, "OFF");
    width = STATUS_TEXT_WIDTH("WAKE KEYS");
    DRAW_STATUS_TEXT(widget, (SCREEN_WIDTH - width + 1) / 2, 94, "WAKE KEYS");

    uint8_t positions[8];
    size_t count = toucan_soft_power_wake_positions(positions, ARRAY_SIZE(positions));
    count = MIN(count, ARRAY_SIZE(positions));
    const int origin_x = 47;
    const int origin_y = 114;
    const int pitch = 9;

    for (uint8_t row = 0U; row < 3U; row++) {
        for (uint8_t column = 0U; column < 6U; column++) {
            uint8_t position = row * 12U + column;
            draw_wake_key(widget, origin_x + column * pitch, origin_y + row * pitch,
                          wake_position_is_set(position, positions, count));
        }
    }

    for (uint8_t thumb = 0U; thumb < 3U; thumb++) {
        uint8_t position = 36U + thumb;
        draw_wake_key(widget, origin_x + (3 + thumb) * pitch, origin_y + 3 * pitch,
                      wake_position_is_set(position, positions, count));
    }
}

static void draw_uf2_page(struct zmk_widget_screen *widget) {
    fill_rect(widget, 70, 26, 4, 14, true);
    for (int row = 0; row < 8; row++) {
        horizontal_line(widget, 64 + row, 40 + row, 16 - 2 * row, true);
    }
    fill_rect(widget, 60, 52, 24, 3, true);

    int width = LAYER_TEXT_WIDTH("UF2");
    DRAW_LAYER_TEXT(widget, (SCREEN_WIDTH - width + 1) / 2, 66, "UF2");
    width = STATUS_TEXT_WIDTH("COPY .UF2");
    DRAW_STATUS_TEXT(widget, (SCREEN_WIDTH - width + 1) / 2, 98, "COPY .UF2");
}

static void draw_boot_page(struct zmk_widget_screen *widget) {
    int width = TITLE_TEXT_WIDTH("TOUCAN");
    DRAW_TITLE_TEXT(widget, (SCREEN_WIDTH - width + 1) / 2, 60, "TOUCAN");

    char version[32];
    snprintf(version, sizeof(version), "ZMK %s * %s", TOUCAN_FIRMWARE_VERSION,
             TOUCAN_GIT_SHA);
    width = SMALL_TEXT_WIDTH(version);
    DRAW_SMALL_TEXT(widget, (SCREEN_WIDTH - width + 1) / 2, 94, version);
}

struct content_cursor {
    int x;
    int y;
    bool clipped;
};

static size_t limited_text_length(const char *text, size_t capacity) {
    size_t length = 0U;
    while (length < capacity && text[length] != '\0') {
        length++;
    }
    return length;
}

static bool content_wrap(struct content_cursor *cursor, int width) {
    if (cursor->x + width > SCREEN_WIDTH - CANVAS_PADDING) {
        cursor->x = CANVAS_PADDING;
        cursor->y += CONTENT_LINE_PITCH;
    }
    if (cursor->y + CONTENT_GLYPH_HEIGHT > CONTENT_BOTTOM) {
        cursor->clipped = true;
    }
    return !cursor->clipped;
}

static void draw_content_text(struct zmk_widget_screen *widget, struct content_cursor *cursor,
                              const char *text) {
    for (const char *character = text; *character != '\0' && !cursor->clipped; character++) {
        if (*character == '\n') {
            cursor->x = CANVAS_PADDING;
            cursor->y += CONTENT_LINE_PITCH;
            (void)content_wrap(cursor, 0);
            continue;
        }
        if (*character == '\t') {
            for (int i = 0; i < 4; i++) {
                draw_content_text(widget, cursor, " ");
            }
            continue;
        }

        char glyph_text[] = {*character, '\0'};
        int width = STATUS_TEXT_WIDTH(glyph_text);
        if (!content_wrap(cursor, width)) {
            break;
        }
        cursor->x += DRAW_STATUS_TEXT(widget, cursor->x, cursor->y, glyph_text);
    }
}

static const struct bitmap_glyph *gui_modifier_icon(bool windows_mode) {
    return windows_mode ? &windows_icon : &command_icon;
}

static void draw_modifier_icon(struct zmk_widget_screen *widget, struct content_cursor *cursor,
                               const struct bitmap_glyph *icon) {
    if (!content_wrap(cursor, icon->width + 2)) {
        return;
    }
    draw_bitmap(widget, cursor->x, cursor->y - 2, icon, true);
    cursor->x += icon->width + 6;
}

static void draw_implicit_modifier_icons(struct zmk_widget_screen *widget,
                                         struct content_cursor *cursor, uint8_t modifiers) {
    if ((modifiers & (MOD_LGUI | MOD_RGUI)) != 0U) {
        draw_modifier_icon(widget, cursor, gui_modifier_icon(widget->state.windows_mode));
    }
    if ((modifiers & (MOD_LCTL | MOD_RCTL)) != 0U) {
        draw_modifier_icon(widget, cursor, &control_icon);
    }
    if ((modifiers & (MOD_LALT | MOD_RALT)) != 0U) {
        draw_modifier_icon(widget, cursor, &option_icon);
    }
    if ((modifiers & (MOD_LSFT | MOD_RSFT)) != 0U) {
        draw_modifier_icon(widget, cursor, &shift_icon);
    }
}

static void sequence_key_label(const struct toucan_memory_sequence_action *action,
                               bool has_character, char character, char *label, size_t label_size) {
    if (action->usage_page != HID_USAGE_KEY) {
        snprintf(label, label_size, "?");
        return;
    }

    const char *name = NULL;
    switch (action->keycode) {
    case HID_USAGE_KEY_KEYBOARD_RETURN_ENTER:
        name = "ENT";
        break;
    case HID_USAGE_KEY_KEYBOARD_ESCAPE:
        name = "ESC";
        break;
    case HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE:
        name = "BSP";
        break;
    case HID_USAGE_KEY_KEYBOARD_TAB:
        name = "TAB";
        break;
    case HID_USAGE_KEY_KEYBOARD_SPACEBAR:
        name = "SPC";
        break;
    case HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD:
        name = "DEL";
        break;
    case HID_USAGE_KEY_KEYBOARD_CAPS_LOCK:
        name = "CAP";
        break;
    default:
        if (has_character) {
            label[0] = character;
            label[1] = '\0';
            return;
        }
        name = "?";
        break;
    }
    snprintf(label, label_size, "%s", name);
}

static void draw_sequence_content(struct zmk_widget_screen *widget, struct content_cursor *cursor) {
    const struct toucan_memory_capture_snapshot *capture = &widget->state.memory.capture;
    struct toucan_key_text_state text_state = {.caps_lock = capture->initial_caps_lock};

    for (size_t i = 0; i < capture->sequence_action_count && !cursor->clipped; i++) {
        const struct toucan_memory_sequence_action *action = &capture->sequence[i];
        const struct toucan_memory_sequence_preview *preview = &capture->sequence_preview[i];
        char character = '\0';
        /* Named modifiers decorate the shortcut, not the character being entered.
         * Keep physical/implicit input Shift and Caps Lock for literal case. */
        bool has_character =
            toucan_key_text_apply(&text_state, action->usage_page, action->keycode,
                                  preview->implicit_modifiers, action->explicit_modifiers,
                                  action->pressed, &character);
        if (!action->pressed) {
            continue;
        }

        if (action->usage_page == HID_USAGE_KEY &&
            action->keycode >= HID_USAGE_KEY_KEYBOARD_LEFTCONTROL &&
            action->keycode <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI) {
            switch (action->keycode) {
            case HID_USAGE_KEY_KEYBOARD_LEFTCONTROL:
            case HID_USAGE_KEY_KEYBOARD_RIGHTCONTROL:
                draw_modifier_icon(widget, cursor, &control_icon);
                break;
            case HID_USAGE_KEY_KEYBOARD_LEFTSHIFT:
            case HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT:
                draw_modifier_icon(widget, cursor, &shift_icon);
                break;
            case HID_USAGE_KEY_KEYBOARD_LEFTALT:
            case HID_USAGE_KEY_KEYBOARD_RIGHTALT:
                draw_modifier_icon(widget, cursor, &option_icon);
                break;
            default:
                draw_modifier_icon(widget, cursor, gui_modifier_icon(widget->state.windows_mode));
                break;
            }
            continue;
        }

        for (size_t j = 0; j < ARRAY_SIZE(preview->modifier_order); j++) {
            draw_implicit_modifier_icons(widget, cursor, preview->modifier_order[j]);
        }
        char label[5];
        sequence_key_label(action, has_character, character, label, sizeof(label));
        draw_content_text(widget, cursor, label);
        cursor->x += 4;
    }
}

static void draw_memory_set_page(struct zmk_widget_screen *widget) {
    const struct toucan_memory_capture_snapshot *capture = &widget->state.memory.capture;
    char header[8];
    snprintf(header, sizeof(header), "MEM %u", capture->slot + 1U);
    DRAW_STATUS_TEXT(widget, CANVAS_PADDING, CANVAS_PADDING, header);

    const char *mode = capture->mode == TOUCAN_MEMORY_CAPTURE_SEQUENCE ? "SEQ" : "TEXT";
    int width = STATUS_TEXT_WIDTH(mode);
    DRAW_STATUS_TEXT(widget, 136 - width, CANVAS_PADDING, mode);
    for (int x = CANVAS_PADDING; x < 136; x += 2) {
        set_pixel(widget, x, 26, true);
    }

    struct content_cursor cursor = {
        .x = CANVAS_PADDING,
        .y = 36,
    };

    if (capture->mode == TOUCAN_MEMORY_CAPTURE_SEQUENCE) {
        draw_sequence_content(widget, &cursor);
        if (capture->sequence_overflow) {
            width = SMALL_TEXT_WIDTH("FULL");
            DRAW_SMALL_TEXT(widget, 136 - width, 154, "FULL");
        }
    } else {
        draw_content_text(widget, &cursor, capture->text);
        char counter[8];
        snprintf(counter, sizeof(counter), "%u/64",
                 (unsigned int)limited_text_length(capture->text,
                                                   TOUCAN_MEMORY_TEXT_CAPACITY));
        width = SMALL_TEXT_WIDTH(counter);
        DRAW_SMALL_TEXT(widget, 136 - width, 154, counter);
    }

    if (widget->animation.caret_visible && content_wrap(&cursor, 5)) {
        fill_rect(widget, cursor.x + 1, cursor.y, 3, status_font.cap_height, true);
    }
}

static void draw_scene(struct zmk_widget_screen *widget, enum toucan_display_page page) {
    clear_framebuffer(widget);

    switch (page) {
    case TOUCAN_DISPLAY_PAGE_BOOT:
        draw_boot_page(widget);
        break;
    case TOUCAN_DISPLAY_PAGE_DASHBOARD:
        draw_power_band(widget);
        draw_layer_band(widget);
        draw_memory_band(widget);
        draw_bt_band(widget);
        draw_system_band(widget);
        break;
    case TOUCAN_DISPLAY_PAGE_SLEEP:
        draw_sleep_page(widget);
        break;
    case TOUCAN_DISPLAY_PAGE_SOFT_OFF:
        draw_soft_off_page(widget);
        break;
    case TOUCAN_DISPLAY_PAGE_UF2:
        draw_uf2_page(widget);
        break;
    case TOUCAN_DISPLAY_PAGE_MEMORY_SET:
        draw_memory_set_page(widget);
        break;
    }
}

static void animation_work_handler(struct k_work *work);
static void page_request_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(animation_work, animation_work_handler);
K_WORK_DEFINE(page_request_work, page_request_work_handler);
K_MUTEX_DEFINE(page_request_mutex);
K_SEM_DEFINE(page_transition_done, 0, 1);

static enum toucan_display_page requested_page;
static bool requested_page_signal;
static atomic_t display_ready;

static bool active_profile_unresolved(const struct zmk_widget_screen *widget) {
    uint8_t profile = MIN(widget->state.active_profile_index, TOUCAN_BT_PROFILE_COUNT - 1);
    return !widget->state.selected_usb &&
           (!widget->state.profiles_bonded[profile] ||
            !widget->state.active_profile_connected);
}

static bool any_battery_low(const struct zmk_widget_screen *widget) {
    return widget->state.battery_left <= LOW_BATTERY_PERCENT ||
           (widget->state.right_connected &&
            widget->state.battery_right <= LOW_BATTERY_PERCENT);
}

static void refresh_sustained_animations(struct zmk_widget_screen *widget) {
    bool dashboard = effective_page(widget) == TOUCAN_DISPLAY_PAGE_DASHBOARD;
    int64_t now = k_uptime_get();
    bool pairing = dashboard && active_profile_unresolved(widget);
    bool low_battery = dashboard && any_battery_low(widget);

    if (pairing && !widget->animation.pairing_blink_active) {
        widget->animation.pairing_blink_active = true;
        widget->animation.pairing_marker_visible = true;
        widget->animation.pairing_due = now + PAIRING_BLINK_MS;
    } else if (!pairing) {
        widget->animation.pairing_blink_active = false;
        widget->animation.pairing_marker_visible = true;
    }

    if (low_battery && !widget->animation.low_battery_blink_active) {
        widget->animation.low_battery_blink_active = true;
        widget->animation.low_battery_fill_visible = true;
        widget->animation.low_battery_due = now + LOW_BATTERY_BLINK_MS;
    } else if (!low_battery) {
        widget->animation.low_battery_blink_active = false;
        widget->animation.low_battery_fill_visible = true;
    }
}

static void schedule_next_animation(void) {
    int64_t next_due = INT64_MAX;
    struct zmk_widget_screen *widget;

#define CONSIDER_DUE(active, due)                                                                 \
    do {                                                                                          \
        if ((active) && (due) < next_due) {                                                       \
            next_due = (due);                                                                     \
        }                                                                                         \
    } while (false)

    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        CONSIDER_DUE(widget->animation.transition_active, widget->animation.transition_due);
        CONSIDER_DUE(widget->animation.boot_to_dashboard_pending,
                     widget->animation.boot_to_dashboard_due);

        if (!widget->animation.transition_active &&
            widget->animation.page == TOUCAN_DISPLAY_PAGE_DASHBOARD) {
            CONSIDER_DUE(widget->animation.pairing_blink_active,
                         widget->animation.pairing_due);
            CONSIDER_DUE(widget->animation.low_battery_blink_active,
                         widget->animation.low_battery_due);
            CONSIDER_DUE(widget->animation.local_bolt_blink_active,
                         widget->animation.local_bolt_due);
            CONSIDER_DUE(widget->animation.right_bolt_blink_active,
                         widget->animation.right_bolt_due);
            CONSIDER_DUE(widget->animation.memory_flash_active,
                         widget->animation.memory_flash_due);
        }

        if (!widget->animation.transition_active &&
            widget->animation.page == TOUCAN_DISPLAY_PAGE_MEMORY_SET) {
            CONSIDER_DUE(true, widget->animation.caret_due);
        }
    }

#undef CONSIDER_DUE

    if (next_due == INT64_MAX) {
        (void)k_work_cancel_delayable(&animation_work);
        return;
    }

    int64_t delay = MAX(0, next_due - k_uptime_get());
    (void)k_work_reschedule_for_queue(zmk_display_work_q(), &animation_work, K_MSEC(delay));
}

static void start_memory_flash(struct zmk_widget_screen *widget, int slot) {
    if (slot < 0 || slot >= TOUCAN_MEMORY_SLOT_COUNT) {
        return;
    }

    widget->animation.pending_memory_flash_slot = -1;
    widget->animation.memory_flash_active = true;
    widget->animation.memory_flash_inverted = false;
    widget->animation.memory_flash_slot = slot;
    widget->animation.memory_flash_frame = 0U;
    widget->animation.memory_flash_due = k_uptime_get() + MEMORY_FLASH_FRAME_MS;
}

static void finish_page_transition(struct zmk_widget_screen *widget) {
    widget->animation.page = widget->animation.transition_target;
    widget->animation.transition_active = false;
    widget->animation.transition_frame = 0U;
    widget->animation.transition_revealed_rows = SCREEN_HEIGHT;

    if (widget->animation.page == TOUCAN_DISPLAY_PAGE_DASHBOARD) {
        refresh_sustained_animations(widget);
        if (widget->animation.pending_memory_flash_slot >= 0) {
            start_memory_flash(widget, widget->animation.pending_memory_flash_slot);
        }
    } else if (widget->animation.page == TOUCAN_DISPLAY_PAGE_MEMORY_SET) {
        widget->animation.caret_visible = true;
        widget->animation.caret_due = k_uptime_get() + CARET_BLINK_MS;
    }

    if (widget->animation.transition_signal) {
        widget->animation.transition_signal = false;
        k_sem_give(&page_transition_done);
    }
}

static void start_page_transition(struct zmk_widget_screen *widget,
                                  enum toucan_display_page target, bool signal) {
    widget->animation.boot_to_dashboard_pending = false;
    widget->animation.pairing_blink_active = false;
    widget->animation.low_battery_blink_active = false;
    widget->animation.local_bolt_blink_active = false;
    widget->animation.right_bolt_blink_active = false;
    widget->animation.local_bolt_visible = true;
    widget->animation.right_bolt_visible = true;
    widget->animation.memory_flash_active = false;

    if (!widget->animation.transition_active && widget->animation.page == target) {
        draw_scene(widget, target);
        lv_obj_invalidate(widget->obj);
        if (signal) {
            k_sem_give(&page_transition_done);
        }
        return;
    }

    widget->animation.transition_target = target;
    widget->animation.transition_active = true;
    widget->animation.transition_signal = signal;
    widget->animation.transition_frame = 0U;
    widget->animation.transition_revealed_rows = 0U;
    widget->animation.transition_due = k_uptime_get() + PAGE_TRANSITION_FRAME_MS;
    draw_scene(widget, target);
    schedule_next_animation();
}

static void page_request_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    k_mutex_lock(&page_request_mutex, K_FOREVER);
    enum toucan_display_page page = requested_page;
    bool signal = requested_page_signal;
    requested_page_signal = false;
    k_mutex_unlock(&page_request_mutex);

    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        if (widget->ready) {
            start_page_transition(widget, page, signal);
            return;
        }
    }

    if (signal) {
        k_sem_give(&page_transition_done);
    }
}

static void request_page_async(enum toucan_display_page page) {
    k_mutex_lock(&page_request_mutex, K_FOREVER);
    requested_page = page;
    requested_page_signal = false;
    k_mutex_unlock(&page_request_mutex);
    (void)k_work_submit_to_queue(zmk_display_work_q(), &page_request_work);
}

static void request_page_sync(enum toucan_display_page page) {
    k_sem_reset(&page_transition_done);
    k_mutex_lock(&page_request_mutex, K_FOREVER);
    requested_page = page;
    requested_page_signal = true;
    k_mutex_unlock(&page_request_mutex);

    int result = k_work_submit_to_queue(zmk_display_work_q(), &page_request_work);
    if (result >= 0) {
        if (k_sem_take(&page_transition_done,
                       K_MSEC(PAGE_TRANSITION_TOTAL_MS + PAGE_TRANSITION_GUARD_MS + 200)) == 0) {
            /* Let the next LVGL tick flush the final revealed row block before deep sleep. */
            k_sleep(K_MSEC(PAGE_TRANSITION_GUARD_MS));
        }
    }
}

uint32_t toucan_display_prepare_soft_off(void) {
    if (atomic_get(&display_ready) == 0) {
        return 0U;
    }

    request_page_async(TOUCAN_DISPLAY_PAGE_SOFT_OFF);
    return PAGE_TRANSITION_TOTAL_MS + PAGE_TRANSITION_GUARD_MS;
}

void toucan_display_cancel_soft_off(void) {
    if (atomic_get(&display_ready) != 0) {
        request_page_async(TOUCAN_DISPLAY_PAGE_DASHBOARD);
    }
}

uint32_t toucan_display_prepare_uf2(void) {
    if (atomic_get(&display_ready) == 0) {
        return 0U;
    }

    request_page_async(TOUCAN_DISPLAY_PAGE_UF2);
    return PAGE_TRANSITION_TOTAL_MS + PAGE_TRANSITION_GUARD_MS;
}

static void update_bolt_animation(struct zmk_widget_screen *widget, bool right) {
    bool *active = right ? &widget->animation.right_bolt_blink_active
                         : &widget->animation.local_bolt_blink_active;
    bool *visible = right ? &widget->animation.right_bolt_visible
                          : &widget->animation.local_bolt_visible;
    uint8_t *frame = right ? &widget->animation.right_bolt_frame
                           : &widget->animation.local_bolt_frame;
    int64_t *due = right ? &widget->animation.right_bolt_due
                         : &widget->animation.local_bolt_due;

    (*frame)++;
    *visible = (*frame >= BOLT_BLINK_FRAMES) || ((*frame & 1U) != 0U);
    if (*frame >= BOLT_BLINK_FRAMES) {
        *active = false;
        *visible = true;
    } else {
        *due += BOLT_BLINK_FRAME_MS;
    }

    draw_power_band(widget);
    invalidate_rows(widget, 7, 12);
}

static void animation_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    int64_t now = k_uptime_get();
    struct zmk_widget_screen *widget;

    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        if (widget->animation.boot_to_dashboard_pending &&
            now >= widget->animation.boot_to_dashboard_due) {
            start_page_transition(widget, TOUCAN_DISPLAY_PAGE_DASHBOARD, false);
        }

        if (widget->animation.transition_active && now >= widget->animation.transition_due) {
            widget->animation.transition_frame++;
            uint16_t rows = (widget->animation.transition_frame * SCREEN_HEIGHT +
                             PAGE_TRANSITION_FRAMES / 2) /
                            PAGE_TRANSITION_FRAMES;
            rows = MIN(rows, SCREEN_HEIGHT);
            invalidate_rows(widget, widget->animation.transition_revealed_rows,
                            rows - widget->animation.transition_revealed_rows);
            widget->animation.transition_revealed_rows = rows;

            if (widget->animation.transition_frame >= PAGE_TRANSITION_FRAMES) {
                finish_page_transition(widget);
            } else {
                widget->animation.transition_due += PAGE_TRANSITION_FRAME_MS;
            }
        }

        if (widget->animation.transition_active) {
            continue;
        }

        if (widget->animation.page == TOUCAN_DISPLAY_PAGE_DASHBOARD) {
            if (widget->animation.pairing_blink_active &&
                now >= widget->animation.pairing_due) {
                widget->animation.pairing_marker_visible =
                    !widget->animation.pairing_marker_visible;
                widget->animation.pairing_due += PAIRING_BLINK_MS;
                draw_bt_band(widget);
                invalidate_rows(widget, 98, 2);
            }
            if (widget->animation.low_battery_blink_active &&
                now >= widget->animation.low_battery_due) {
                widget->animation.low_battery_fill_visible =
                    !widget->animation.low_battery_fill_visible;
                widget->animation.low_battery_due += LOW_BATTERY_BLINK_MS;
                draw_power_band(widget);
                invalidate_rows(widget, 25, 8);
            }
            if (widget->animation.local_bolt_blink_active &&
                now >= widget->animation.local_bolt_due) {
                update_bolt_animation(widget, false);
            }
            if (widget->animation.right_bolt_blink_active &&
                now >= widget->animation.right_bolt_due) {
                update_bolt_animation(widget, true);
            }
            if (widget->animation.memory_flash_active &&
                now >= widget->animation.memory_flash_due) {
                widget->animation.memory_flash_frame++;
                widget->animation.memory_flash_inverted =
                    (widget->animation.memory_flash_frame & 1U) != 0U;
                if (widget->animation.memory_flash_frame >= MEMORY_FLASH_FRAMES) {
                    widget->animation.memory_flash_active = false;
                    widget->animation.memory_flash_inverted = false;
                } else {
                    widget->animation.memory_flash_due += MEMORY_FLASH_FRAME_MS;
                }
                draw_memory_band(widget);
                invalidate_rows(widget, MEMORY_Y, 16);
            }
        } else if (widget->animation.page == TOUCAN_DISPLAY_PAGE_MEMORY_SET &&
                   now >= widget->animation.caret_due) {
            widget->animation.caret_visible = !widget->animation.caret_visible;
            widget->animation.caret_due += CARET_BLINK_MS;
            draw_scene(widget, TOUCAN_DISPLAY_PAGE_MEMORY_SET);
            invalidate_rows(widget, 36, 124);
        }
    }

    schedule_next_animation();
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
        refresh_sustained_animations(widget);
        render_bands(widget, DASHBOARD_BAND_POWER);
        schedule_next_animation();
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
        refresh_sustained_animations(widget);
        render_bands(widget, DASHBOARD_BAND_POWER);
        schedule_next_animation();
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
        bool usb_power_appeared = widget->ready && !widget->state.usb_powered &&
                                  state.usb_powered;

        widget->state.selected_usb = state.selected_usb;
        widget->state.usb_powered = state.usb_powered;
        widget->state.usb_hid_ready = state.usb_hid_ready;
        widget->state.active_profile_index = state.active_profile_index;
        widget->state.active_profile_connected = state.active_profile_connected;
        memcpy(widget->state.profiles_bonded, state.profiles_bonded,
               sizeof(widget->state.profiles_bonded));

        if (usb_power_appeared && effective_page(widget) == TOUCAN_DISPLAY_PAGE_DASHBOARD) {
            widget->animation.local_bolt_blink_active = true;
            widget->animation.local_bolt_visible = false;
            widget->animation.local_bolt_frame = 0U;
            widget->animation.local_bolt_due = k_uptime_get() + BOLT_BLINK_FRAME_MS;
        } else if (!state.usb_powered) {
            widget->animation.local_bolt_blink_active = false;
            widget->animation.local_bolt_visible = true;
        }

        refresh_sustained_animations(widget);
        render_bands(widget, DASHBOARD_BAND_POWER | DASHBOARD_BAND_BT);
        schedule_next_animation();
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
        uint8_t previous = widget->state.layer_index;
        widget->state.layer_index = state.index;

        if (!widget->ready || previous == state.index) {
            continue;
        }

        if (effective_page(widget) != TOUCAN_DISPLAY_PAGE_DASHBOARD) {
            continue;
        }

        draw_layer_band(widget);
        if (!widget->animation.transition_active) {
            invalidate_rows(widget, LAYER_Y, LAYER_HEIGHT);
        }
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
        bool usb_power_appeared = widget->ready && !widget->state.right_usb_powered &&
                                  state.usb_powered;

        widget->state.right_connected = state.connected;
        widget->state.right_usb_powered = state.usb_powered;

        if (usb_power_appeared && effective_page(widget) == TOUCAN_DISPLAY_PAGE_DASHBOARD) {
            widget->animation.right_bolt_blink_active = true;
            widget->animation.right_bolt_visible = false;
            widget->animation.right_bolt_frame = 0U;
            widget->animation.right_bolt_due = k_uptime_get() + BOLT_BLINK_FRAME_MS;
        } else if (!state.usb_powered) {
            widget->animation.right_bolt_blink_active = false;
            widget->animation.right_bolt_visible = true;
        }

        refresh_sustained_animations(widget);
        render_bands(widget, DASHBOARD_BAND_POWER);
        schedule_next_animation();
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
        if (effective_page(widget) == TOUCAN_DISPLAY_PAGE_MEMORY_SET) {
            draw_scene(widget, TOUCAN_DISPLAY_PAGE_MEMORY_SET);
            invalidate_rows(widget, 36, 124);
        } else {
            render_bands(widget, DASHBOARD_BAND_SYSTEM);
        }
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(toucan_display_platform, struct platform_state, platform_update_cb,
                            platform_get_state);
ZMK_SUBSCRIPTION(toucan_display_platform, toucan_platform_mode_changed);

static struct toucan_memory_state_changed memory_get_state(const zmk_event_t *eh) {
    const struct toucan_memory_state_changed *event =
        eh != NULL ? as_toucan_memory_state_changed(eh) : NULL;
    return event != NULL ? *event : (struct toucan_memory_state_changed){.slot = -1};
}

static void memory_update_cb(struct toucan_memory_state_changed state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        bool was_capturing = widget->state.memory.capture.active;
        /* The listener macro copies its state by value on both workqueues.
         * Carry only the small notification there; fetch the large snapshot
         * directly into display-owned storage, never onto either thread's stack. */
        toucan_memory_get_snapshot(&widget->state.memory);
        bool capturing = widget->state.memory.capture.active;

        if (!widget->ready) {
            continue;
        }

        if (!was_capturing && capturing) {
            start_page_transition(widget, TOUCAN_DISPLAY_PAGE_MEMORY_SET, false);
        } else if (was_capturing && !capturing) {
            widget->animation.pending_memory_flash_slot = state.saved ? state.slot : -1;
            start_page_transition(widget, TOUCAN_DISPLAY_PAGE_DASHBOARD, false);
        } else if (capturing && effective_page(widget) == TOUCAN_DISPLAY_PAGE_MEMORY_SET) {
            draw_scene(widget, TOUCAN_DISPLAY_PAGE_MEMORY_SET);
            invalidate_rows(widget, 0, SCREEN_HEIGHT);
        } else {
            render_bands(widget, DASHBOARD_BAND_MEMORY);
            if (state.saved) {
                start_memory_flash(widget, state.slot);
            }
        }

        schedule_next_animation();
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(toucan_display_memory, struct toucan_memory_state_changed,
                            memory_update_cb, memory_get_state);
ZMK_SUBSCRIPTION(toucan_display_memory, toucan_memory_state_changed);

static int display_activity_event_handler(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *event = as_zmk_activity_state_changed(eh);
    if (event == NULL) {
        return -ENOTSUP;
    }

    if (event->state == ZMK_ACTIVITY_SLEEP) {
        request_page_sync(TOUCAN_DISPLAY_PAGE_SLEEP);
    } else if (event->state == ZMK_ACTIVITY_ACTIVE) {
        struct zmk_widget_screen *widget;
        SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
            if (widget->ready && effective_page(widget) == TOUCAN_DISPLAY_PAGE_SLEEP) {
                request_page_async(widget->state.memory.capture.active
                                       ? TOUCAN_DISPLAY_PAGE_MEMORY_SET
                                       : TOUCAN_DISPLAY_PAGE_DASHBOARD);
                break;
            }
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(toucan_display_activity, display_activity_event_handler);
ZMK_SUBSCRIPTION(toucan_display_activity, zmk_activity_state_changed);

int zmk_widget_screen_init(struct zmk_widget_screen *widget, lv_obj_t *parent) {
    memset(&widget->state, 0, sizeof(widget->state));
    memset(&widget->animation, 0, sizeof(widget->animation));

    widget->obj = lv_canvas_create(parent);
    lv_canvas_set_buffer(widget->obj, widget->cbuf, SCREEN_WIDTH, SCREEN_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(widget->obj, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    widget->animation.page = TOUCAN_DISPLAY_PAGE_BOOT;
    widget->animation.transition_target = TOUCAN_DISPLAY_PAGE_BOOT;
    widget->animation.pairing_marker_visible = true;
    widget->animation.low_battery_fill_visible = true;
    widget->animation.local_bolt_visible = true;
    widget->animation.right_bolt_visible = true;
    widget->animation.memory_flash_slot = -1;
    widget->animation.pending_memory_flash_slot = -1;
    widget->animation.caret_visible = true;

    sys_slist_append(&widgets, &widget->node);
    toucan_display_local_battery_init();
    toucan_display_peripheral_battery_init();
    toucan_display_host_init();
    toucan_display_layer_init();
    toucan_display_split_status_init();
    toucan_display_caps_init();
    toucan_display_platform_init();
    toucan_display_memory_init();

    widget->ready = true;
    atomic_set(&display_ready, 1);
    draw_scene(widget, TOUCAN_DISPLAY_PAGE_BOOT);
    lv_obj_invalidate(widget->obj);
    widget->animation.boot_to_dashboard_pending = true;
    widget->animation.boot_to_dashboard_due = k_uptime_get() + BOOT_HOLD_MS;
    schedule_next_animation();
    return 0;
}

lv_obj_t *zmk_widget_screen_obj(struct zmk_widget_screen *widget) {
    return widget->obj;
}
