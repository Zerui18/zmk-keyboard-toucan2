#include <zephyr/kernel.h>
#include "profile_arc.h"
#include "profile_draw.h"

#define BT_PROFILE_X       126
#define BT_PROFILE_START_Y 105
#define BT_PROFILE_SIZE    8
#define BT_PROFILE_SPACING 10

void draw_profile_status(lv_obj_t *canvas, const struct status_state *state) {
    for (int i = 0; i < TOUCAN_BT_PROFILE_COUNT; i++) {
        draw_profile_slot(canvas, i, BT_PROFILE_X, BT_PROFILE_START_Y + i * BT_PROFILE_SPACING,
                          BT_PROFILE_SIZE, i == state->active_profile_index);
    }
}
