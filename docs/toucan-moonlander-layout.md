# Toucan Moonlander-derived layout

This is the current Toucan port of the useful middle three Moonlander rows and
four inner thumb keys. The Moonlander's top and extra rows, innermost columns,
and unreachable innermost thumb keys are omitted. Toucan's two outer thumb
keys provide the new system and function layers.

The physical order in every diagram is left half, then right half. `___` means
transparent: the binding from the next active layer below is used. Layer
indexes are defined in [`dts/toucan_layers.dtsi`](../dts/toucan_layers.dtsi),
and the keymap, display, combos, and trackpad processors use those shared names.

## Base layer

Tap/hold pairs are written as `tap/hold`.

```text
Esc        Q/NAV    W       E       R       T       | Y       U       I       O       P/NAV       Backslash
Equal/Ctrl A/SYM    S/CLIP  D/EDIT  F       G       | H       J       K       L       Semicolon/SYM Quote/Ctrl
Minus      Z/Shift  X       C/APP   V       B       | N       M       Comma   Dot     Slash/Shift  Underscore

                                SYS  Backspace  Tab/Cmd | Enter/Option  Space  FN
```

The thumb row is shown from the outside of the left half to the outside of the
right half. Backspace, Tab/Command, Enter/Option, and Space occupy the easier
inner thumb positions. The harder outer thumbs are dedicated momentary layer
keys. The left Tab thumb sends Tab on tap and holds Command on macOS or
Control in Windows mode. The right Enter thumb sends Enter on tap and holds
left Option/Alt (ordinary Alt, not AltGr, in Windows mode). Backspace and Space
remain single-role.

For Option+Command+P on BASE, hold left Tab and right Enter, then tap and
release P before releasing the thumbs. This puts the two modifiers on separate
thumbs. NAV retains its dedicated Command/Control and Alt thumb bindings.

## Layers and access

The display uses the short names shown below.

| Index | Source name | Display | Access | Purpose |
| ---: | --- | --- | --- | --- |
| 0 | `BASE` | `BASE` | Default | Letters and punctuation |
| 1 | `SYM` | `SYM` | Hold A or semicolon | Symbols and right-side numpad |
| 2 | `NAV` | `NAV` | Hold Q or P | Navigation and action shortcuts |
| 3 | `CLIP` | `CLP` | Hold S, then use D/F | Copy, cut, and paste |
| 4 | `EDIT` | `EDT` | Hold D, then use S/F | Undo and redo |
| 5 | `APP` | `APP` | Hold C, then use X/V | Cycle backward/forward through applications |
| 6 | `MOUSE` | `MOU` | Touch the trackpad | Opaque pointer controls, D/F scroll, and thumb buttons |
| 7 | `SYS` | `SYS` | Hold outer-left thumb | Bluetooth profiles, five persistent memory slots, Hyper shortcuts, and platform mode |
| 8 | `FN` | `FN` | Hold outer-right thumb | Diagnostics, F-keys, brightness, and volume |
| 9 | `DANGER` | `DNG` | Hold both outer thumbs | Side-local bootloader and reset |

`DANGER` is a conditional layer: it exists only while `SYS` and `FN` are both
active. Higher numbered active layers have normal ZMK priority.

## Hold-tap resolution

All typing-facing hold-taps share the balanced flavor, a 200 ms tapping term,
and a 125 ms prior-idle guard. This includes the base-layer modifier and layer
taps, both dual-role thumbs, and NAV F. The settings have one source of truth:
`TOUCAN_TYPING_HOLD_TAP_POLICY` in
[`config/toucan_behaviors.dtsi`](../config/toucan_behaviors.dtsi).

- If the leader follows another non-modifier keypress within 125 ms, it resolves
  immediately as its tap action. Holding longer cannot undo this decision.
- Otherwise, releasing the leader before the target, and before 200 ms, is a
  typing rollover. An opposite-hand keypress alone no longer selects a hold.
- Keeping the leader held while pressing and releasing an eligible target
  selects the hold, without waiting for the full tapping term.
- Holding the leader alone through 200 ms also selects the hold, unless the
  prior-idle or thumb quick-tap guard already forced a tap.

Position filters still limit which targets can select a hold before 200 ms:

- Equal/Ctrl, Z/Shift, Quote/Ctrl, Slash/Shift, A/SYM, Semicolon/SYM, and NAV F
  require an opposite-hand target.
- S can select CLIP only with D/F, D can select EDIT only with S/F, and C can
  select APP only with X/V.
- Q/P and the Tab/Command and Enter/Option thumbs have no position filter.

The dual-role thumbs also have a 200 ms quick-tap window: tap the key and press
it again within 200 ms of the first press to hold and repeat its tap behavior
instead of engaging its modifier. Backspace repeats normally without a hold-tap
delay.

For example, pressing A, pressing N, releasing A, then releasing N within
200 ms types `an`, not `-`. The same release order with Quote and S types `'s`,
not Ctrl+S. To deliberately chord, pause at least 125 ms after the previous
non-modifier keypress, then hold the leader while tapping and releasing the
target. Fast typing that releases the target first after a pause can still
select a hold; balanced resolution cannot distinguish that sequence from an
intentional chord.

Action-only hold-taps are deliberately excluded: CLIP D remains tap-preferred
with a 200 ms hold to cut, memory slots retain their 400 ms replacement hold,
and Bluetooth clear retains its two-second safety hold. These actions must
not become holds merely because another key was tapped.

## Symbol layer (`SYM`)

```text
___  Grave    Hash  LeftBrace  RightBrace  Ampersand | Caret  1  2  3  Asterisk  F11
___  Exclaim  Dollar LeftParen RightParen  At        | 0      4  5  6  Backslash F12
___  Percent  Pipe   LeftBracket RightBracket Plus   | Minus  7  8  9  Tilde     ___

                                      ___  ___  ___  | ___  ___  ___
```

On SYM, the physical J+K positions produce a period and K+L produce a comma.
This places both punctuation keys inside the right-side numpad without adding
dedicated bindings.

## Navigation layer (`NAV`)

```text
___  ___  Alt+Ctrl+Left  Alt+Ctrl+Enter   RAlt+RCtrl+Right  ___ | Left  Down  Up  Right  ___  ___
___  ___  Cmd/Ctrl+[     Shift/GUI+Enter  Ctrl+Shift/Space  Cmd/Ctrl+] | ___ ___ ___ ___ ___ ___
___  ___  ___            ___               ___                ___ | ___   ___   ___ ___    ___  ___

                                                    ___  ___  ___ | Cmd/Ctrl  Alt  ___
```

W/E/R group Alt+Ctrl+Left, Alt+Ctrl+Enter, and RAlt+RCtrl+Right together.
The F position is itself a hold-tap: tapping sends Ctrl+Shift+Space, while
holding it with an opposite-hand key holds Ctrl+Shift. The D position is a tap
dance: one tap sends Shift+Enter and two taps send GUI+Enter. That double-tap
uses GUI directly and is not changed by the macOS/Windows mode.

The right inner thumb sends right Command on macOS or right Control on Windows.
The adjacent thumb always sends right Alt.

## Clipboard, editing, and app switching

For a balanced chord, keep the leader held, tap and release the target, then
release the leader.

```text
S then tap D       Command/Ctrl+C
S then hold D      Command/Ctrl+X
S then tap F       Command/Ctrl+V

D then tap S       Command/Ctrl+Z
D then tap F       Command/Ctrl+Shift+Z

C then tap X       Shift+Command+Tab (macOS) or Shift+Alt+Tab (Windows)
C then tap V       Command+Tab (macOS) or Alt+Tab (Windows)
```

CLIP+D uses a separate tap-preferred 200 ms hold-tap: a tap copies and a hold
cuts. Repeated D taps therefore repeat copy. Once APP resolves as a hold, C
keeps Command (macOS) or Alt (Windows) pressed. Releasing X/V therefore leaves
the application switcher visible without advancing it again; tap X or V as
many times as needed, then release C to release the modifier and choose the
highlighted application.

## Text combos

The following positional combos have a 50 ms timeout:

```text
J + K       -> "<-"
K + L       -> "->"
M + comma   -> "<="
comma + dot -> "=>"
```

They are active on BASE, NAV, CLIP, EDIT, and APP. The J/K/L positions have the
separate period/comma meanings described above on SYM. The old password combo
is deliberately not stored in this repository.

## System layer (`SYS`)

Hold the outer-left thumb. Persistent memory occupies the left top row;
Mode and Hyper shortcuts occupy the left home row; Bluetooth occupies the bottom row:

```text
MEM_CLR  MEM1     MEM2     MEM3  MEM4  MEM5 | ___  ___  ___  ___  ___  ___
Mode     Hyper+A  Hyper+V  ___   ___   ___ | ___  ___  ___  ___  ___  ___
USB/CLR  BT1      BT2      BT3   BT4   BT5 | ___  ___  ___  ___  ___  ___

                                  ___  ___  ___ | ___  ___  ___
```

SYS+Equal toggles the active host's saved [Mac/Windows mode](#platform-mode).
It is the leftmost key on the left home row.

SYS+A sends Hyper+A and SYS+S sends Hyper+V. Hyper means
Ctrl+Shift+Alt+GUI (Control+Shift+Option+Command on macOS). These are ordinary
key bindings with all four modifiers, not hold-taps or tap dances, and do not
change with the macOS/Windows platform mode.

BT1-BT5 on Z/X/C/V/B select ZMK profiles 0-4 and request Bluetooth output,
even while USB is plugged in. USB/CLR (the former BT_CLR key) is on Minus:
tap it to request USB, or hold it for two seconds to clear only the currently
selected BLE profile. It does not clear every bond.

Output selection uses ZMK's availability fallback: a USB tap leaves BLE active
if USB isn't ready; a requested BLE profile takes over when connected. The
display shows the actual output, not just the requested preference. Its memory
row is above the Bluetooth row. BT1-BT5 retain their bonded/open markers while
USB is active, but no BT slot is underlined or shown selected.

**Windows BLE compatibility:** the left/central firmware disables 2M PHY
with `CONFIG_BT_CTLR_PHY_2M=n`, following ZMK's documented Intel/Realtek
compatibility workaround. Its host and inter-half links therefore use 1M.
This resolved the reported Windows disconnects without changing battery
reporting, connection timing, queue sizes, or USB fallback.
No bond reset is needed. To revert the workaround, remove that override from
`boards/shields/toucan/toucan_left.conf` and rebuild/flash the left half.

For a memory slot:

- Tap MEM1–MEM5 to replay it.
- Hold a slot for 400 ms to replace it and enter capture in SEQ mode. Release
  SYS after capture starts; the ordinary keymap, hold-taps, layers, macros, and
  combos continue to resolve, but their resulting key events are captured and
  never sent to the host.
- Hold MEM_CLR and tap a slot to erase only that slot.

SEQ stores up to 64 resolved press/release actions, including chords,
modifiers, Enter, and Escape, with the typed shorthand below.
TEXT stores literal text up to 64 characters and never interprets shorthand names.
While capture is active, the physical SYS thumb becomes the control key:

- Single-tap SYS to switch SEQ/TEXT, but only while the capture is empty.
- Double-tap SYS to cancel (start the second tap within 275 ms). Escape remains
  available to a SEQ and is not a cancellation key.
- Hold SYS for 600 ms to save a non-empty capture.

Slots and their `sequence`/`text` type survive resets in Zephyr settings. Slot
contents are shown only on the MEM SET capture page; the dashboard exposes only
occupancy, with a small prime mark distinguishing sequence slots.

Both capture previews distinguish uppercase and lowercase. TEXT displays its
literal stored characters; SEQ tracks held Shift, action modifiers, and Caps
Lock while formatting key labels. SEQ replays the resulting key events,
whereas TEXT compensates for host Caps Lock to preserve literal case. See the
[memory font design](memory-font.md) for glyph metrics and validation.

Symbols entered from SYM appear as single characters: `+` shows `+`, not a
Shift icon followed by `+`. The same applies to the other shifted symbols.
Their required Shift remains in the recorded key events for correct playback.
Typing `cmd` then entering SYM `+` shows Cmd + `+`; deliberately typed or held
Shift stays visible, so `shiftcmd4` still shows Shift + Cmd + `4`.

### SEQ modifier-name shorthand

Type `ctrl`, `shift`, `alt`, or `cmd` in SEQ to replace the completed name with
its modifier icon immediately, without a delimiter or waiting for save. Names
are case-insensitive; ordinary letters retain their original case. Enter names
and the target consecutively, without spaces: a space is a recorded key, not
a separator.

The named target `del` records **forward Delete**, displayed as `DEL`. It uses
the same case-insensitive, immediate recognition as the modifier names:
`cmddel` records Cmd+Delete, and `shiftcmddel` shows Shift, Cmd, then `DEL`.
Delete consumes the pending modifiers, so `cmddela` records Cmd+Delete followed
by an unmodified `a`. `deldel` records two Delete taps. This records a key for
playback; it does **not** erase the last item from the capture or mean Backspace.

- Names combine and apply to the next non-modifier key: `ctrlshiftp` records
  Ctrl+Shift+P, `cmdc` records GUI+C, and `ctrlsctrlc` records Ctrl+S then Ctrl+C.
  `cmd` always means left GUI, regardless of the macOS/Windows mode.
- Named modifier icons stay in entry order, both while the prefix is pending
  and after the target key is entered. `shiftcmd4` shows Shift then Cmd then `4`;
  `cmdshift4` shows Cmd then Shift then `4`. Both replay the same chord.
  Repeated names keep their first position without adding a duplicate icon.
- Named modifiers affect playback, not the character labels being entered.
  `shiftcmd4` shows Shift and Cmd icons followed by `4`, not `$`, and replays
  Shift+GUI+4. While entering `shiftc`, the provisional `c` stays lowercase.
  Physical Shift, shifted key bindings, and Caps Lock still determine literal
  case and symbols, so physically typing `$` continues to display `$`.
- The target can also be Enter, Space, Escape, Backspace, or another non-letter
  key. The named modifiers accompany that key's press and release, not later
  key presses. Physical chords remain available.
- Partial names remain visible as letters. In `ctrlshift`, the provisional
  Ctrl+S becomes Ctrl+Shift when the final `t` is pressed. Saving names without
  a target presses the modifiers in entry order and releases them in reverse,
  never leaving a stuck hold.
- These short names are reserved even inside words: `shifted` records Shift+E
  then D, and `model` records `m`, `o`, then Delete. Use TEXT to preserve literal
  words containing them. A non-letter press or physical modifier change breaks
  a name; letters already chorded with Ctrl, Alt, or GUI are not interpreted as
  names.

The capture retains up to 1,024 source events while compiling them to the
64-action saved sequence. If either limit is exceeded, MEM SET shows `FULL`
and refuses to save, leaving the previous slot intact. A partial name can
temporarily exceed the action limit and fit again when completed. Otherwise,
double-tap SYS to cancel and record a shorter sequence.

## Function layer (`FN`)

Hold the outer-right thumb:

```text
Studio  ___      F3  F2  F1  F10 | ___   BriDown  BriUp  ___  ___  ___
___     ___      F6  F5  F4  F11 | Mute  VolDown  VolUp  ___  ___  ___
___     Battery  F9  F8  F7  F12 | ___   ___      ___    ___  ___  ___

                              ___  DEL  ___ | ___  ___  ___
```

- Studio unlocks ZMK Studio.
- FN + Backspace sends forward Delete: hold the outer-right thumb and tap the
  left Backspace thumb. Plain Backspace is unchanged.
- Battery requests the latest raw median ADC voltage from both halves. Each
  available result is typed through the selected USB/BLE endpoint as
  `left=NNNNmv` or `right=NNNNmv`, followed by Enter.
- F1-F9 mirror SYM's 1-9 grid horizontally onto the left split:
  W/E/R are F3/F2/F1, S/D/F are F6/F5/F4, and X/C/V are F9/F8/F7.
  F10/F11/F12 occupy the inner column on T/G/B, top to bottom.
- Volume down/up are on J/K, directly below NAV's Down/Up keys on U/I.
  Mute is immediately to their left on H.
- Brightness down/up are on U/I, directly above volume down/up. These send
  standard display-brightness consumer keys; host/display support determines
  whether they adjust a particular monitor.

## Platform mode

SYS+Equal toggles and persists the macOS/Windows shortcut mode for the active
output: the current BLE profile, or USB's separate setting. The dashboard
footer shows an Apple logo for macOS or a Windows logo for Windows.

In macOS mode, `&cmd_key` adds GUI/Command; in Windows mode it adds Control.
Dedicated Control bindings are unchanged. `&app_switch_layer` instead holds
Command on macOS or Alt on Windows for the lifetime of the layer. The same
per-host mode therefore controls navigation/editing shortcuts, APP switching,
and trackpad pinch zoom.

For each Bluetooth device, select its profile and wait until Bluetooth is the
active output, then set Mode once with SYS+Equal. Switching devices automatically
restores that profile's choice, including after a restart. USB has its own
choice; changing the background BLE profile while output remains USB does not
change USB's mode. Choices belong to profile slots and survive clearing/re-pairing
a slot. On upgrade, the old global mode seeds any slots without a saved choice;
without an old setting they default to macOS.

EDT+F sends Cmd+Shift+Z in macOS mode and Ctrl+Shift+Z in Windows mode.
The Command wrapper preserves any modifiers already encoded in the binding,
including Redo's Shift, and releases the same chord even if the mode changes
while the key is held.

`make test-platform` runs native ZMK regression tests for these shortcuts,
modifier-only Command keys, per-host selection, legacy migration, settings
save/load, and press/release pairing across host changes. The profile tests use
synthetic endpoint events and a test storage backend, including overlapping
deferred saves; they don't require or emulate a Bluetooth radio.

## Danger layer (`DNG`)

Hold both outer thumbs simultaneously:

```text
___  LeftBootloader  ___  ___  ___  ___ | ___  ___  ___  ___  RightBootloader  ___
___  LeftReset       ___  ___  ___  ___ | ___  ___  ___  ___  RightReset       ___
___  ___             ___  ___  ___  ___ | ___  ___  ___  ___  ___              ___

                                  ___  ___  ___ | ___  ___  ___
```

These bindings use event-source locality. Physical Q and A operate on the left
half; physical P and semicolon operate on the right half. Entering one half's
bootloader or resetting it does not intentionally reset the other half.

## Trackpad and mouse layer (`MOU`)

Touching the right trackpad automatically holds MOUSE and makes the left display
show `MOU`. Every unused key is `&none`, so MOU is opaque and no BASE binding
falls through while it is active. Its assigned alpha keys are:

```text
D  Scroll up
F  Scroll down
```

These use HID Resolution Multipliers and a 10-unit/second velocity. At the
host's maximum 16x wheel resolution, this is approximately 0.625 wheel notches
per second. Because the HID descriptor changes, previously bonded Bluetooth
hosts must be cleared and paired again after first flashing this version. For
USB, disconnect and reconnect the cable once so the host reloads the descriptor.

The thumb row becomes:

```text
                         Middle  Left  Right | Left  Right  Middle
```

Trackpad motion becomes scrolling while SYM or NAV is held. Pinch zoom sends
Command+minus/equal on macOS or Control+minus/equal on Windows. Directional
swipes in the current build send Ctrl+GUI+arrow shortcuts; unlike pinch zoom,
those swipe bindings do not currently follow the runtime platform toggle.

## Per-half soft power

Each half enters and wakes from System OFF independently. Its exact five-key
pattern is:

```text
left:   A + X + D + V + outer-left SYS thumb
right:  semicolon + dot + K + M + outer-right FN thumb
```

To shut down one half:

1. With no other key held, tap and release that half's outer thumb within
   200 ms.
2. Press the same thumb again within 300 ms and keep holding it.
3. Within the 1.5-second capture window, press the four character keys. All
   four presses must fit inside a 120 ms window, with no extra key and no early
   release.
4. Release all five keys. The left first reveals its retained OFF page, then
   the half enters System OFF; the right has no display and powers down after
   its normal settling interval.

The four captured character presses are consumed and are not typed into the
host application. The right half puts the trackpad to sleep; normal BLE and
keyboard activity then stop. The Sharp memory LCD retains the left OFF page
without refresh traffic while the MCU is off.

For a reliable wake, hold the four character keys first, press the same outer
thumb last, and keep the exact five-key pattern held. Early boot accepts it when
it remains stable for 20 ms within the one-second validation window. A wrong,
extra, or incomplete pattern is rejected and the half returns to System OFF
after all keys are released. Reset and USB/VBUS startup remain recovery paths
that bypass chord validation.
