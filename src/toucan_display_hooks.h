/* Display pages requested by firmware actions outside the display module. */

#pragma once

#include <stdint.h>

/* Return the delay required to finish the retained-image transition. */
uint32_t toucan_display_prepare_soft_off(void);
void toucan_display_cancel_soft_off(void);
uint32_t toucan_display_prepare_uf2(void);
