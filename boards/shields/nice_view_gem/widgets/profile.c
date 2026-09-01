#include <zephyr/kernel.h>
#include "profile.h"
#include "profile_draw.h"

#define BT_PROFILE_START_X 85
#define BT_PROFILE_Y       143
#define BT_PROFILE_SIZE    8
#define BT_PROFILE_SPACING 10

void draw_profile_status(lv_obj_t *canvas, const struct status_state *state) {
    for (int i = 0; i < TOUCAN_BT_PROFILE_COUNT; i++) {
        draw_profile_slot(canvas, i, BT_PROFILE_START_X + i * BT_PROFILE_SPACING, BT_PROFILE_Y,
                          BT_PROFILE_SIZE, i == state->active_profile_index);
    }
}
