# Case-sensitive MEM SET typography

MEM SET uses the dashboard's hand-drawn STATUS font, not the old QuinqueFive
assets. Its source is the `status_glyphs` table in
[`screen.c`](../boards/shields/nice_view_gem/widgets/screen.c). The same glyph
lookup and drawing path is used for TEXT characters and SEQ key labels; lookup
does not fold case.

## Lowercase design

The original capitals, digits, and punctuation are unchanged. Lowercase is a
companion alphabet, not scaled-down capitals:

| Metric | Source pixels | On MEM SET at 2× |
| --- | ---: | ---: |
| Capital/ascender height | 6 | 12 |
| Lowercase x-height | 4, rows 2–5 | 8 |
| Baseline | Bottom of row 5 | Bottom of row 11 |
| Descenders (`g j p q y`) | Rows 6–7 | 4 below baseline |
| Typical lowercase width | 4 | 8 |
| Maximum width (`m w`, most capitals) | 5 | 10 |
| Inter-glyph gap | 1 | 2 |
| Content line pitch | 9 | 18 |

Design rules:

- Use one-pixel strokes and square terminals, with the same single-pixel
  clipped corners as the capitals. No grayscale, antialiasing, or fractional
  scaling.
- Keep the bowls of `a b d g o p q` related. Single-storey `a` and `g` leave
  recognizable counters in the four-row body.
- Ascenders reach the existing capital height; descenders extend below the
  shared baseline rather than lifting or shrinking the rest of the letter.
- Keep `i` narrow with a separated dot, `l` with a hooked terminal, and the
  existing capital `I` with bars, so mixed-case text remains distinguishable.
- Share spacing, wrapping, and glyph lookup between both capture modes.

The missing printable ASCII punctuation is included as well, so apostrophes,
quotes, brackets, and shifted symbols do not turn into fallback question marks.
Space advances without a bitmap. Named SEQ control keys (`ENT`, `TAB`, `SPC`,
`ESC`, `BSP`, `DEL`, `CAP`) and modifier icons remain distinct from literal text.

The body reserves room for eight source rows per glyph. It stops before the
TEXT counter's footer; a wrapped caret uses the same bounds. Descenders cannot
overlap the next line or the counter.

## Case semantics

- **TEXT:** capture and playback preserve literal case, including Shift XOR
  Caps Lock. The preview now displays that stored case.
- **SEQ:** the preview consumes every recorded press and release, tracks both
  physical Shift keys and compound modifiers, and combines held modifiers with
  each action's original input modifiers. A release affects later letters,
  not letters already shown. Physically shifted punctuation uses the same
  character decoder as TEXT.
- Both modes start with the host Caps Lock state sampled when capture begins
  and simulate captured Caps Lock presses locally, without toggling it at the
  host. Later host indicator changes do not alter the capture-local case state
  or retroactively recase earlier preview text.
- SEQ compiles [typed modifier names](toucan-moonlander-layout.md#seq-modifier-name-shorthand)
  into key events before preview and save. Those names are case-insensitive;
  their added modifiers appear as icons but do not change the character label.
  Icons preserve the order of the names, so `shiftcmd4` displays Shift + Cmd + `4`
  and `cmdshift4` displays Cmd + Shift + `4`, never `$`. Capture-only per-action
  metadata keeps the input modifiers for labels and an ordered list for icons,
  separate from the compiled playback modifiers. Repeated names stay in their
  first position. Extra modifiers from one compound key binding have no input
  order and follow the names in GUI/Ctrl/Alt/Shift order, without duplicate icons.
- Stored SEQ data is a key-event stream, not converted text. Replay therefore
  still depends on the host's layout and lock state. Use TEXT for literal
  strings that must retain case across Caps Lock changes or contain reserved
  modifier names.

The shared decoder is
[`toucan_key_text.c`](../src/toucan_key_text.c). Existing persisted slots and
their settings version are unchanged; the initial Caps Lock snapshot exists
only during capture.

## Review and validation

Generate a font specimen directly from the production pixel rows:

```sh
python3 scripts/preview-memory-font.py /tmp/toucan-memory-font.svg
```

Run the host decoder/glyph-lookup and SEQ compiler tests inside the existing
ZMK builder, plus glyph coverage, case distinction, row-width, and
baseline/descender checks. Compiler tests cover all 24 modifier orders, pending
prefixes, target entry, repeated names, and mixed named/physical modifiers.
The native ZMK integration test captures mixed-case content in both modes,
checks that `ctrl` stays literal in TEXT, and exercises live SEQ names,
modifier-only playback, and overflow rejection. It checks the distinct icon
orders and literal `4` labels for `shiftcmd4` and `cmdshift4`, while verifying
identical Shift+GUI+4 playback. It uses the SYS save/cancel gestures and verifies
playback and capture privacy. It checks runtime slots, not persistence across
a power cycle:

```sh
make test-memory
make all
```

The native tests exercise capture and playback, not the LCD renderer.
The left firmware build additionally enforces a 256-byte per-function stack
budget for the production screen code with GCC. Memory snapshots are copied
directly into display-owned storage, not passed by value through the widget
listener; see the [display specification](display-redesign-spec.md).

On the keyboard, capture `aABc`, `AaZz`, and `It's MixedCase!` in TEXT and SEQ.
Also hold Shift across several letters, release it for lowercase, and try the
same with right Shift and Caps Lock. Save/play both modes and confirm that the
SEQ modifier icons/control labels remain visible. Type `shiftcmd4` without
spaces and check for Shift/Cmd icons followed by `4`, without reordering as
each name or the target completes. Check the reverse order with `cmdshift4`;
physically entering `$` should still show `$`. The specimen is a font preview,
not evidence of a hardware flash.

Repeat SEQ entry, typing, save, and cancel several times, including normal
typing immediately after leaving capture. Verify that both keyboard input and
the display remain responsive through the page transitions.
