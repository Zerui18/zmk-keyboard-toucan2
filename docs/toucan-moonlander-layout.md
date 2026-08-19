# Toucan Moonlander-derived layout

This is the implemented Toucan port of the middle three Moonlander rows and
the four used Moonlander thumb keys. The Moonlander's innermost columns, top
row, extra row, and unreachable innermost thumb keys are intentionally omitted.

Layer indexes are defined once in [`dts/toucan_layers.dtsi`](../dts/toucan_layers.dtsi).
The keymap and trackpad processors both consume those names instead of numeric
layer literals.

## Base layer

```text
Esc     Q/NAV   W       E       R       T        Y       U       I       O       P/NAV   \
=/Ctrl  A/SYM   S/CLIP  D/EDIT  F       G        H       J       K       L       ;/SYM   '/Ctrl
-       Z/Shift X       C       V       B        N       M       ,       .       //Shift _

                    BT  Backspace  Tab        Enter  Space  SYS
```

The thumb positions above are shown from the outside edge of the left half to
the outside edge of the right half. Backspace/Tab and Enter/Space preserve the
Moonlander sequence on the two inner Toucan thumb switches. All four remain
single-role keys so ordinary Space and Backspace rollovers cannot become holds.

## Layer access

| Layer | Access | Purpose |
| --- | --- | --- |
| `BASE` | Default | Letters and punctuation |
| `SYM` | Hold A or semicolon | Moonlander symbols and numpad |
| `NAV` | Hold Q or P | Navigation, action shortcuts, and thumb modifiers |
| `CLIP` | Hold S before D or F | Copy, cut, and paste |
| `EDIT` | Hold D before S or F | Undo and redo |
| `MOUSE` | Touch the trackpad | Existing mouse-button thumb layout |
| `BT` | Hold the outer-left thumb | Bluetooth profiles and guarded clear |
| `SYS` | Hold the outer-right thumb | Battery, function keys, volume, and mode |
| `DANGER` | Hold both outer thumbs | Side-local bootloader and reset |

`DANGER` is a conditional layer and is active only while both `BT` and `SYS`
are held.

## Hold-tap and editing policy

All relevant hold-taps use a 200 ms tapping term.

- Q and P become NAV when followed by any key, matching the Moonlander rule.
- A, semicolon, Ctrl, and Shift become holds for opposite-hand chords. A
  same-hand rollover resolves as the printed tap key.
- S becomes CLIP early only for D or F.
- D becomes EDIT early only for S or F.
- Holding any of these keys through the tapping term still selects its hold.

The order-sensitive editing chords are:

```text
S then tap D    Command/Ctrl+C
S then hold D   Command/Ctrl+X
S then F        Command/Ctrl+V
D then S        Command/Ctrl+Z
D then F        Command/Ctrl+Shift+Z
```

Repeated taps of CLIP+D repeat copy. NAV+D retains the Moonlander tap dance:
one tap sends Shift+Enter and two taps send GUI+Enter.

## Symbol layer

```text
___  `  #  {  }  &        ^  1  2  3  *  F11
___  !  $  (  )  @        0  4  5  6  \  F12
___  %  |  [  ]  +        -  7  8  9  ~  ___
```

On SYM, physical J+K sends `.` and K+L sends `,`.

## Navigation layer

```text
___  ___  Alt+Ctrl+Left  Ctrl+Shift/Space  RAlt+RCtrl+Right  ___    Left Down Up Right ___ ___
___  ___  Cmd+[          Shift/GUI+Enter   Alt+Ctrl+Enter     Cmd+]  ___  ___  ___ ___   ___ ___
___  ___  ___            ___               ___                ___    ___  ___  ___ ___   ___ ___

                    ___  ___  ___        Cmd  Alt  ___
```

The special E position holds Ctrl+Shift and taps Ctrl+Shift+Space. The right
thumb Command and Alt bindings are single-role while NAV is active.

## Text combos

```text
J + K       -> "<-"
K + L       -> "->"
M + comma   -> "<="
comma + dot -> "=>"
```

The old password combo is deliberately not stored in the repository.

## Bluetooth layer

The five visible slots follow the same non-mirrored numpad geometry as SYM,
but on the left half. Labels 1-5 map to ZMK's internal profiles 0-4.

```text
___     BT1  BT2  BT3  ___  ___
BT_CLR  BT4  BT5  ___  ___  ___
___     ___  ___  ___  ___  ___
```

BT_CLR is inert when tapped. Hold it for one second to clear all bonds.

## System and danger layers

On SYS:

- Z types both halves' raw battery voltage through the selected endpoint.
- M toggles the persistent macOS/Windows shortcut mode.
- Esc unlocks ZMK Studio.
- The existing F1-F12 grid and volume-down/mute/volume-up keys are preserved.

macOS mode makes `&cmd_key` send GUI/Command. Windows mode makes the same
bindings send Control while leaving dedicated Control keys unchanged. This
applies to navigation/editing shortcuts and trackpad zoom.

On DANGER:

```text
Q             left bootloader       P             right bootloader
A             left reset            semicolon     right reset
```

Reset behaviors use event-source locality. The physical half containing the
danger key is therefore the half that resets or enters its UF2 bootloader.

## Mouse layer

Trackpad touch continues to hold MOUSE automatically. Alpha positions are
transparent and the thumb row remains:

```text
Middle  Left  Right        Left  Right  Middle
```

## Per-half soft power

Soft power now uses the new outer layer thumbs as its final keys:

```text
left:   A X D V + outer-left BT thumb
right:  ; . K M + outer-right SYS thumb
```

The arming sequence and exact-pattern validation are unchanged. Both outer
thumb switches use matrix row P0.29 and column P0.5, which is also the dedicated
System OFF wake intersection.
