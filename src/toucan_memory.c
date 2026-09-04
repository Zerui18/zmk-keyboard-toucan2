/*
 * Persistent five-slot sequence/text memory for the Toucan2.
 *
 * Copyright (c) 2026 Zerui Chen
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid_indicators.h>
#include <zmk/keys.h>

#include "toucan_memory.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define MEMORY_SETTINGS_PREFIX "toucan_mem"
#define MEMORY_SETTINGS_VERSION 3U
#define SYS_THUMB_POSITION 36U
#define SYS_HOLD_TO_SAVE_MS 600
#define SYS_DOUBLE_TAP_MS 275
#define PLAYBACK_QUEUE_DEPTH TOUCAN_MEMORY_SLOT_COUNT
#define PLAYBACK_THREAD_STACK_SIZE 2048
#define PLAYBACK_ACTION_GAP_MS 8
#define PLAYBACK_TEXT_PRESS_MS 12
#define PLAYBACK_TEXT_GAP_MS 8
#define SWALLOWED_ACTIVE_CAPACITY TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY

struct persisted_memory_slot {
    uint8_t version;
    uint8_t type;
    uint8_t sequence_action_count;
    uint8_t text_length;
    union {
        struct toucan_memory_sequence_action
            sequence[TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY];
        char text[TOUCAN_MEMORY_TEXT_CAPACITY];
    } content;
};

struct memory_capture {
    bool active;
    uint8_t slot;
    uint8_t mode;
    uint8_t sequence_action_count;
    struct toucan_memory_sequence_action
        sequence[TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY];
    char text[TOUCAN_MEMORY_TEXT_CAPACITY + 1];
};

struct swallowed_key {
    uint16_t usage_page;
    uint16_t keycode;
    uint16_t capture_generation;
};

static struct persisted_memory_slot slots[TOUCAN_MEMORY_SLOT_COUNT];
static struct memory_capture capture;
static struct swallowed_key swallowed_active[SWALLOWED_ACTIVE_CAPACITY];
static struct swallowed_key forwarded_active[SWALLOWED_ACTIVE_CAPACITY];
static uint8_t swallowed_active_count;
static uint8_t forwarded_active_count;
static uint16_t capture_generation;
static uint8_t held_modifiers;
static bool memory_clear_held;
static bool sys_thumb_pressed;
static bool initial_sys_release_pending;
static bool swallowed_sys_release;
static bool capture_sys_pressed;
static bool capture_sys_hold_fired;
static uint8_t capture_sys_tap_count;
static atomic_t playback_pending;

K_MUTEX_DEFINE(memory_mutex);
K_MSGQ_DEFINE(playback_queue, sizeof(uint8_t), PLAYBACK_QUEUE_DEPTH, 1);

static void sys_hold_work_handler(struct k_work *work);
static void sys_tap_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(sys_hold_work, sys_hold_work_handler);
K_WORK_DELAYABLE_DEFINE(sys_tap_work, sys_tap_work_handler);

ZMK_EVENT_IMPL(toucan_memory_state_changed);

static void wipe(void *data, size_t size) {
    volatile uint8_t *bytes = data;

    while (size-- > 0U) {
        *bytes++ = 0U;
    }
}

static void notify_memory_changed(int8_t slot, bool saved) {
    raise_toucan_memory_state_changed((struct toucan_memory_state_changed){
        .slot = slot,
        .saved = saved,
    });
}

static bool valid_slot(uint32_t slot) {
    return slot < TOUCAN_MEMORY_SLOT_COUNT;
}

static size_t bounded_length(const char *text, size_t capacity) {
    size_t length = 0U;

    while (length < capacity && text[length] != '\0') {
        length++;
    }

    return length;
}

static int persist_slot(uint8_t slot) {
    char key[24];
    struct persisted_memory_slot saved;

    k_mutex_lock(&memory_mutex, K_FOREVER);
    saved = slots[slot];
    k_mutex_unlock(&memory_mutex);

    int length = snprintf(key, sizeof(key), MEMORY_SETTINGS_PREFIX "/%u", slot);
    if (length < 0 || length >= (int)sizeof(key)) {
        wipe(&saved, sizeof(saved));
        return -ENOMEM;
    }

    int err = settings_save_one(key, &saved, sizeof(saved));
    wipe(&saved, sizeof(saved));
    return err;
}

static int memory_settings_set(const char *name, size_t length, settings_read_cb read_cb,
                               void *cb_arg) {
    if (name == NULL || name[0] < '0' || name[0] > '4' || name[1] != '\0' ||
        length != sizeof(struct persisted_memory_slot)) {
        return -ENOENT;
    }

    struct persisted_memory_slot loaded = {0};
    int err = read_cb(cb_arg, &loaded, sizeof(loaded));
    if (err < 0) {
        wipe(&loaded, sizeof(loaded));
        return err;
    }

    if (err != (int)sizeof(loaded) || loaded.version != MEMORY_SETTINGS_VERSION ||
        loaded.type > TOUCAN_MEMORY_SLOT_TEXT ||
        loaded.sequence_action_count > TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY ||
        loaded.text_length > TOUCAN_MEMORY_TEXT_CAPACITY) {
        wipe(&loaded, sizeof(loaded));
        return -EINVAL;
    }

    uint8_t slot = (uint8_t)(name[0] - '0');
    k_mutex_lock(&memory_mutex, K_FOREVER);
    slots[slot] = loaded;
    k_mutex_unlock(&memory_mutex);
    wipe(&loaded, sizeof(loaded));
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(toucan_memory, MEMORY_SETTINGS_PREFIX, NULL, memory_settings_set,
                               NULL, NULL);

void toucan_memory_get_snapshot(struct toucan_memory_snapshot *snapshot) {
    if (snapshot == NULL) {
        return;
    }

    k_mutex_lock(&memory_mutex, K_FOREVER);
    memset(snapshot, 0, sizeof(*snapshot));

    for (size_t i = 0; i < ARRAY_SIZE(slots); i++) {
        snapshot->slot_types[i] = slots[i].type;
    }

    snapshot->capture.active = capture.active;
    snapshot->capture.slot = capture.slot;
    snapshot->capture.mode = capture.mode;
    snapshot->capture.sequence_action_count = capture.sequence_action_count;
    memcpy(snapshot->capture.sequence, capture.sequence,
           sizeof(snapshot->capture.sequence));
    memcpy(snapshot->capture.text, capture.text, sizeof(snapshot->capture.text));
    k_mutex_unlock(&memory_mutex);
}

static bool capture_is_empty_locked(void) {
    return capture.mode == TOUCAN_MEMORY_CAPTURE_TEXT
               ? capture.text[0] == '\0'
               : capture.sequence_action_count == 0U;
}

static void reset_sys_gesture_locked(void) {
    capture_sys_pressed = false;
    capture_sys_hold_fired = false;
    capture_sys_tap_count = 0U;
    (void)k_work_cancel_delayable(&sys_hold_work);
    (void)k_work_cancel_delayable(&sys_tap_work);
}

static void begin_capture(uint8_t slot) {
    if (atomic_get(&playback_pending) != 0) {
        LOG_WRN("Memory playback is busy; ignoring capture request");
        return;
    }

    k_mutex_lock(&memory_mutex, K_FOREVER);
    if (capture.active) {
        k_mutex_unlock(&memory_mutex);
        return;
    }

    wipe(&capture, sizeof(capture));
    capture_generation++;
    if (capture_generation == 0U) {
        capture_generation = 1U;
    }
    capture.active = true;
    capture.slot = slot;
    capture.mode = TOUCAN_MEMORY_CAPTURE_SEQUENCE;
    held_modifiers = 0U;
    initial_sys_release_pending = sys_thumb_pressed;
    reset_sys_gesture_locked();
    k_mutex_unlock(&memory_mutex);

    LOG_INF("Capturing memory slot %u as a key sequence", slot + 1U);
    notify_memory_changed(slot, false);
}

static void cancel_capture(void) {
    uint8_t slot;

    k_mutex_lock(&memory_mutex, K_FOREVER);
    if (!capture.active) {
        k_mutex_unlock(&memory_mutex);
        return;
    }

    slot = capture.slot;
    wipe(&capture, sizeof(capture));
    held_modifiers = 0U;
    initial_sys_release_pending = false;
    reset_sys_gesture_locked();
    k_mutex_unlock(&memory_mutex);

    LOG_INF("Cancelled memory slot %u capture", slot + 1U);
    notify_memory_changed(slot, false);
}

static void clear_slot(uint8_t slot) {
    k_mutex_lock(&memory_mutex, K_FOREVER);
    wipe(&slots[slot], sizeof(slots[slot]));
    slots[slot].version = MEMORY_SETTINGS_VERSION;
    k_mutex_unlock(&memory_mutex);

    int err = persist_slot(slot);
    if (err < 0) {
        LOG_WRN("Unable to persist cleared memory slot %u (%d)", slot + 1U, err);
    }

    notify_memory_changed(slot, false);
}

static bool append_text_character_locked(char character) {
    size_t length = bounded_length(capture.text, sizeof(capture.text));
    if (length >= TOUCAN_MEMORY_TEXT_CAPACITY) {
        return false;
    }

    capture.text[length] = character;
    capture.text[length + 1U] = '\0';
    return true;
}

static uint8_t modifier_for_keycode(uint32_t keycode) {
    if (keycode < HID_USAGE_KEY_KEYBOARD_LEFTCONTROL ||
        keycode > HID_USAGE_KEY_KEYBOARD_RIGHT_GUI) {
        return 0U;
    }

    return BIT(keycode - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL);
}

static bool keyboard_event_to_character(const struct zmk_keycode_state_changed *event,
                                        uint8_t modifiers, char *character) {
    bool shifted = (modifiers & (MOD_LSFT | MOD_RSFT)) != 0U;
    uint32_t keycode = event->keycode;

    if (keycode >= HID_USAGE_KEY_KEYBOARD_A && keycode <= HID_USAGE_KEY_KEYBOARD_Z) {
        zmk_hid_indicators_t indicators = zmk_hid_indicators_get_current_profile();
        bool caps_lock = (indicators & BIT(HID_USAGE_LED_CAPS_LOCK - 1U)) != 0U;
        *character = (char)(((shifted != caps_lock) ? 'A' : 'a') +
                            keycode - HID_USAGE_KEY_KEYBOARD_A);
        return true;
    }

    if (keycode >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
        keycode <= HID_USAGE_KEY_KEYBOARD_9_AND_LEFT_PARENTHESIS) {
        static const char normal[] = "123456789";
        static const char shifted_chars[] = "!@#$%^&*(";
        size_t index = keycode - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION;
        *character = shifted ? shifted_chars[index] : normal[index];
        return true;
    }
    if (keycode == HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
        *character = shifted ? ')' : '0';
        return true;
    }

    switch (keycode) {
    case HID_USAGE_KEY_KEYBOARD_RETURN_ENTER:
        *character = '\n';
        return true;
    case HID_USAGE_KEY_KEYBOARD_TAB:
        *character = '\t';
        return true;
    case HID_USAGE_KEY_KEYBOARD_SPACEBAR:
        *character = ' ';
        return true;
    case HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE:
        *character = shifted ? '_' : '-';
        return true;
    case HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS:
        *character = shifted ? '+' : '=';
        return true;
    case HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE:
        *character = shifted ? '{' : '[';
        return true;
    case HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE:
        *character = shifted ? '}' : ']';
        return true;
    case HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE:
        *character = shifted ? '|' : '\\';
        return true;
    case HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON:
        *character = shifted ? ':' : ';';
        return true;
    case HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE:
        *character = shifted ? '"' : '\'';
        return true;
    case HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE:
        *character = shifted ? '~' : '`';
        return true;
    case HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN:
        *character = shifted ? '<' : ',';
        return true;
    case HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN:
        *character = shifted ? '>' : '.';
        return true;
    case HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK:
        *character = shifted ? '?' : '/';
        return true;
    default:
        return false;
    }
}

static bool add_swallowed_key_locked(uint16_t usage_page, uint16_t keycode) {
    if (swallowed_active_count >= ARRAY_SIZE(swallowed_active)) {
        return false;
    }

    swallowed_active[swallowed_active_count++] = (struct swallowed_key){
        .usage_page = usage_page,
        .keycode = keycode,
        .capture_generation = capture_generation,
    };
    return true;
}

static bool remove_swallowed_key_locked(uint16_t usage_page, uint16_t keycode,
                                        uint16_t *generation) {
    for (size_t i = 0; i < swallowed_active_count; i++) {
        if (swallowed_active[i].usage_page != usage_page ||
            swallowed_active[i].keycode != keycode) {
            continue;
        }

        if (generation != NULL) {
            *generation = swallowed_active[i].capture_generation;
        }
        swallowed_active[i] = swallowed_active[swallowed_active_count - 1U];
        swallowed_active_count--;
        return true;
    }

    return false;
}

static bool update_forwarded_key_locked(uint16_t usage_page, uint16_t keycode, bool pressed) {
    if (!pressed) {
        for (size_t i = 0; i < forwarded_active_count; i++) {
            if (forwarded_active[i].usage_page != usage_page ||
                forwarded_active[i].keycode != keycode) {
                continue;
            }

            forwarded_active[i] = forwarded_active[forwarded_active_count - 1U];
            forwarded_active_count--;
            return true;
        }
        return false;
    }

    if (forwarded_active_count >= ARRAY_SIZE(forwarded_active)) {
        return false;
    }

    forwarded_active[forwarded_active_count++] = (struct swallowed_key){
        .usage_page = usage_page,
        .keycode = keycode,
    };
    return true;
}

static bool finish_capture(void) {
    uint8_t slot;

    k_mutex_lock(&memory_mutex, K_FOREVER);
    if (!capture.active || capture_is_empty_locked()) {
        k_mutex_unlock(&memory_mutex);
        return false;
    }

    slot = capture.slot;
    struct persisted_memory_slot *saved = &slots[slot];
    wipe(saved, sizeof(*saved));
    saved->version = MEMORY_SETTINGS_VERSION;
    saved->type = capture.mode == TOUCAN_MEMORY_CAPTURE_SEQUENCE
                      ? TOUCAN_MEMORY_SLOT_SEQUENCE
                      : TOUCAN_MEMORY_SLOT_TEXT;

    if (saved->type == TOUCAN_MEMORY_SLOT_SEQUENCE) {
        saved->sequence_action_count = capture.sequence_action_count;
        memcpy(saved->content.sequence, capture.sequence, sizeof(saved->content.sequence));
    } else {
        saved->text_length = bounded_length(capture.text, TOUCAN_MEMORY_TEXT_CAPACITY);
        memcpy(saved->content.text, capture.text, saved->text_length);
    }

    wipe(&capture, sizeof(capture));
    held_modifiers = 0U;
    initial_sys_release_pending = false;
    reset_sys_gesture_locked();
    k_mutex_unlock(&memory_mutex);

    int err = persist_slot(slot);
    if (err < 0) {
        LOG_WRN("Unable to persist memory slot %u (%d)", slot + 1U, err);
    }

    LOG_INF("Saved memory slot %u", slot + 1U);
    notify_memory_changed(slot, true);
    return true;
}

static int memory_keycode_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *event = as_zmk_keycode_state_changed(eh);
    if (event == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool notify = false;
    uint16_t swallowed_generation = 0U;

    k_mutex_lock(&memory_mutex, K_FOREVER);

    if (!event->state && remove_swallowed_key_locked(event->usage_page, event->keycode,
                                                     &swallowed_generation)) {
        held_modifiers &= ~(modifier_for_keycode(event->keycode) |
                            event->explicit_modifiers);

        if (capture.active && capture.mode == TOUCAN_MEMORY_CAPTURE_SEQUENCE &&
            swallowed_generation == capture_generation &&
            capture.sequence_action_count < TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY) {
            capture.sequence[capture.sequence_action_count++] =
                (struct toucan_memory_sequence_action){
                    .usage_page = event->usage_page,
                    .keycode = event->keycode,
                    .implicit_modifiers = event->implicit_modifiers,
                    .explicit_modifiers = event->explicit_modifiers,
                    .pressed = false,
                };
        }

        k_mutex_unlock(&memory_mutex);
        return ZMK_EV_EVENT_HANDLED;
    }

    if (!capture.active) {
        (void)update_forwarded_key_locked(event->usage_page, event->keycode, event->state);
        k_mutex_unlock(&memory_mutex);
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!event->state) {
        bool was_forwarded =
            update_forwarded_key_locked(event->usage_page, event->keycode, false);
        k_mutex_unlock(&memory_mutex);
        return was_forwarded ? ZMK_EV_EVENT_BUBBLE : ZMK_EV_EVENT_HANDLED;
    }

    (void)add_swallowed_key_locked(event->usage_page, event->keycode);

    if (capture.mode == TOUCAN_MEMORY_CAPTURE_SEQUENCE) {
        if (capture.sequence_action_count < TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY) {
            capture.sequence[capture.sequence_action_count++] =
                (struct toucan_memory_sequence_action){
                    .usage_page = event->usage_page,
                    .keycode = event->keycode,
                    .implicit_modifiers = event->implicit_modifiers,
                    .explicit_modifiers = event->explicit_modifiers,
                    .pressed = true,
                };
            notify = true;
        }
    } else if (event->usage_page == HID_USAGE_KEY) {
        uint8_t explicit_modifier = modifier_for_keycode(event->keycode);
        if (explicit_modifier != 0U) {
            held_modifiers |= explicit_modifier | event->explicit_modifiers;
        } else {
            char character;
            if (keyboard_event_to_character(event,
                                            held_modifiers | event->implicit_modifiers |
                                                event->explicit_modifiers,
                                            &character)) {
                notify = append_text_character_locked(character);
            } else if (event->keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE &&
                       capture.text[0] != '\0') {
                size_t length = bounded_length(capture.text, sizeof(capture.text));
                capture.text[length - 1U] = '\0';
                notify = true;
            }
        }
    }

    k_mutex_unlock(&memory_mutex);

    if (notify) {
        notify_memory_changed(-1, false);
    }
    return ZMK_EV_EVENT_HANDLED;
}

ZMK_LISTENER(toucan_memory_keycode, memory_keycode_listener);
ZMK_SUBSCRIPTION(toucan_memory_keycode, zmk_keycode_state_changed);

static void sys_hold_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    k_mutex_lock(&memory_mutex, K_FOREVER);
    bool hold_fired = capture.active && capture_sys_pressed;
    bool should_save = hold_fired && !capture_is_empty_locked();
    if (hold_fired) {
        capture_sys_hold_fired = true;
        capture_sys_tap_count = 0U;
        (void)k_work_cancel_delayable(&sys_tap_work);
    }
    k_mutex_unlock(&memory_mutex);

    if (should_save) {
        (void)finish_capture();
    }
}

static void sys_tap_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    bool toggled = false;

    k_mutex_lock(&memory_mutex, K_FOREVER);
    if (!capture_sys_pressed) {
        if (capture.active && capture_sys_tap_count == 1U && capture_is_empty_locked()) {
            capture.mode = capture.mode == TOUCAN_MEMORY_CAPTURE_SEQUENCE
                               ? TOUCAN_MEMORY_CAPTURE_TEXT
                               : TOUCAN_MEMORY_CAPTURE_SEQUENCE;
            toggled = true;
        }
        capture_sys_tap_count = 0U;
    }
    k_mutex_unlock(&memory_mutex);

    if (toggled) {
        notify_memory_changed(-1, false);
    }
}

static int memory_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);
    if (event == NULL || event->position != SYS_THUMB_POSITION) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool cancel = false;
    int result = ZMK_EV_EVENT_BUBBLE;

    k_mutex_lock(&memory_mutex, K_FOREVER);
    sys_thumb_pressed = event->state;

    if (!event->state && initial_sys_release_pending) {
        initial_sys_release_pending = false;
    } else if (capture.active) {
        result = ZMK_EV_EVENT_HANDLED;

        if (event->state) {
            capture_sys_pressed = true;
            capture_sys_hold_fired = false;
            swallowed_sys_release = true;
            if (capture_sys_tap_count > 0U) {
                /* A second tap started in time; wait for its release to cancel. */
                (void)k_work_cancel_delayable(&sys_tap_work);
            }
            (void)k_work_reschedule(&sys_hold_work, K_MSEC(SYS_HOLD_TO_SAVE_MS));
        } else if (capture_sys_pressed) {
            capture_sys_pressed = false;
            swallowed_sys_release = false;
            (void)k_work_cancel_delayable(&sys_hold_work);
            if (capture_sys_hold_fired) {
                capture_sys_hold_fired = false;
                capture_sys_tap_count = 0U;
            } else {
                capture_sys_tap_count++;

                if (capture_sys_tap_count >= 2U) {
                    capture_sys_tap_count = 0U;
                    (void)k_work_cancel_delayable(&sys_tap_work);
                    cancel = true;
                } else {
                    (void)k_work_reschedule(&sys_tap_work,
                                            K_MSEC(SYS_DOUBLE_TAP_MS));
                }
            }
        }
    } else if (!event->state && swallowed_sys_release) {
        swallowed_sys_release = false;
        result = ZMK_EV_EVENT_HANDLED;
    }

    k_mutex_unlock(&memory_mutex);

    if (cancel) {
        cancel_capture();
    }
    return result;
}

ZMK_LISTENER(toucan_memory_position, memory_position_listener);
ZMK_SUBSCRIPTION(toucan_memory_position, zmk_position_state_changed);

static uint32_t encoded_for_character(char character, bool caps_lock) {
    static const uint32_t letters[] = {
        A, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    };
    static const uint32_t numbers[] = {N0, N1, N2, N3, N4, N5, N6, N7, N8, N9};

    if (character >= 'a' && character <= 'z') {
        uint32_t keycode = letters[character - 'a'];
        return caps_lock ? LS(keycode) : keycode;
    }
    if (character >= 'A' && character <= 'Z') {
        uint32_t keycode = letters[character - 'A'];
        return caps_lock ? keycode : LS(keycode);
    }
    if (character >= '0' && character <= '9') {
        return numbers[character - '0'];
    }

    switch (character) {
    case ' ':
        return SPACE;
    case '\n':
        return ENTER;
    case '\t':
        return TAB;
    case '-':
        return MINUS;
    case '_':
        return LS(MINUS);
    case '=':
        return EQUAL;
    case '+':
        return LS(EQUAL);
    case '[':
        return LBKT;
    case '{':
        return LS(LBKT);
    case ']':
        return RBKT;
    case '}':
        return LS(RBKT);
    case '\\':
        return BSLH;
    case '|':
        return LS(BSLH);
    case ';':
        return SEMI;
    case ':':
        return LS(SEMI);
    case '\'':
        return SQT;
    case '"':
        return LS(SQT);
    case '`':
        return GRAVE;
    case '~':
        return LS(GRAVE);
    case ',':
        return COMMA;
    case '<':
        return LS(COMMA);
    case '.':
        return DOT;
    case '>':
        return LS(DOT);
    case '/':
        return FSLH;
    case '?':
        return LS(FSLH);
    case '!':
        return LS(N1);
    case '@':
        return LS(N2);
    case '#':
        return LS(N3);
    case '$':
        return LS(N4);
    case '%':
        return LS(N5);
    case '^':
        return LS(N6);
    case '&':
        return LS(N7);
    case '*':
        return LS(N8);
    case '(':
        return LS(N9);
    case ')':
        return LS(N0);
    default:
        return 0U;
    }
}

static int raise_recorded_action(const struct toucan_memory_sequence_action *action,
                                 bool pressed) {
    return raise_zmk_keycode_state_changed((struct zmk_keycode_state_changed){
        .usage_page = action->usage_page,
        .keycode = action->keycode,
        .implicit_modifiers = action->implicit_modifiers,
        .explicit_modifiers = action->explicit_modifiers,
        .state = pressed,
        .timestamp = k_uptime_get(),
    });
}

static int tap_encoded(uint32_t encoded) {
    int err = raise_zmk_keycode_state_changed_from_encoded(encoded, true, k_uptime_get());
    k_sleep(K_MSEC(PLAYBACK_TEXT_PRESS_MS));
    int release_err =
        raise_zmk_keycode_state_changed_from_encoded(encoded, false, k_uptime_get());
    k_sleep(K_MSEC(PLAYBACK_TEXT_GAP_MS));
    return err < 0 ? err : release_err;
}

static bool caps_lock_active(void) {
    zmk_hid_indicators_t indicators = zmk_hid_indicators_get_current_profile();
    return (indicators & BIT(HID_USAGE_LED_CAPS_LOCK - 1U)) != 0U;
}

static void play_slot(uint8_t slot_index) {
    struct persisted_memory_slot slot;
    bool unmatched_presses[TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY] = {false};

    k_mutex_lock(&memory_mutex, K_FOREVER);
    slot = slots[slot_index];
    k_mutex_unlock(&memory_mutex);

    if (slot.type == TOUCAN_MEMORY_SLOT_SEQUENCE) {
        for (size_t i = 0; i < slot.sequence_action_count; i++) {
            const struct toucan_memory_sequence_action *action = &slot.content.sequence[i];
            (void)raise_recorded_action(action, action->pressed);

            if (action->pressed) {
                unmatched_presses[i] = true;
            } else {
                for (size_t j = i; j > 0U; j--) {
                    const struct toucan_memory_sequence_action *candidate =
                        &slot.content.sequence[j - 1U];
                    if (unmatched_presses[j - 1U] &&
                        candidate->usage_page == action->usage_page &&
                        candidate->keycode == action->keycode &&
                        candidate->implicit_modifiers == action->implicit_modifiers &&
                        candidate->explicit_modifiers == action->explicit_modifiers) {
                        unmatched_presses[j - 1U] = false;
                        break;
                    }
                }
            }
            k_sleep(K_MSEC(PLAYBACK_ACTION_GAP_MS));
        }

        /* A full buffer can omit trailing releases; never leave a HID key held. */
        for (size_t i = slot.sequence_action_count; i > 0U; i--) {
            const struct toucan_memory_sequence_action *action =
                &slot.content.sequence[i - 1U];
            if (unmatched_presses[i - 1U]) {
                (void)raise_recorded_action(action, false);
            }
        }
    } else if (slot.type == TOUCAN_MEMORY_SLOT_TEXT) {
        for (size_t i = 0; i < slot.text_length; i++) {
            uint32_t encoded =
                encoded_for_character(slot.content.text[i], caps_lock_active());
            if (encoded != 0U) {
                (void)tap_encoded(encoded);
            }
        }
    }

    wipe(&slot, sizeof(slot));
}

static void playback_thread(void *unused_a, void *unused_b, void *unused_c) {
    ARG_UNUSED(unused_a);
    ARG_UNUSED(unused_b);
    ARG_UNUSED(unused_c);

    while (true) {
        uint8_t slot;
        (void)k_msgq_get(&playback_queue, &slot, K_FOREVER);
        play_slot(slot);
        atomic_dec(&playback_pending);
    }
}

K_THREAD_DEFINE(toucan_memory_playback_thread, PLAYBACK_THREAD_STACK_SIZE, playback_thread, NULL,
                NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

static void queue_playback(uint8_t slot) {
    k_mutex_lock(&memory_mutex, K_FOREVER);
    bool usable = !capture.active && slots[slot].type != TOUCAN_MEMORY_SLOT_EMPTY;
    k_mutex_unlock(&memory_mutex);

    if (!usable) {
        return;
    }

    atomic_inc(&playback_pending);
    int err = k_msgq_put(&playback_queue, &slot, K_NO_WAIT);
    if (err < 0) {
        atomic_dec(&playback_pending);
        LOG_WRN("Memory playback queue is full");
    }
}

#define DT_DRV_COMPAT zmk_behavior_toucan_memory_mode

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
static int on_memory_mode_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    k_mutex_lock(&memory_mutex, K_FOREVER);
    memory_clear_held = true;
    k_mutex_unlock(&memory_mutex);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_memory_mode_released(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    k_mutex_lock(&memory_mutex, K_FOREVER);
    memory_clear_held = false;
    k_mutex_unlock(&memory_mutex);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api memory_mode_driver_api = {
    .binding_pressed = on_memory_mode_pressed,
    .binding_released = on_memory_mode_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
};

#define MEMORY_MODE_INST(n)                                                                       \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                               \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &memory_mode_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MEMORY_MODE_INST)
#endif

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT zmk_behavior_toucan_memory_set

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
static int on_memory_set_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    if (!valid_slot(binding->param1)) {
        return -EINVAL;
    }

    begin_capture((uint8_t)binding->param1);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_memory_set_released(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api memory_set_driver_api = {
    .binding_pressed = on_memory_set_pressed,
    .binding_released = on_memory_set_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
};

#define MEMORY_SET_INST(n)                                                                        \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                               \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &memory_set_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MEMORY_SET_INST)
#endif

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT zmk_behavior_toucan_memory_use

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)
static int on_memory_use_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    if (!valid_slot(binding->param1)) {
        return -EINVAL;
    }

    uint8_t slot = (uint8_t)binding->param1;
    k_mutex_lock(&memory_mutex, K_FOREVER);
    bool clear = memory_clear_held;
    k_mutex_unlock(&memory_mutex);

    if (clear) {
        clear_slot(slot);
    } else {
        queue_playback(slot);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api memory_use_driver_api = {
    .binding_pressed = on_memory_use_pressed,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
};

#define MEMORY_USE_INST(n)                                                                        \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                               \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &memory_use_driver_api);

DT_INST_FOREACH_STATUS_OKAY(MEMORY_USE_INST)
#endif
