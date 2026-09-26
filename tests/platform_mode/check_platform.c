/*
 * Native ZMK regression test for platform-aware Command key bindings.
 * This test uses generated input only; never enable it on a real keyboard.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/sys/util.h>

#include <dt-bindings/zmk/keys.h>
#include <zmk/events/keycode_state_changed.h>

#include "toucan_platform_mode.h"

/* Independent expected encodings, not the production modifier-combining expression. */
static const uint32_t expected[] = {
    /* Undo, Redo, mixed left/right modifiers, an existing GUI bit, consumer usage,
     * and the three modifier-only forms used by thumb/layer bindings. */
    LG(Z), LG(LS(Z)), LG(LA(RS(N4))), LG(Z), LG(C_VOL_UP), LGUI, LGUI, RGUI,
    LC(Z), LC(LS(Z)), LC(LA(RS(N4))), LC(LG(Z)), LC(C_VOL_UP), LCTRL, LCTRL, RCTRL,
    /* Mode changed while held; then a fresh press in the new mode. */
    LG(LS(Z)), LC(LS(Z)),
};
static size_t event_count;

static int shortcut_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *event = as_zmk_keycode_state_changed(eh);

    if (event->usage_page == HID_USAGE_KEY && event->keycode == ZMK_HID_USAGE_ID(F12)) {
        assert(event_count == 2U * ARRAY_SIZE(expected));
        assert(toucan_platform_is_windows());
        if (event->state) {
            puts("PASS: platform shortcuts preserve keycodes, usage pages, modifiers, "
                 "and paired releases in macOS/Windows modes");
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    assert(event_count < 2U * ARRAY_SIZE(expected));
    uint32_t encoded = expected[event_count / 2U];
    assert(event->usage_page == ZMK_HID_USAGE_PAGE(encoded));
    assert(event->keycode == ZMK_HID_USAGE_ID(encoded));
    assert(event->implicit_modifiers == SELECT_MODS(encoded));
    assert(event->explicit_modifiers == 0U);
    assert(event->state == ((event_count % 2U) == 0U));
    event_count++;
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(toucan_test_shortcuts, shortcut_listener);
ZMK_SUBSCRIPTION(toucan_test_shortcuts, zmk_keycode_state_changed);
