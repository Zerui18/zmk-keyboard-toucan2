# Toucan2 display redesign reference

Generated 2026-08-31 (Asia/Singapore) from repository commit `8e97cbfa272a`
plus the current uncommitted Bluetooth-layer/profile-indicator work. This is a
design input and implementation map for replacing the left-half UI from
scratch.

## Executive specification

| Property | Value | Confidence / source |
| --- | --- | --- |
| Physical location | Left/central keyboard half only | Toucan shield overlays and build matrix |
| Panel | Sharp `LS013B7DH05` Memory LCD | Confirmed by Beekeeb's display-demo for this 144×168 breakout; the local devicetree itself uses the generic `sharp,ls0xx` compatible |
| Native resolution | **144 × 168 px**, portrait | Local devicetree; 24,192 pixels total; 6:7 aspect ratio |
| Display type | Reflective active-matrix memory-in-pixel LCD | Sharp specification |
| Color | Monochrome, 1 bit per pixel | Local LVGL and driver configuration |
| Pixel polarity | `1 = white`, `0 = black` | Zephyr LS0xx driver |
| Backlight | None in the panel and no firmware-controlled display light | Sharp catalog and local devicetree |
| Pixel pitch | 0.145 × 0.145 mm, about 175 ppi | Sharp specification |
| Active area | 20.88 × 24.36 mm | Sharp specification |
| Panel outline | 24.68 × 30.00 × 0.745 mm typical | Sharp catalog |
| Diagonal / weight | 1.26 in / 1.1 g maximum | Sharp catalog |
| Interface | Write-only serial/SPI-style interface, 8-bit, LSB first, active-high chip select | Local Zephyr driver |
| Configured SPI clock | **1.000 MHz** | Local devicetree; panel maximum is 1.1 MHz |
| Logical coordinate system | Origin at top-left; `x=0..143`, `y=0..167` | Current full-screen LVGL canvas |
| Rotation support | Not supported by the pinned LS0xx driver | A rotated design must transform its own coordinates/assets or introduce a wrapper driver |
| Brightness / contrast control | Not supported | Reflective panel; pinned driver returns `-ENOTSUP` |
| Partial updates | Full-width row bands only | `x` must be 0 and width must be 144; any contiguous subset of rows is legal |
| Current UI strategy | Event-driven, full-screen canvas redraw | Every relevant state event clears and redraws all 144×168 pixels |

The folder name `nice_view_gem` is inherited from the original widget project.
It must not be used to choose a screen template: the common nice!view is
160×68, whereas this Toucan display is 144×168.

## Refresh and update rates

There is no single useful "refresh rate" for a memory LCD. These are the
distinct rates that matter:

| Layer of the system | Rate / duration | Meaning |
| --- | --- | --- |
| Panel frame / COM-inversion timing | 57–66 Hz, nominal 60 Hz | Sharp's electrical timing range for the panel. This is polarity-maintenance timing, not the achievable application animation frame rate. |
| Panel COM frequency | 28.5–33 Hz | Resulting COM frequency in the Sharp specification |
| Optical response | 10 ms rise, 20 ms fall typical | Pixel response at 25 °C |
| ZMK display tick | 10 ms / 100 Hz | How often ZMK asks LVGL to do pending work; it does not force a transfer each time |
| LVGL default refresh period | 30 ms / 33.3 Hz | Upper scheduler rate for invalidated content |
| Full-screen SPI wire time | 26.896 ms minimum at 1 MHz | 3,362 transmitted bytes, before driver/SPI/software overhead |
| Theoretical full-screen bus ceiling | 37.18 frames/s | Pure wire-time bound; not a practical UI target |
| Practical current full-screen ceiling | About 30–33 frames/s | Bounded by the 30 ms LVGL refresh period and transfer overhead |
| Normal idle UI rate | 0 frames/s | Memory pixels retain their state; the current UI redraws only on events |
| WPM sampling | Once per second | ZMK recalculates WPM every second and resets its measurement window every five seconds |
| Battery reports | Once per 60 seconds | Both halves use `CONFIG_ZMK_BATTERY_REPORT_INTERVAL=60` |

### Transfer budget

The pinned driver sends each updated row as one line-address byte, 18 pixel
bytes, and one dummy byte. A transaction also has one leading command byte and
one trailing byte:

```text
bytes(rows) = 2 + 20 × rows
full screen = 2 + 20 × 168 = 3,362 bytes
full-screen wire time at 1 MHz = 3,362 × 8 µs = 26.896 ms
```

A 20-row status band is therefore 402 bytes, or 3.216 ms of ideal wire time.
The driver cannot update a narrow rectangle: even a one-pixel change transfers
the full 144-pixel width of every invalidated row. A new renderer should keep
independently changing regions in separate horizontal bands and avoid clearing
the full canvas when only one band changed.

### Practical redraw-rate policy

Use **10 FPS as the normal full-screen animation cap**. Up to 15 FPS is
reasonable for a short transition, while approximately 25–30 FPS is an
engineering stress ceiling rather than a sustainable UI target.

| Full-screen rate | Ideal SPI busy time per second | Recommended use |
| ---: | ---: | --- |
| 1 FPS | 2.7% | Status changes and low-rate telemetry |
| 5 FPS | 13.4% | Battery-conscious continuous animation |
| 10 FPS | 26.9% | Normal full-screen animation cap |
| 15 FPS | 40.3% | Brief page transitions, roughly one second |
| 20 FPS | 53.8% | Technically possible but inefficient |
| 25 FPS | 67.2% | Near the practical full-screen limit |
| 30 FPS | 80.7% | Avoid as a continuous operating mode |

Those percentages account only for bits on the wire. Each full frame also
requires 168 row writes plus the leading and trailing commands, LVGL rendering,
canvas-to-1-bpp conversion, interrupts, and scheduler time. At 30 FPS the SPI
bus alone is occupied for about 807 ms of every second, leaving little margin
for BLE, split-trackpad input, or timing variation. The known protocol and LVGL
limits are 37.18 and 33.3 FPS respectively; until measured on hardware, treat
25–30 FPS as the likely real ceiling and 10–15 FPS as the responsive operating
range.

Recommended policy for the replacement UI:

- Leave static status pages event-driven, normally producing no transfers.
- Animate persistent dashboard elements at about 5 FPS.
- Cap ordinary full-screen animation at 10 FPS.
- Permit up to 15 FPS only for short transitions.
- Use partial row-band redraws for anything that should move more smoothly.

The current histogram occupies rows `78..99`, a 22-row band. Updating only
that band takes 442 bytes or 3.536 ms of ideal wire time. At 10 FPS it consumes
about **3.5% of SPI time**, compared with 26.9% for ten full-screen updates.
Its WPM source still changes at most once per second, so a smoother animation
would interpolate visual motion between data samples rather than obtain more
frequent measurements.

The bare panel is specified at about 25 µW with no data update and 35 µW for a
one-frame-per-second test. Sharp does not specify a linear power curve for
higher application frame rates. More importantly, continuous redraws also wake
the MCU, run LVGL, and issue the row transfers. Do not derive a 320 mAh battery
runtime from panel power alone: benchmark total keyboard current on hardware if
animation duty cycle becomes a design requirement.

## Panel electrical and optical data

The following values are for the bare `LS013B7DH05`, not necessarily the total
Toucan breakout-board consumption.

| Property | Sharp specification |
| --- | --- |
| Recommended `VDD` / `VDDA` | 2.7–3.3 V, 3.0 V typical; `VDD >= VDDA` |
| Absolute maximum supply | 3.6 V |
| SPI clock | 1.0 MHz typical, 1.1 MHz maximum |
| Static display power | 25 µW typical, 125 µW maximum under the datasheet test condition |
| Updating power | 35 µW typical, 150 µW maximum at one full update per second with a vertical-stripe test image |
| Contrast ratio | 20:1 typical, 12:1 minimum in the specified reflective test |
| Reflectivity | 14.5% typical, 10% minimum |
| Viewing angle at contrast ratio >= 2 | 60° typical in each direction |
| Operating panel temperature | -20 to +70 °C |
| Storage temperature | -30 to +80 °C |

Primary references:

- [Beekeeb ZMK display demo](https://github.com/beekeeb/zmk-keyboard-displaydemo) identifies its matching breakout as the Sharp `LS013B7DH05`.
- [Sharp wearable/mobile LCD catalog](https://global.sharp/products/device/lineup/selection/lcd/mobile/index.html) lists geometry, interface, power, and mechanical data.
- [Sharp LS013B7DH05 device specification, 2024](https://www.sharpsde.com/fileadmin/products/Displays/Specs/LS013B7DH05_25Jan24_Spec_LD-2023X06.pdf) is the source for electrical timing and optical limits.
- [Nordic nRF52840 product page](https://www.nordicsemi.com/products/nrf52840) covers the MCU resources used below.

### VCOM and blanking verification item

Memory LCDs need periodic COM polarity inversion even when their picture is not
changing. The local overlay defines neither `extcomin-gpios` nor
`disp-en-gpios`. In the pinned Zephyr 3.5 LS0xx driver:

- VCOM is toggled only when `extcomin-gpios` exists.
- The serial VCOM command bit is defined but never used.
- `display_blanking_on/off()` returns `-ENOTSUP` without `disp-en-gpios`.

The Beekeeb breakout may provide VCOM and display-enable handling in hardware,
which would make the missing GPIOs intentional. Confirm that from the breakout
schematic or board designer before changing power behavior. From firmware alone
we can only prove that these signals are not MCU-controlled. This also explains
why the existing image remains frozen on the panel during soft shutdown: a
memory LCD naturally retains the last picture, and the current driver cannot
blank it.

Local implementation references:

- [`nice_view_gem.overlay`](../boards/shields/nice_view_gem/nice_view_gem.overlay)
- [`toucan_left.overlay`](../boards/shields/toucan/toucan_left.overlay)
- [Zephyr 3.5 LS0xx driver](https://github.com/zephyrproject-rtos/zephyr/blob/v3.5.0/drivers/display/ls0xx.c)
- [Zephyr 3.5 LS0xx binding](https://github.com/zephyrproject-rtos/zephyr/blob/v3.5.0/dts/bindings/display/sharp%2Cls0xx.yaml)

## Local wiring and software stack

### Left-half bus

| Signal | nRF52840 pin | Notes |
| --- | --- | --- |
| SCK | P1.13 | `SPI0`, nRF SPIM |
| MOSI / SI | P1.15 | Pixel and command output |
| MISO | P1.14 | Configured in the pinctrl group but unused by this write-only panel |
| CS / SCS | P0.03 | Active high |

The right half reuses P1.13/P1.15 as I²C for the TPS43 trackpad and has no
display. The display is therefore built only into the left central firmware.

### Pinned rendering stack

```text
ZMK/Zephyr events
        ↓
widget listeners update one shared status_state
        ↓
draw_top() clears and redraws a 144×168 LVGL canvas
        ↓
LVGL 8.3.7 invalidates and flushes row bands
        ↓
Zephyr 3.5 sharp,ls0xx driver
        ↓
1 MHz SPI → LS013B7DH05
```

Pinned versions in the West workspace:

- ZMK `v0.3`, commit `edf5c0814fd3ea202e43aad2d68fd32e882a518c`
- Zephyr `3.5.0`, commit `dacab4875df72109b96cc8977547a0dc04875bcd`
- LVGL `8.3.7`, commit `8a6a2d1d29d17d1e4bdc94c243c146a39d635fdd`

Relevant configuration:

| Setting | Current value |
| --- | --- |
| `CONFIG_TOUCAN_STATUS_SCREEN` | `2` |
| `CONFIG_LV_COLOR_DEPTH` | `1` |
| `CONFIG_LV_Z_BITS_PER_PIXEL` | `1` |
| `CONFIG_LV_Z_VDB_SIZE` | `100` (one full packed display buffer) |
| `CONFIG_LV_Z_MEM_POOL_SIZE` | 4,096 bytes |
| `CONFIG_LV_DPI_DEF` | 161 (layout metadata; native pixel pitch is about 175 ppi) |
| `CONFIG_ZMK_DISPLAY_TICK_PERIOD_MS` | 10 ms |
| `CONFIG_LV_DISP_DEF_REFR_PERIOD` | 30 ms |
| Display work queue | Dedicated, priority 5, 2,048-byte stack |
| `CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE` | Disabled |
| ZMK idle / sleep timeout | 30 seconds / 60 minutes |

Because blank-on-idle is disabled, entering normal IDLE leaves the screen as-is
and leaves the LVGL timer running. On the SLEEP transition the custom renderer
draws its sleep page and forces one flush before the MCU sleeps.

## Current display design

The active style is `CONFIG_TOUCAN_STATUS_SCREEN=2`: white foreground on a
black background.

| Region | Geometry | Current content and behavior |
| --- | --- | --- |
| Left battery | Arc dots span about `x=9..63`, `y=17..56` | Ten 6×6 round dots; fill count is `ceil(percent/10)`; `L` and numeric percent in the center |
| Right battery | Arc dots span about `x=81..135`, `y=17..56` | Same for split peripheral; hidden when the cached level is zero, so disconnected and a true 0% are indistinguishable |
| WPM history | `x=12..129`, `y=78..99` | 30 columns × 6 rows of 2×2 dots with 2 px gaps; scale clamps at 100 WPM |
| Layer name | `x=-23`, `y=115`, width 144 | Right-aligned 18 px QuinqueFive text such as `BASE`, `SYM`, `MOU`; falls back to `L#n` |
| BLE profiles | `x=126..133`; starts `y=105`, 10 px spacing | Five vertical 8×8 slots. Current working tree: dotted outline = empty, solid outline = bonded inactive, filled = bonded active, dotted plus 2×2 center = active but empty |
| Output | `x=-18`, `y=143`, width 136 | Right-aligned 8 px `USB`, `BLE`, or `NULL` |
| Sleep page | Icon at `x=24`, `y=32`; label at `x=12`, `y=140` | Static 96×96 Beekeeb bitmap plus `SLEEP` |

Current caveats worth fixing in the redesign:

- Every state change calls `fill_background()` and redraws the whole canvas, so
  even a tiny status change becomes a full-screen transfer.
- Output text says `BLE` whenever BLE is the selected transport, even if the
  active profile is not connected. The already-captured
  `active_profile_connected` field is not used by the renderer.
- The left `charging` flag really means only "USB power present" and is not
  drawn by style 2. The right charging flag is always false because the
  peripheral does not relay USB/charger state.
- The WPM chart advances only when the WPM value changes. It is a history of
  transitions, not 30 evenly spaced seconds.
- The sleep flag is module-global rather than part of the screen state model.
- Old style 0/1 widget files remain in the tree. Only the selected style is
  linked, but maintaining parallel renderers makes a from-scratch redesign
  harder than replacing them with one implementation.

Main UI entry points:

- [`custom_status_screen.c`](../boards/shields/nice_view_gem/custom_status_screen.c) creates the root LVGL screen and includes the fonts.
- [`widgets/screen.c`](../boards/shields/nice_view_gem/widgets/screen.c) owns state, event listeners, and full-screen redraws.
- [`widgets/screen.h`](../boards/shields/nice_view_gem/widgets/screen.h) owns the canvas buffers and state structure.
- [`widgets/`](../boards/shields/nice_view_gem/widgets) contains the existing draw functions.
- [`assets/`](../boards/shields/nice_view_gem/assets) contains generated fonts and images.

### Fonts and image assets

The embedded QuinqueFive font files cover printable ASCII and are 1 bpp,
uncompressed:

| Nominal font | LVGL line height / baseline | Linked flash footprint in the current build |
| --- | --- | --- |
| 8 px | 10 / 2 px | 1,579 bytes |
| 12 px | 12 / 2 px | 2,294 bytes |
| 18 px | 20 / 4 px | 4,316 bytes |
| 24 px | 25 / 5 px | Not linked; currently included in C source but removed by linker garbage collection |

The sleep bitmap is indexed 1-bit, 96×96, and occupies 1,160 bytes including
its eight-byte palette. Assets should be generated at exact 1:1 pixel size;
grayscale and antialiasing cannot be represented on this panel.

## Memory and performance budget

The nRF52840 provides a 64 MHz Cortex-M4F, 1 MiB flash, and 256 KiB RAM. The
bootloader/partition layout leaves a 788 KiB application flash region.

Figures from the latest successful left build in the persistent local toolchain:

| Resource | Used | Available region | Headroom |
| --- | ---: | ---: | ---: |
| Application flash (`text + data`) | 357,275 B | 806,912 B | 449,637 B |
| RAM (`data + bss`) | 202,752 B | 262,144 B | 59,392 B |
| Flash utilization | 44.28% | 100% | 55.72% |
| RAM utilization | 77.34% | 100% | 22.66% |

The right build, which has no display, uses 202,157 B of application flash and
56,166 B of RAM. That comparison is only directional because the halves also
differ in USB Studio, split role, and trackpad features.

### Current display-specific allocations

| Allocation | Bytes | Notes |
| --- | ---: | --- |
| One physically packed panel frame | 3,024 | `144 × 168 / 8` |
| LVGL Zephyr VDB (`buf0`) | 3,024 | Full packed flush buffer |
| One `lv_color_t[144×168]` canvas | 24,192 | LVGL stores one byte per pixel in this build despite the final packed 1 bpp output |
| Current three canvas arrays | 72,576 | `cbuf`, `cbuf2`, and `cbuf3` |
| Whole `screen_widget` object | 72,612 | Measured ELF symbol size |
| LVGL heap | 4,096 | Does not include the static canvas arrays |
| Display work-queue stack | 2,048 | Dedicated ZMK display thread |

Only `cbuf` is attached to the canvas. `cbuf2` is never used, and `cbuf3` is
passed to `draw_top()` but that parameter is ignored. Removing the two unused
arrays releases **48,384 bytes**. At the current build size this would reduce
RAM use from 77.34% to about 58.89%, increasing headroom from 59,392 B to
107,776 B before any other redesign changes.

That cleanup should be the first implementation change. A new UI does not need
three full-screen application canvases unless it intentionally implements
off-screen compositing or page transitions.

## State and telemetry available to a new design

### Already collected by the current screen

| Signal | Source / cadence | Quality and caveats |
| --- | --- | --- |
| Left battery state of charge | `zmk_battery_state_changed`, nominally every 60 s | Filtered custom estimator percentage, 0–100 |
| Right battery state of charge | `zmk_peripheral_battery_state_changed`, source 0 | Proxied across split BLE; set to 0 on disconnect, which is ambiguous with an empty battery |
| Left USB power presence | `zmk_usb_conn_state_changed` / `zmk_usb_is_powered()` | Power present is not proof of active charging or charge completion |
| Selected output transport | `zmk_endpoint_changed` | USB, BLE, or none |
| Active BLE profile | `zmk_ble_active_profile_index()` | Index 0–4, displayed to the user as BT1–BT5 |
| Active BLE connection | `zmk_ble_active_profile_is_connected()` | Already stored but currently ignored by the drawing code |
| Active profile bonded/open | `zmk_ble_active_profile_is_open()` | Already stored for the active profile |
| All five profile occupancy states | `zmk_ble_profile_is_open(i)` | Current uncommitted profile renderer queries this for each slot |
| Highest active layer | `zmk_keymap_highest_layer_active()` | Immediate layer events; name comes from `display-name` in the keymap |
| WPM | `zmk_wpm_state_changed` | Recalculated each second from released keycodes; five keystrokes = one word |
| Activity state | `zmk_activity_state_changed` | Active, idle, or sleep |

### Available in pinned ZMK with another listener or query

These require display code but no new hardware:

| Candidate UI signal | Integration note |
| --- | --- |
| Exact USB state | `ZMK_USB_CONN_NONE`, `POWERED`, or `HID` distinguishes charging-only USB from a ready USB HID link |
| Connection for any BLE profile | Query `zmk_ble_profile_is_connected(i)`, not just the active profile |
| Host lock LEDs | `zmk_hid_indicators_changed` provides Num Lock, Caps Lock, Scroll Lock, Compose, and Kana bits when the host reports them |
| Active modifiers | `zmk_modifiers_state_changed` provides modifier press/release changes; maintain a bitset in the display model |
| Last logical keycode | `zmk_keycode_state_changed` supplies usage page, keycode, implicit/explicit modifiers, press state, and timestamp |
| Last physical key / half | `zmk_position_state_changed` supplies position, local/peripheral source, press state, and timestamp |
| Mouse button state | `zmk_mouse_button_state_changed` supplies button bits and press state |
| Trackpad touch and gestures | Input callbacks can observe touch, relative motion, scroll, zoom, and gesture pseudo-buttons. Redraws must be throttled; never redraw per raw X/Y event. |
| Local filtered battery voltage | The custom battery sensor exposes `SENSOR_CHAN_GAUGE_VOLTAGE`; query it when the battery event arrives |
| Local raw median battery voltage | `toucan_battery_estimator_get_raw_millivolts()` already exists |
| Uptime / timers | `k_uptime_get()`; use a low-rate timer only while the value is visible |
| Firmware/build identity | Compile a short Git describe/hash string into the image; static information costs no event plumbing |

The pinned central split API does not expose a simple public per-peripheral
connection getter/event to widgets. The current code uses right battery `0` as
a disconnect proxy. A robust right-link icon should add a small central-side
status adapter instead of treating 0% as disconnected.

### Needs a small custom API or event

| Candidate UI signal | Why it is not ready today |
| --- | --- |
| macOS vs Windows shortcut mode | `windows_mode` is private static state in `toucan_platform_mode.c`; add a getter and change event |
| Right raw/filtered voltage on the screen | The private split path sends raw voltage only after `&battery` and immediately types it; cache/relay a structured value instead |
| Right USB/charging state | Not relayed from peripheral to central |
| True charge/discharge/charge-complete state | The current hardware path measures only voltage and USB presence; no charger-status integration or fuel gauge is exposed |
| Split RSSI / link quality | Requires polling the Zephyr Bluetooth connection or a custom metric and should be rate-limited |
| Trackpad health / packet rate | Add counters and a last-event timestamp around the existing packed X/Y transport |
| Soft-power arming/countdown | State is private to `toucan_soft_power.c`; exposing it would require an event, and the final screen cannot be actively blanked with the current display wiring |
| Caps Word state | Pinned ZMK v0.3 has no public Caps Word state event/getter, and the current Toucan keymap does not bind Caps Word |
| Battery current, capacity remaining, time remaining, or temperature | Not measurable with the present voltage-divider-only battery sensor |

## Current keymap context

The keyboard has 42 logical positions: three rows of 12 plus six thumb keys.
The display should assume the following named layer set and access scheme.

| Index | Display name | Access | Purpose |
| ---: | --- | --- | --- |
| 0 | `BASE` | Default | Letters, punctuation, home-row/layer tap-holds |
| 1 | `SYM` | Hold `A` or `;` | Symbols and right-side numpad |
| 2 | `NAV` | Hold `Q` or `P` | Navigation, action shortcuts, thumb modifiers |
| 3 | `CLP` | Hold `S`, then use `D`/`F` | Copy, cut, paste |
| 4 | `EDT` | Hold `D`, then use `S`/`F` | Undo, redo |
| 5 | `MOU` | Trackpad touch | Mouse buttons on thumbs |
| 6 | `BT` | Hold outer-left thumb | Profile selection and guarded profile clear |
| 7 | `SYS` | Hold outer-right thumb | Diagnostics, F-keys, volume, platform mode |
| 8 | `DNG` | Hold both outer thumbs | Bootloader and reset |

### Base

```text
Esc     Q/NAV   W       E       R       T        Y       U       I       O       P/NAV   \
=/Ctrl  A/SYM   S/CLIP  D/EDIT  F       G        H       J       K       L       ;/SYM   '/Ctrl
-       Z/Shift X       C       V       B        N       M       ,       .       //Shift _

                    BT  Backspace  Tab        Enter  Space  SYS
```

All thumb keys are deliberately single-role. The outer thumbs are layer keys;
the two inner keys per side preserve the Moonlander sequence.

### Symbol

```text
___  `  #  {  }  &        ^  1  2  3  *  F11
___  !  $  (  )  @        0  4  5  6  \  F12
___  %  |  [  ]  +        -  7  8  9  ~  ___
```

### Navigation and editing

```text
___ ___ Alt+Ctrl+Left Ctrl+Shift/Space RAlt+RCtrl+Right ___   Left Down Up Right ___ ___
___ ___ Cmd+[         Shift/GUI+Enter  Alt+Ctrl+Enter    Cmd+] ___  ___  ___ ___   ___ ___
___ ___ ___           ___              ___               ___   ___  ___  ___ ___   ___ ___

                           ___ ___ ___        Cmd Alt ___
```

- `CLP`: `S→D` tap = copy, `S→D` hold = cut, `S→F` = paste.
- `EDT`: `D→S` = undo, `D→F` = redo.
- `NAV+D`: one tap sends Shift+Enter; double tap sends GUI+Enter.
- `&cmd_key` sends GUI on macOS and Control on Windows; the selection persists.

### Mouse, Bluetooth, system, and danger

- `MOU` thumb row: `Middle Left Right | Left Right Middle`.
- `BT` current working tree: the left home row is
  `BT_CLR, BT1, BT2, BT3, BT4, BT5`. `BT_CLR` is inert on release before two
  seconds; a two-second hold clears only the selected profile.
- `SYS`: `Z` requests raw voltage from both halves, `M` toggles macOS/Windows
  mode, `Esc` unlocks Studio, the left side carries F1–F12, and the top-right
  positions carry volume down/mute/up.
- `DNG`: physical `Q`/`P` enter the bootloader on their own halves and physical
  `A`/`;` reset their own halves.

### Chording policy and combos

- All relevant hold-taps use a 200 ms tapping term.
- Ctrl, Shift, `A/SYM`, and `;/SYM` become holds early only for opposite-hand
  chords, preserving ordinary same-hand rolls.
- `Q/P` become NAV for any following key.
- `S` becomes CLP early only for `D`/`F`; `D` becomes EDT early only for
  `S`/`F`.
- Text combos use a 50 ms timeout: `J+K → <-`, `K+L → ->`,
  `M+, → <=`, `,+. → =>`.
- On SYM, the same physical `J+K` and `K+L` positions emit `.` and `,`.

### Trackpad behavior relevant to the display

- The Azoteq TPS43 is on the right half; touch automatically activates `MOU`
  on the left display.
- Relative X/Y are packed into one private split notification, then unpacked on
  the central. This prevents the v0.3 split BLE queue from stalling under motion.
- On `SYM` or `NAV`, X/Y is converted to scrolling.
- Pinch/zoom emits platform-aware Command/Control plus minus/equal.
- Three-finger gestures emit Mission Control/desktop/left/right shortcuts on
  macOS, with a 1.2 s throttle.
- The trackpad enters its lower-power mode after keyboard idle; its configured
  LP2 report interval is 640 ms.

### Per-half soft power

Soft power is independent on each half and uses a deliberate tap-release-hold
sequence before the five-key chord:

```text
left:  A X D V + outer-left BT thumb
right: ; . K M + outer-right SYS thumb
```

Shutdown input is swallowed. Wake requires the exact same five physical keys;
the outer thumb is the electrical wake source and early firmware validates the
full matrix pattern before normal Bluetooth, display, or trackpad startup.
The priming tap must be at most 200 ms, the second thumb press must begin within
300 ms, the capture window is 1.5 s, and the four character keys must converge
within 120 ms. Early wake validation waits up to 1 s for the exact chord and
requires it to remain stable for 20 ms.

The more detailed layout rationale remains in
[`toucan-moonlander-layout.md`](toucan-moonlander-layout.md), but its Bluetooth
section predates the current uncommitted one-row/two-second-clear changes. This
document records the current working tree.

## Custom firmware modules under `src/`

| Module | Current design | Display opportunities / constraints |
| --- | --- | --- |
| `toucan_battery_estimator.c/.h` | Five ADC readings 10 ms apart, median, per-half mV offset, nonlinear 3.3–4.2 V LiPo curve, 1/4 IIR filter, maximum 2 percentage points per report, persisted state restored when within 200 mV | Local raw, calibrated/filtered voltage and percent exist; only percentage is currently in the UI |
| `behavior_toucan_battery_readout.c` | Global `&battery` behavior asks each half for its own reading | Could be changed from typing text to opening a temporary diagnostics page |
| `toucan_battery_readout.c/.h` | Queues left raw mV, relays right raw mV through a private input-split message, waits for a ready endpoint, then types `left=...mv` and `right=...mv` | Reuse the relay but cache structured state; do not type when the intended action is screen-only |
| `toucan_platform_mode.c` | Persistent macOS/Windows mode; platform-aware command-key behavior keeps dedicated Ctrl unchanged | Add getter/event before showing an OS badge |
| `toucan_soft_power.c` | Per-half arming, exact shutdown chord suppression, peripheral suspension, retained marker, early wake validation, rejected-wake return to System OFF | Current display blanking call is unsupported by the local LS0xx node; a redesign can draw an explicit off page before shutdown |
| `toucan_split_xy_packing.c` | Packs adjacent signed 16-bit X/Y into one 32-bit private MSC event and reconstructs it centrally | Protect this path: never add high-rate screen traffic to the same split BLE flow |
| `toucan_split_protocol.h` | Reserves private MSC code `0x7F01` for packed X/Y | Extend with named message types if structured display telemetry is added |
| `toucan_usb_bootloader_touch.c` | Guarded 1200-baud DTR on/off sequence followed by 2400-baud DTR on/off within 2 s enters UF2 | Useful diagnostic icon/state only if explicitly exposed; current state is private and normally too brief to display |

The build selects these modules through the repository root
[`CMakeLists.txt`](../CMakeLists.txt) and [`Kconfig`](../Kconfig).

## Recommended architecture for the replacement UI

1. Remove `cbuf2` and `cbuf3`; keep one 144×168 canvas initially.
2. Replace the style switch and parallel widget families with one renderer.
3. Split state collection from drawing: one `display_state` model, event-specific
   reducers, and one renderer per horizontal band/page.
4. Track dirty bands and invalidate only their full-width row ranges. Avoid a
   screen-wide background fill for localized changes.
5. Represent connection states explicitly: selected transport, HID-ready,
   profile bonded, profile connected, and right-half connected are different
   facts.
6. Treat USB presence as `USB power`, not `charging`, until real charger state
   is available.
7. Keep high-rate input off the render path. For a touch visualizer, aggregate
   counters and redraw at no more than roughly 5–10 Hz for a short active window.
8. Stop periodic decorative animation after an interaction window. Static
   pixels cost almost nothing at the panel, while transfers, LVGL work, and CPU
   wakeups cost battery.
9. Generate only the exact font sizes and glyph ranges the design uses. At
   144×168, hand-tuned 1-bit assets are preferable to runtime scaling.
10. Verify VCOM/DISP handling before relying on display blanking or changing the
    sleep/soft-power sequence.

For a first visual mockup, use a native **144×168 monochrome canvas** with no
grayscale, top-left origin, and exact integer-pixel placement. The firmware has
ample flash; RAM and unnecessary full-screen redraws are the meaningful design
constraints.

## Implemented static dashboard

The handoff in `/Users/zeruichen/Downloads/handoff` is now implemented as the
single linked status renderer. It uses the handoff's embedded 5×6 status font,
4×5 layer font, pixel icons, and these five independently invalidated bands:

The implemented polarity is subsequently inverted from the original reference:
`CONFIG_NICE_VIEW_WIDGET_INVERTED=y` renders black UI pixels on a white
background. Disabling that option restores the handoff's white-on-black
polarity, including the sleep page.

| Band | Content rows | Live source |
| --- | --- | --- |
| PWR | `3..30` | Both battery percentages, exact selected USB/BLE state, all BLE profile states, explicit right split connection, and relayed right USB power |
| LAYER | `44..65` | Highest active ZMK layer |
| BT | `79..98` | Five live bonded/open slots, active profile, and connection state |
| MEM | `112..131` | Visual prototype only: slots 1 and 2 set, slot 3 empty |
| SYS | `145..160` | Host Caps Lock and persistent MAC/WIN mode |

The bolt artwork begins at `y=2` in the reference render, so the PWR dirty
range is deliberately expanded by one row to `2..30`; all other band ranges
match the handoff exactly. Boot and sleep/page transitions invalidate the full
frame. Ordinary state changes clear and invalidate only the affected rows.

The MEM band has no layer, behaviors, recording, playback, clearing, or
persistence yet. Its two set slots are intentionally hard-coded in
`zmk_widget_screen_init()` so the physical design can be judged before choosing
an interaction model.

The right half now relays USB-power presence using private input-split code
`0x7F02`, adjacent to but distinct from packed trackpad X/Y code `0x7F01`.
Central-role Bluetooth callbacks provide explicit split connect/disconnect
state, rather than treating a cached peripheral battery value of zero as a
disconnect. The platform behavior likewise exposes a getter and change event
for the footer badge.

The legacy style-specific widget files remain in the source tree for history,
but are no longer compiled. WPM is disabled, and the screen widget owns one
24,192-byte framebuffer instead of three. The current left build reports
116,402 bytes of RAM in use (44.40% of 256 KiB); its `screen_widget` symbol is
24,224 bytes including state and bookkeeping.
