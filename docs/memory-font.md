# Native monospace display typography

All linked text uses **Toucan Display**, a bitmap subset of
[Terminus Font 4.49.1](https://terminus-font.sourceforge.net/). Its supplied
bitmap sizes replace both the hand-drawn fonts and the experimental outline
font conversions. Every ink pixel comes directly from the upstream BDF files:
there is no rasterization, hinting, smoothing, retouching, or resizing in the
generation process. The LCD receives only native one-bit pixels, with no font
engine or dynamic allocation on the keyboard. The legacy QuinqueFive assets
are not linked.

The checked-in pixel tables are
[`generated_fonts.h`](../boards/shields/nice_view_gem/assets/generated_fonts.h).
They preserve the typeface's monospaced advances, horizontal bearings, and
shared baselines. Spaces have the same advance as other characters, and lookup
is case-sensitive in both TEXT and SEQ. The small and STATUS sizes cover every
printable ASCII character. The larger sizes contain the page/layer labels,
digits for `L<n>` fallbacks, and `?`, rather than unused alphabets.

## Sizes and baseline

| Font | Use | Upstream bitmap | Capital height | Advance | Packed cell |
| --- | --- | --- | ---: | ---: | ---: |
| `small_font` | Version, TEXT counter, FULL | `ter-u12n.bdf`, 6×12 normal | 8 | 6 | 6×12 |
| `status_font` | Dashboard, MEM SET header and content | `ter-u18b.bdf`, 10×18 bold | 12 | 10 | 10×18 |
| `title_font` | TOUCAN, OFF | `ter-u24b.bdf`, 12×24 bold | 15 | 12 | 12×15 |
| `layer_font` | Layer names, UF2 | `ter-u32b.bdf`, 16×32 bold | 20 | 16 | 16×20 |

All measurements are native LCD pixels. Packing removes only top/bottom rows
that are blank across the selected glyphs; it preserves every ink coordinate
relative to the original baseline. The larger uppercase subsets need fewer
rows than their upstream cells. Titles use the supplied 15-pixel capitals,
not an interpolated 16-pixel size.

Text coordinates refer to the top of a capital, not the top of the bitmap cell.
A font-level `y_offset`
preserves taller punctuation without cropping or individually recentering it.
For STATUS, capitals occupy y=0..11, lowercase x-height is 9 pixels, and
descenders extend up to three pixels below the baseline. The complete glyph
set spans 18 rows, including punctuation above the capitals.

MEM content uses a 19-pixel line pitch: the full cell plus one clear row. Six
lines still fit above the footer. Wrapping and caret bounds reserve space for
both text and modifier symbols and stop before y=148. The dashboard's capital
labels and digits remain inside their existing dirty bands. Text advances are
now truly monospaced rather than retaining the old proportional spacing.

Named SEQ control keys (`ENT`, `TAB`, `SPC`, `ESC`, `BSP`, `DEL`, `CAP`) and
modifier icons remain distinct from literal characters.

## Regenerating the fonts

Change sizes/coverage in
[`generate-display-font.py`](../scripts/generate-display-font.py), not individual
pixel rows. The standard-library-only generator downloads the pinned upstream
source archive into memory and verifies its SHA-256 and bundled license before
reading the four BDF files. It copies their MSB-first bitmap rows and preserves
bearings, advances, and baselines. Invalid metrics or padding fail explicitly
rather than silently clipping or altering artwork.

```sh
python3 scripts/generate-display-font.py
python3 scripts/generate-display-font.py --check
# Offline, using the same unmodified source archive:
python3 scripts/generate-display-font.py --archive /path/to/terminus-font-4.49.1.tar.gz --check
```

The source filenames, version, and archive checksum are recorded in the
generated header. There is no Pillow or FreeType dependency. Normal firmware
builds, font tests, and SVG specimens use the checked-in rows and need no font
download.

The upstream font and these derived bitmap tables are distributed under the
[SIL Open Font License 1.1](../boards/shields/nice_view_gem/assets/Terminus-OFL.txt).
The subset is named **Toucan Display** to respect the upstream Reserved Font
Name, “Terminus Font”; source attribution is retained in the generated header.
The renderer and generator retain the project's MIT license.

## Icons

Hand-edited icon artwork remains separate in
[`display_icons.h`](../boards/shields/nice_view_gem/assets/display_icons.h).
Its `X`/`.` rows are native pixels: Apple, Windows, Command, Shift, Control, and
Option are 16×16; power/disconnect icons are 8×12 and 10×10. Windows shares one
glyph between the footer and SEQ; Apple is platform-only, and Command retains
its own SEQ symbol. The procedural moon, power, download, signal, and battery
shapes also remain native.

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
- Shift built into a symbol binding is represented by the symbol itself, not an
  extra icon: SYM `+` shows `+`, and `cmd` followed by SYM `+` shows Cmd + `+`.
  This applies to all shifted punctuation in the shared US-layout decoder.
  Named or explicitly held Shift stays visible, even with a shifted symbol;
  letter shortcuts and controls such as Shift+Tab also retain their Shift icon.
  Only the capture's icon metadata changes: character decoding and saved
  press/release modifiers retain the Shift needed for playback.
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
ZMK builder, plus glyph coverage, case distinction, row-width, native-detail,
monospacing, icon-size, label-fit, baseline/descender, and dirty-band checks.
Offline fingerprints independently derived from the original BDFs verify all
244 glyphs' ink coordinates and advances. Importer tests cover byte padding,
bearings, descenders, blank-row removal, and rejection of invalid input.
Compiler tests cover all 24 modifier orders, pending
prefixes, target entry, repeated names, mixed named/physical modifiers, and
`del` taps with independent releases under rollover. All 21 shifted punctuation
bindings are checked with left/right implicit Shift, unchanged playback, and
no redundant icons; deliberate Shift and non-symbol shortcuts stay visible.
The native ZMK integration test captures mixed-case content in both modes,
checks that `ctrl` and `del` stay literal in TEXT, and exercises live SEQ names,
plain and modified Delete taps, modifier-only playback, and overflow rejection.
It checks the distinct icon orders and literal `4` labels for `shiftcmd4` and
`cmdshift4`, while verifying identical Shift+GUI+4 playback. It enters `+`
through a held symbol layer, alone and after `cmd`/`shiftcmd`, checking both
the visible modifier order and the original Shift+Equals playback. It uses the SYS
save/cancel gestures and verifies playback and capture privacy. It checks
runtime slots, not persistence across a power cycle:

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
