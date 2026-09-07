/*
 * Bare ctrl/shift/alt/cmd names collapse immediately, without a delimiter.
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/modifiers.h>

#include "toucan_key_text.h"
#include "toucan_memory_sequence.h"

#define MAX_MODIFIER_NAME_LENGTH 5

static const struct {
    char name[MAX_MODIFIER_NAME_LENGTH + 1];
    uint8_t modifier;
} modifier_names[] = {
    {"ctrl", MOD_LCTL},
    {"shift", MOD_LSFT},
    {"alt", MOD_LALT},
    {"cmd", MOD_LGUI},
};

static void append_modifier(uint8_t order[TOUCAN_MEMORY_MODIFIER_COUNT], uint8_t modifier) {
    for (size_t i = 0; i < TOUCAN_MEMORY_MODIFIER_COUNT; i++) {
        if (order[i] == modifier) {
            return;
        }
        if (order[i] == 0U) {
            order[i] = modifier;
            return;
        }
    }
}

struct sequence_compiler {
    const struct toucan_memory_sequence_action *input;
    size_t input_count;
    struct toucan_memory_sequence_action *output;
    struct toucan_memory_sequence_preview *preview;
    struct toucan_memory_sequence_result result;
    uint8_t omitted[(TOUCAN_MEMORY_SEQUENCE_INPUT_CAPACITY + 7) / 8];
    uint16_t press_source[TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY];
    bool unmatched_press[TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY];
};

static bool is_modifier(const struct toucan_memory_sequence_action *action) {
    return action->usage_page == HID_USAGE_KEY &&
           action->keycode >= HID_USAGE_KEY_KEYBOARD_LEFTCONTROL &&
           action->keycode <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI;
}

static bool same_key(const struct toucan_memory_sequence_action *a,
                     const struct toucan_memory_sequence_action *b) {
    return a->usage_page == b->usage_page && a->keycode == b->keycode &&
           a->implicit_modifiers == b->implicit_modifiers &&
           a->explicit_modifiers == b->explicit_modifiers;
}

static bool is_omitted(const struct sequence_compiler *compiler, size_t index) {
    return (compiler->omitted[index / 8] & (1U << (index % 8))) != 0U;
}

static void omit(struct sequence_compiler *compiler, size_t index) {
    compiler->omitted[index / 8] |= 1U << (index % 8);
}

static char name_letter(const struct toucan_memory_sequence_action *action,
                        uint8_t held_modifiers) {
    uint8_t modifiers = held_modifiers | action->implicit_modifiers | action->explicit_modifiers;
    if (!action->pressed || action->usage_page != HID_USAGE_KEY ||
        action->keycode < HID_USAGE_KEY_KEYBOARD_A ||
        action->keycode > HID_USAGE_KEY_KEYBOARD_Z ||
        (modifiers & ~(MOD_LSFT | MOD_RSFT)) != 0U) {
        return '\0';
    }
    /* Names are case-insensitive; unmatched keys keep their original modifiers. */
    return 'a' + action->keycode - HID_USAGE_KEY_KEYBOARD_A;
}

static bool match_name(const struct sequence_compiler *compiler, size_t start,
                       uint8_t held_modifiers, const char *name, size_t *presses) {
    size_t letter = 0U;
    for (size_t i = start; i < compiler->input_count; i++) {
        if (is_omitted(compiler, i)) {
            continue;
        }
        const struct toucan_memory_sequence_action *action = &compiler->input[i];
        if (!action->pressed) {
            /* Ordinary rollover is fine, but don't match across a modifier change. */
            if (is_modifier(action)) {
                return false;
            }
            continue;
        }
        if (name_letter(action, held_modifiers) != name[letter]) {
            return false;
        }
        presses[letter++] = i;
        if (name[letter] == '\0') {
            return true;
        }
    }
    return false;
}

static void omit_key_pair(struct sequence_compiler *compiler, size_t press) {
    omit(compiler, press);
    size_t nested = 0U;
    for (size_t i = press + 1U; i < compiler->input_count; i++) {
        if (!same_key(&compiler->input[press], &compiler->input[i])) {
            continue;
        }
        if (compiler->input[i].pressed) {
            nested++;
        } else if (nested > 0U) {
            nested--;
        } else {
            omit(compiler, i);
            break;
        }
    }
}

static void emit(struct sequence_compiler *compiler,
                 struct toucan_memory_sequence_action action, size_t source,
                 const struct toucan_memory_sequence_preview *preview) {
    if (compiler->result.count == TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY) {
        compiler->result.overflow = true;
        return;
    }
    size_t index = compiler->result.count++;
    compiler->output[index] = action;
    if (preview != NULL) {
        compiler->preview[index] = *preview;
    }
    compiler->press_source[index] = source;
    compiler->unmatched_press[index] = action.pressed;
}

static void emit_input(struct sequence_compiler *compiler, size_t source,
                       const uint8_t named_modifiers[TOUCAN_MEMORY_MODIFIER_COUNT]) {
    struct toucan_memory_sequence_action action = compiler->input[source];
    struct toucan_memory_sequence_preview preview = {
        .implicit_modifiers = action.implicit_modifiers,
    };
    if (action.pressed) {
        if (named_modifiers != NULL) {
            memcpy(preview.modifier_order, named_modifiers, sizeof(preview.modifier_order));
            for (size_t i = 0; i < TOUCAN_MEMORY_MODIFIER_COUNT; i++) {
                action.implicit_modifiers |= named_modifiers[i];
            }
        }
    } else {
        /* A one-shot named modifier must accompany the corresponding release too,
         * even when other keys have rolled over the target in the meantime. */
        for (size_t j = compiler->result.count; j > 0U; j--) {
            size_t index = j - 1U;
            if (compiler->unmatched_press[index] &&
                same_key(&compiler->input[compiler->press_source[index]], &action)) {
                action.implicit_modifiers = compiler->output[index].implicit_modifiers;
                memcpy(preview.modifier_order, compiler->preview[index].modifier_order,
                       sizeof(preview.modifier_order));
                compiler->unmatched_press[index] = false;
                break;
            }
        }
    }

    /* Preserve named order, then append any extra modifiers from the key binding.
     * Left and right variants share one icon, even when a name and binding overlap. */
    static const uint8_t binding_order[] = {MOD_LGUI, MOD_LCTL, MOD_LALT, MOD_LSFT};
    uint8_t modifiers = action.implicit_modifiers | action.explicit_modifiers;
    modifiers |= modifiers >> 4;
    for (size_t i = 0; i < sizeof(binding_order); i++) {
        if ((modifiers & binding_order[i]) != 0U) {
            append_modifier(preview.modifier_order, binding_order[i]);
        }
    }
    emit(compiler, action, source, &preview);
}

static void emit_modifier(struct sequence_compiler *compiler, uint8_t modifier, bool pressed) {
    if (modifier == 0U) {
        return;
    }
    uint16_t keycode = HID_USAGE_KEY_KEYBOARD_LEFTCONTROL;
    for (; modifier > 1U; modifier >>= 1) {
        keycode++;
    }
    emit(compiler, (struct toucan_memory_sequence_action){
        .usage_page = HID_USAGE_KEY,
        .keycode = keycode,
        .pressed = pressed,
    }, 0U, NULL);
}

struct toucan_memory_sequence_result toucan_memory_sequence_compile(
    const struct toucan_memory_sequence_action *input, size_t input_count,
    struct toucan_memory_sequence_action *output, struct toucan_memory_sequence_preview *preview) {
    struct sequence_compiler compiler = {
        .input = input,
        .input_count = input_count,
        .output = output,
        .preview = preview,
    };
    struct toucan_key_text_state physical_modifiers = {0};
    uint8_t pending_modifiers[TOUCAN_MEMORY_MODIFIER_COUNT] = {0};

    memset(output, 0, sizeof(*output) * TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY);
    memset(preview, 0, sizeof(*preview) * TOUCAN_MEMORY_SEQUENCE_ACTION_CAPACITY);
    if (compiler.input_count > TOUCAN_MEMORY_SEQUENCE_INPUT_CAPACITY) {
        compiler.input_count = TOUCAN_MEMORY_SEQUENCE_INPUT_CAPACITY;
        compiler.result.overflow = true;
    }

    for (size_t i = 0; i < compiler.input_count; i++) {
        if (is_omitted(&compiler, i)) {
            continue;
        }
        const struct toucan_memory_sequence_action *action = &input[i];
        char unused;
        (void)toucan_key_text_apply(&physical_modifiers, action->usage_page, action->keycode,
                                   action->implicit_modifiers, action->explicit_modifiers,
                                   action->pressed, &unused);
        uint8_t held = toucan_key_text_modifiers(&physical_modifiers);
        bool matched = false;
        if (name_letter(action, held) != '\0') {
            for (size_t name = 0; name < sizeof(modifier_names) / sizeof(modifier_names[0]); name++) {
                size_t presses[MAX_MODIFIER_NAME_LENGTH];
                if (!match_name(&compiler, i, held, modifier_names[name].name, presses)) {
                    continue;
                }
                for (size_t letter = 0; modifier_names[name].name[letter] != '\0'; letter++) {
                    omit_key_pair(&compiler, presses[letter]);
                }
                append_modifier(pending_modifiers, modifier_names[name].modifier);
                matched = true;
                break;
            }
        }
        if (matched) {
            continue;
        }

        if (action->pressed && !is_modifier(action)) {
            emit_input(&compiler, i, pending_modifiers);
            memset(pending_modifiers, 0, sizeof(pending_modifiers));
        } else {
            emit_input(&compiler, i, NULL);
        }
    }

    /* Show completed names immediately, even before a target exists. Saving a
     * modifier-only prefix presses them in entry order and releases in reverse,
     * never leaving a stuck hold. Repeated names keep their first position. */
    for (size_t i = 0; i < TOUCAN_MEMORY_MODIFIER_COUNT; i++) {
        emit_modifier(&compiler, pending_modifiers[i], true);
    }
    for (size_t i = TOUCAN_MEMORY_MODIFIER_COUNT; i > 0U; i--) {
        emit_modifier(&compiler, pending_modifiers[i - 1U], false);
    }
    return compiler.result;
}
