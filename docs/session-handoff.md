# Toucan2 customization session handoff

Updated 2026-09-04. The commit containing this file is the current continuation
baseline. A fresh session should begin with:

```sh
cd /Users/zeruichen/Desktop/zmk-keyboard-toucan2
git status --short
git log -5 --oneline --decorate
make doctor
```

Read this file first, then use `docs/toucan-moonlander-layout.md` as the
authoritative user-facing layout description and `docs/display-redesign-spec.md`
for the display hardware, render budget, and v2 implementation details. The old
Moonlander QMK source and its semantics are preserved in
`docs/moonlander-qmk-reference.md`.

## Repository and toolchain

- `origin` is the user's fork: `git@github.com:Zerui18/zmk-keyboard-toucan2.git`.
- `upstream` is Beekeeb's repository and is fetch-only.
- Work happens on `main`; use `make sync-upstream` to merge `upstream/main`.
- ZMK is pinned to `v0.3` in `config/west.yml`. Do not assume APIs from current
  online ZMK documentation exist in this checkout; inspect the pinned source in
  the Docker workspace when behavior details matter.
- Builds run in `zmkfirmware/zmk-build-arm:stable`. West, Zephyr, and modules
  live in the persistent Docker volume `toucan2-zmk-v03`; the repo is bind
  mounted at `/repo` and the West workspace at `/workspace`.
- Run `make format-keymap` after editing `config/toucan.keymap`. The repository
  pre-commit hook checks the physical-layout formatting.
- Preserve unrelated dirty changes. Do not reset user work to make an edit.

Common commands:

```sh
make left                  # firmware/toucan_left.uf2
make right                 # firmware/toucan_right.uf2
make all
make install left          # build + guarded reboot + flash
make install right
make install-both
make flash-left            # reuse existing image
make flash-right
make usb-check
make check-keymap-format
```

The macOS flashing helper identifies `Toucan Left` and `Toucan Right` CDC
devices, performs DTR on/off at 1200 baud followed by 2400 baud within two
seconds, waits for `/Volumes/XIAO-BOOT/INFO_UF2.TXT`, copies with macOS extended
attributes disabled, waits for the volume to disappear, and verifies that the
runtime port returns. Both halves are flashed serially because they share the
same UF2 volume name. A manually mounted XIAO bootloader remains a fallback.

`CMakeLists.txt` embeds firmware version `0.3` and the current seven-character
Git SHA. Build after committing when the boot page must identify the exact
commit.

## Hardware facts

- Beekeeb Toucan2: 42 logical keys, three rows of twelve plus six thumbs.
- Both halves use Seeed XIAO BLE / nRF52840 controllers.
- The left half is the ZMK central and carries a Sharp LS013B7DH05 144×168
  one-bit reflective Memory LCD.
- The right half is the split peripheral and carries the Azoteq TPS43 trackpad.
- Batteries are 3.7 V, 320 mAh, size 402535.
- The left physical power switch is broken and soldered permanently on. Its
  firmware soft-power path is therefore important and must not regress.
- The Memory LCD retains its last pixels with the MCU off. A frozen OFF screen
  during soft shutdown is intentional.

## Current base layout and layers

Tap/hold pairs are shown as `tap/hold`:

```text
Esc        Q/NAV    W       E       R       T       | Y       U       I       O       P/NAV       Backslash
Equal/Ctrl A/SYM    S/CLIP  D/EDIT  F       G       | H       J       K       L       Semicolon/SYM Quote/Ctrl
Minus      Z/Shift  X       C/APP   V       B       | N       M       Comma   Dot     Slash/Shift  Underscore

                          SYS  Backspace/Cmd  Tab/Option | Enter  Space  FN
```

Layer indexes are centralized in `dts/toucan_layers.dtsi`:

| Index | Name | Access / purpose |
| ---: | --- | --- |
| 0 | BASE | Default typing |
| 1 | SYM | Hold A or semicolon; symbols and right-side numpad |
| 2 | NAV | Hold Q or P; arrows and navigation shortcuts |
| 3 | CLIP | Hold S, then D/F for copy-cut/paste |
| 4 | EDIT | Hold D, then S/F for undo/redo |
| 5 | APP | Hold C, then X/V for backward/forward app switching |
| 6 | MOU | Activated by trackpad touch; opaque pointer controls |
| 7 | SYS | Hold outer-left thumb; Bluetooth and macro memory |
| 8 | FN | Hold outer-right thumb; diagnostics, F-keys, media, OS mode |
| 9 | DNG | Conditional while SYS+FN are held; bootloader/reset |

Important hold-tap policy is defined in `config/toucan_behaviors.dtsi`:

- Typing-facing terms are 200 ms.
- Q/P, S, D, and C are balanced with `require-prior-idle-ms = 125`; S/D/C also
  have narrow positional trigger lists to reject unwanted same-hand rolls.
- Ctrl, Shift, A/SYM, and semicolon/SYM use opposite-hand positional triggers.
- Left Backspace taps Backspace and holds platform-aware Command on macOS or
  Control on Windows. It is balanced, has 125 ms prior-idle protection, and a
  200 ms quick-tap window: tap then press-and-hold again within the window to
  repeat Backspace instead of invoking the modifier.
- Left Tab taps Tab and holds left Option/Alt with the same balanced, prior-idle,
  and 200 ms quick-tap behavior, allowing tap-then-hold Tab repeat.
- `&cmd_key` is implemented in `src/toucan_platform_mode.c`; it resolves GUI on
  macOS and Control on Windows while preserving the resolved modifier until
  release. FN+physical M toggles and persists the OS mode.
- APP holds GUI on macOS or Alt on Windows for the lifetime of C, so X/V can be
  tapped repeatedly while the host app switcher remains open.

The latest thumb behaviors have compiled and the left half has been flashed,
but the user has not yet reported the physical feel. Tune only the dedicated
thumb behaviors if their 200/125/200 ms timing needs adjustment.

## SYS memory and Bluetooth controls

SYS is the former BT layer:

```text
BT_CLR   BT1   BT2   BT3   BT4   BT5 | ___ ___ ___ ___ ___ ___
MEM_CLR  MEM1  MEM2  MEM3  MEM4  MEM5 | ___ ___ ___ ___ ___ ___
```

- BT_CLR must be held for two seconds; it clears only the selected profile.
- Tap a memory slot to replay it; hold it for 400 ms to replace it and enter
  capture in SEQ mode.
- Hold MEM_CLR (the leftmost home-row position) while tapping MEM1–MEM5 to clear
  that slot. The physical slot positions are A, S, D, F, G.
- Once capture begins, SYS may be released. Normal keymap resolution continues,
  including layers, hold-taps, combos, macros, Enter, and Escape, but resolved
  HID events are captured before the HID listener and never reach the host.
- A single physical SYS tap toggles SEQ/TEXT only while capture is empty.
- A second SYS tap begun within 275 ms cancels and wipes capture.
- Holding SYS for 600 ms saves a non-empty capture.
- Escape is ordinary data in SEQ, not a cancellation key.
- TEXT stores up to 64 literal characters. SEQ stores up to 64 resolved
  press/release actions, including implicit and explicit modifiers.
- Five slots persist through Zephyr settings. Persistence schema is currently
  version 3.

Implementation is in `src/toucan_memory.c/.h`; devicetree behavior nodes are in
`config/toucan_behaviors.dtsi` and their bindings in `dts/bindings/behaviors/`.
The subsystem is central-only. Its position listener deliberately links before
soft power and keymap, and its keycode listener before HID. Keep those ordering
properties when changing `CMakeLists.txt`.

### Next unresolved memory design

SEQ delimiter and delete controls were discussed but are not implemented.
Current SEQ recording is a raw resolved key-down/key-up stream; releases already
preserve chord overlap, but there is no editable logical-step boundary.

The current recommendation to evaluate with the user is:

- `SYS + Space`: finish the current step and insert an all-keys-up boundary with
  roughly a 50 ms playback pause. Space without SYS remains capturable.
- `SYS + Backspace`: delete the most recently captured complete step. Backspace
  without SYS remains capturable.
- Saving implicitly finishes the final non-empty step; an empty delimiter is a
  no-op.
- Accept delimit/delete only when captured keys are released. Never truncate a
  raw press or release independently, because that can create stuck modifiers.
- A SYS control chord must consume that SYS gesture and cancel its single-tap,
  double-tap, and 600 ms save timers. Show a delimiter as `·` on MEM SET.

Before implementation, confirm that SYS+Backspace is physically comfortable and
whether “delimiter” means a logical all-up step/pause or something different.
There is also an interaction to account for: Backspace is now itself a
Backspace/Command hold-tap, so delimiter/delete controls should intercept
physical positions before keymap resolution.

## Display v2

The v2 handoff from `/Users/zeruichen/Downloads/handoff-v2` is implemented in
`boards/shields/nice_view_gem/widgets/screen.c`. The external handoff directory
is not part of the repo; all durable implementation details are summarized in
`docs/display-redesign-spec.md`.

- The display is currently inverted with
  `CONFIG_NICE_VIEW_WIDGET_INVERTED=y`.
- Dashboard bands are PWR, LAYER, BT, ME, and SYS. State changes invalidate only
  their row bands; idle dashboard traffic is zero.
- Pages are BOOT, dashboard, SLEEP, retained soft OFF, UF2, and MEM SET.
- Page changes use a seven-frame top-down reveal in which each physical row is
  transferred once. Layer roll, pairing, low-battery, USB bolt, memory-save, and
  caret animations share one delayable scheduler.
- Live state includes both batteries, selected endpoint and USB HID readiness,
  explicit right-half link and USB-power presence, all five BLE profiles,
  highest layer, memory slot types/live capture, Caps Lock, and MAC/WIN mode.
- Bootloader and soft-power code call narrow hooks in
  `src/toucan_display_hooks.h`, so the screen gets time to show retained UF2/OFF
  pages before reboot or System OFF.
- The most recently measured left build uses about 361,472 bytes flash and
  125,495 bytes RAM. The right build from the v2 work used 203,132 bytes flash
  and 43,676 bytes RAM. Re-measure after substantial changes.

The UI compiles and has been flashed as part of development, but the entire v2
acceptance matrix has not been visually checked on hardware. Preserve the
single 24,192-byte LVGL canvas and avoid adding high-rate redraws.

## Trackpad and scrolling

- TPS43 configuration is in `boards/shields/toucan/toucan_right.overlay`.
- Raw adjacent X/Y events are packed into private split input code `0x7F01` by
  `src/toucan_split_xy_packing.c`, then reconstructed on the central. This fixed
  a prior failure where trackpad activity saturated split transport and froze
  the right half.
- Right link/USB status uses separate low-rate private code `0x7F02` in
  `src/toucan_split_status.c`. Do not put display updates or battery chatter on
  the high-rate pointing path.
- Trackpad touch activates MOU. MOU is opaque (`&none` for unused positions), D
  sends `MOVE_Y(-10)`, F sends `MOVE_Y(10)`, and thumb keys are mouse buttons.
- ZMK smooth scrolling is enabled. The manual D/F scrolling rate/speed work was
  explicitly parked. Validate direction, effective rate, and feel before
  resuming it; do not assume the current settings meet the earlier 120 Hz goal.

## Battery estimation

`src/toucan_battery_estimator.c` takes a five-sample median, applies a nonlinear
single-cell LiPo voltage curve, restores compatible persisted state, filters
changes, and limits displayed movement to two percentage points per 60-second
report. `&battery` on FN+physical Z types current raw medians on demand. Startup
typing/calibration instrumentation was deliberately removed.

The full-charge readings observed during development were approximately
`left=4124mv` and `right=3721mv`. The right value is suspiciously low for a full
cell and may indicate per-half ADC/divider bias rather than battery state.
Battery calibration/estimator work was parked in favor of higher-priority input
and UI work. Side-specific signed millivolt offsets are supported, but do not
guess one without comparing against a trusted physical voltage measurement.

## Soft power and recovery

Each half independently enters nRF52840 System OFF using a local gesture:

```text
left:  A + X + D + V + outer-left SYS
right: ; + . + K + M + outer-right FN
```

Entry is tap outer thumb (≤200 ms), release, press it again within 300 ms and
hold, then complete the exact four character keys within 120 ms. Captured
characters are suppressed. Release all keys to power off. The left display
retains the OFF/wake-key page; the right trackpad is suspended.

Wake by holding the four character keys first, then the outer thumb, and keep
all five held until startup. Early boot validates the exact local matrix for up
to one second and returns rejected wakes to System OFF after all keys release.
A reset press is the recovery escape hatch. The implementation is
`src/toucan_soft_power.c`; do not replace it with a normal central-only combo.

## Safe continuation checklist

1. Inspect `git status` and do not overwrite unrelated user changes.
2. Keep `dts/toucan_layers.dtsi`, the keymap, display layer names, and docs in
   sync whenever layers move.
3. Format the keymap and run `git diff --check`.
4. Build every affected half. Keymap/central/display-only work normally needs a
   left build; shared shield, soft-power, bootloader, split, or trackpad work
   needs both.
5. Flash only with user authorization. The left is the central; many keymap-only
   changes require flashing only it.
6. After a commit, rebuild before flashing if the BOOT page SHA should match.
7. Do not commit generated `firmware/*.uf2` artifacts.
8. Report what was built, what was flashed, and what still needs physical
   validation.
