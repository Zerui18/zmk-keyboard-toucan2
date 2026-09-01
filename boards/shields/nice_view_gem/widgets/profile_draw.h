#pragma once

#include <stdbool.h>

#include <lvgl.h>
#include <zmk/ble.h>

#include "util.h"

#define TOUCAN_BT_PROFILE_COUNT 5

static inline void draw_profile_filled_square(lv_obj_t *canvas, int x, int y, int size,
                                               lv_color_t color) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.border_width = 0;
    dsc.radius = 0;
    lv_canvas_draw_rect(canvas, x, y, size, size, &dsc);
}

static inline void draw_profile_solid_outline(lv_obj_t *canvas, int x, int y, int size,
                                               lv_color_t bg, lv_color_t border) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = bg;
    dsc.border_color = border;
    dsc.border_width = 1;
    dsc.radius = 2;
    lv_canvas_draw_rect(canvas, x, y, size, size, &dsc);
}

static inline void draw_profile_dotted_outline(lv_obj_t *canvas, int x, int y, int size,
                                                lv_color_t color) {
    for (int offset = 0; offset < size; offset += 2) {
        lv_canvas_set_px_color(canvas, x + offset, y, color);
        lv_canvas_set_px_color(canvas, x + offset, y + size - 1, color);
        lv_canvas_set_px_color(canvas, x, y + offset, color);
        lv_canvas_set_px_color(canvas, x + size - 1, y + offset, color);
    }
}

static inline void draw_profile_slot(lv_obj_t *canvas, int profile, int x, int y, int size,
                                     bool active) {
    const lv_color_t bg = LVGL_BACKGROUND;
    const lv_color_t fg = LVGL_FOREGROUND;
    const bool empty = zmk_ble_profile_is_open(profile);

    if (!empty && active) {
        draw_profile_filled_square(canvas, x, y, size, fg);
    } else if (!empty) {
        draw_profile_solid_outline(canvas, x, y, size, bg, fg);
    } else {
        draw_profile_dotted_outline(canvas, x, y, size, fg);

        /* Preserve the active-slot cue without hiding that the slot is empty. */
        if (active) {
            draw_profile_filled_square(canvas, x + (size - 2) / 2, y + (size - 2) / 2, 2, fg);
        }
    }
}
