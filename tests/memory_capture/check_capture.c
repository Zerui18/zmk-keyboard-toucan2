/*
 * Native ZMK integration test: private capture -> SYS save -> slot playback.
 * This test uses generated input only; never enable it on a real keyboard.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zmk/events/keycode_state_changed.h>

#include "toucan_key_text.h"
#include "toucan_memory.h"

static const struct {
    uint16_t keycode;
    uint8_t modifiers;
    uint8_t order[TOUCAN_MEMORY_MODIFIER_COUNT];
} named_shortcuts[] = {
    {HID_USAGE_KEY_KEYBOARD_P, MOD_LCTL | MOD_LSFT, {MOD_LCTL, MOD_LSFT}},
    {HID_USAGE_KEY_KEYBOARD_S, MOD_LCTL, {MOD_LCTL}},
    {HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD, 0, {0}},
    {HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD, MOD_LGUI, {MOD_LGUI}},
    {HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD, MOD_LSFT | MOD_LGUI, {MOD_LSFT, MOD_LGUI}},
    {HID_USAGE_KEY_KEYBOARD_A, 0, {0}},
};

static const struct {
    const char *preview;
    const char *playback;
    size_t playback_events;
} expected[] = {
    {"aABcAactrldel", "aABcAactrldel", 26},
    {"aABcAa", "aABcAa", 20},
    {"psa", "Psa", 2 * ARRAY_SIZE(named_shortcuts)},
    {"", "", 2},
    {"44", "$$", 4},
};
static unsigned int saved_count;
static size_t replay_length;
static size_t replay_events;
static bool complete_capture[ARRAY_SIZE(expected)];
static bool saw_live_ctrl;
static bool saw_live_del;
static bool saw_live_shift_cmd;
static bool saw_live_cmd_shift;
static bool saw_overflow;
static bool cancelled_overflow;
static struct toucan_key_text_state replay_state;

static void verify_replay(void) {
    assert(saved_count > 0U && saved_count <= ARRAY_SIZE(expected));
    assert(replay_length == strlen(expected[saved_count - 1U].playback));
    assert(replay_events == expected[saved_count - 1U].playback_events);
    assert(!replay_state.caps_lock);
    for (size_t i = 0; i < ARRAY_SIZE(replay_state.modifier_counts); i++) {
        assert(replay_state.modifier_counts[i] == 0U);
    }
}

static bool is_ctrl_token(const struct toucan_memory_capture_snapshot *capture) {
    return capture->sequence_action_count == 2 &&
           capture->sequence[0].keycode == HID_USAGE_KEY_KEYBOARD_LEFTCONTROL &&
           capture->sequence[0].pressed && !capture->sequence[1].pressed &&
           capture->sequence[1].keycode == HID_USAGE_KEY_KEYBOARD_LEFTCONTROL;
}

static int capture_listener(const zmk_event_t *eh) {
    const struct toucan_memory_state_changed *event = as_toucan_memory_state_changed(eh);
    struct toucan_memory_snapshot snapshot;
    toucan_memory_get_snapshot(&snapshot);

    if (event->saved) {
        assert(saved_count < ARRAY_SIZE(expected) && complete_capture[saved_count]);
        if (saved_count > 0U) {
            verify_replay();
        }
        saved_count++;
        replay_length = 0U;
        replay_events = 0U;
        memset(&replay_state, 0, sizeof(replay_state));
    }
    if (!snapshot.capture.active) {
        if (!event->saved && event->slot == 3) {
            assert(saw_overflow && saved_count == 3U);
            assert(snapshot.slot_types[3] == TOUCAN_MEMORY_SLOT_EMPTY);
            cancelled_overflow = true;
        }
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (snapshot.capture.sequence_overflow) {
        assert(snapshot.capture.slot == 3 && saved_count == 3U);
        saw_overflow = true;
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (snapshot.capture.slot == 2 && is_ctrl_token(&snapshot.capture)) {
        saw_live_ctrl = true;
    }
    if (snapshot.capture.slot == 2 && snapshot.capture.sequence_action_count >= 6 &&
        snapshot.capture.sequence[4].keycode == HID_USAGE_KEY_KEYBOARD_DELETE_FORWARD &&
        snapshot.capture.sequence[4].pressed && !snapshot.capture.sequence[5].pressed) {
        saw_live_del = true;
    }
    if (snapshot.capture.slot == 4) {
        const struct toucan_memory_sequence_action *sequence = snapshot.capture.sequence;
        if (snapshot.capture.sequence_action_count == 4 &&
            sequence[0].keycode == HID_USAGE_KEY_KEYBOARD_LEFTSHIFT && sequence[0].pressed &&
            sequence[1].keycode == HID_USAGE_KEY_KEYBOARD_LEFT_GUI && sequence[1].pressed) {
            saw_live_shift_cmd = true;
        }
        if (snapshot.capture.sequence_action_count == 6 &&
            sequence[2].keycode == HID_USAGE_KEY_KEYBOARD_LEFT_GUI && sequence[2].pressed &&
            sequence[3].keycode == HID_USAGE_KEY_KEYBOARD_LEFTSHIFT && sequence[3].pressed) {
            saw_live_cmd_shift = true;
        }
    }
    if (snapshot.capture.slot == 3) {
        if (cancelled_overflow && is_ctrl_token(&snapshot.capture)) {
            complete_capture[3] = true;
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    char preview[TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY + 1] = {0};
    if (snapshot.capture.mode == TOUCAN_MEMORY_CAPTURE_TEXT) {
        memcpy(preview, snapshot.capture.text, sizeof(preview));
    } else {
        struct toucan_key_text_state state = {
            .caps_lock = snapshot.capture.initial_caps_lock,
        };
        size_t length = 0U;
        for (size_t i = 0; i < snapshot.capture.sequence_action_count; i++) {
            const struct toucan_memory_sequence_action *action = &snapshot.capture.sequence[i];
            char character;
            if (toucan_key_text_apply(&state, action->usage_page, action->keycode,
                                     snapshot.capture.sequence_preview[i].implicit_modifiers,
                                     action->explicit_modifiers,
                                     action->pressed, &character)) {
                preview[length++] = character;
            }
        }
    }
    const char *wanted = expected[snapshot.capture.slot].preview;
    if (snapshot.capture.slot < 2) {
        assert(strncmp(preview, wanted, strlen(preview)) == 0);
    }
    if (strcmp(preview, wanted) == 0) {
        if (snapshot.capture.slot == 2) {
            assert(saw_live_del);
            assert(snapshot.capture.sequence_action_count <= 2 * ARRAY_SIZE(named_shortcuts));
            for (size_t i = 0; i < snapshot.capture.sequence_action_count; i++) {
                const struct toucan_memory_sequence_action *action = &snapshot.capture.sequence[i];
                assert(action->usage_page == HID_USAGE_KEY);
                assert(action->keycode == named_shortcuts[i / 2].keycode);
                assert(action->implicit_modifiers == named_shortcuts[i / 2].modifiers);
                assert(action->explicit_modifiers == 0U);
                assert(action->pressed == ((i % 2U) == 0U));
                assert(memcmp(snapshot.capture.sequence_preview[i].modifier_order,
                              named_shortcuts[i / 2].order, TOUCAN_MEMORY_MODIFIER_COUNT) == 0);
            }
        }
        if (snapshot.capture.slot == 4) {
            assert(saw_live_shift_cmd && saw_live_cmd_shift);
            for (size_t i = 0; i < snapshot.capture.sequence_action_count; i++) {
                const uint8_t *order = snapshot.capture.sequence_preview[i].modifier_order;
                assert(order[0] == (i < 2 ? MOD_LSFT : MOD_LGUI));
                assert(order[1] == (i < 2 ? MOD_LGUI : MOD_LSFT));
                assert(order[2] == 0U && order[3] == 0U);
                assert(snapshot.capture.sequence[i].keycode == HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR);
                assert(snapshot.capture.sequence[i].implicit_modifiers == (MOD_LSFT | MOD_LGUI));
            }
        }
        complete_capture[snapshot.capture.slot] = true;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int playback_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *event = as_zmk_keycode_state_changed(eh);
    struct toucan_memory_snapshot snapshot;
    toucan_memory_get_snapshot(&snapshot);

    /* This listener is downstream of memory capture, just like ZMK's HID listener. */
    assert(!snapshot.capture.active);
    if (event->keycode == HID_USAGE_KEY_KEYBOARD_F12) {
        if (event->state) {
            assert(saved_count == ARRAY_SIZE(expected) && saw_live_ctrl && cancelled_overflow);
            verify_replay();
            assert(snapshot.slot_types[0] == TOUCAN_MEMORY_SLOT_TEXT);
            assert(snapshot.slot_types[1] == TOUCAN_MEMORY_SLOT_SEQUENCE);
            assert(snapshot.slot_types[2] == TOUCAN_MEMORY_SLOT_SEQUENCE);
            assert(snapshot.slot_types[3] == TOUCAN_MEMORY_SLOT_SEQUENCE);
            assert(snapshot.slot_types[4] == TOUCAN_MEMORY_SLOT_SEQUENCE);
            puts("PASS: private TEXT/SEQ capture, del shorthand, ordered modifier previews, "
                 "shortcut playback, and overflow guards");
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    assert(saved_count > 0U && saved_count <= ARRAY_SIZE(expected));
    if (saved_count == 3U) {
        assert(replay_events < 2 * ARRAY_SIZE(named_shortcuts));
        assert(event->usage_page == HID_USAGE_KEY);
        assert(event->keycode == named_shortcuts[replay_events / 2].keycode);
        assert(event->implicit_modifiers == named_shortcuts[replay_events / 2].modifiers);
        assert(event->explicit_modifiers == 0U);
        assert(event->state == ((replay_events % 2U) == 0U));
    } else if (saved_count == 4U) {
        assert(replay_events < 2U);
        assert(event->keycode == HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
        assert(event->state == (replay_events == 0U));
    } else if (saved_count == 5U) {
        assert(replay_events < 4U);
        assert(event->keycode == HID_USAGE_KEY_KEYBOARD_4_AND_DOLLAR);
        assert(event->implicit_modifiers == (MOD_LSFT | MOD_LGUI));
        assert(event->explicit_modifiers == 0U);
        assert(event->state == ((replay_events % 2U) == 0U));
    }
    replay_events++;
    char character;
    if (toucan_key_text_apply(&replay_state, event->usage_page, event->keycode,
                             event->implicit_modifiers, event->explicit_modifiers,
                             event->state, &character)) {
        assert(replay_length < strlen(expected[saved_count - 1U].playback));
        assert(character == expected[saved_count - 1U].playback[replay_length++]);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(toucan_test_capture, capture_listener);
ZMK_SUBSCRIPTION(toucan_test_capture, toucan_memory_state_changed);
ZMK_LISTENER(toucan_test_playback, playback_listener);
ZMK_SUBSCRIPTION(toucan_test_playback, zmk_keycode_state_changed);
