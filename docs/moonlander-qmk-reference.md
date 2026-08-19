# Moonlander QMK layout reference

This file records the behavior of the old Moonlander QMK keymap so it can be
ported to the Toucan without depending on an Oryx-generated `keymap.c` or the
original keyboard. `PWD_SECRET` is intentionally not recorded.

## Physical base layout

The rows below follow `LAYOUT_moonlander` order: left hand from outside to
inside, then right hand from inside to outside.

```text
top:   ___  LAlt  Left  Up    Down  Right  ___ | ___  Left  Down  Up     Right  RAlt  RAlt+Shift
alpha: Esc  Q/L2  W     E     R     T      TG1 | TG2  Y     U     I      O      P/L2  Backslash
home:  =/Ctl A/L1 S/L3  D/L4  F     G      CG  | Meh  H     J     K      L      ;/L1  '/Ctl
lower: -    Z/Sh  X     C     V     B          | N    M     ,     .      //Sh   _
extra: Del  RGB  ___   ___   ___   TD0         | Caps Word  ___  ___    ___    ___   ___
thumb: Backspace  Tab  ___                    | ___  Enter  Space
```

Abbreviations:

- `Q/L2`, `A/L1`, etc. tap the character and hold the indicated layer.
- `=/Ctl`, `'/Ctl`, `Z/Sh`, and `//Sh` are modifier-taps.
- `TG1` and `TG2` latch layers 1 and 2.
- `CG` is the Mac/Windows modifier-mode toggle described below.
- `Meh` is Ctrl+Shift+Alt.
- `TD0` is tap dance 0.

## Layers

### Layer 1: symbols and numbers

Reached by holding A or semicolon, or latched with `TG(1)`.

```text
alpha: ___  `  #  {  }  &  ___ | ___  ^  1  2  3  *  F11
home:  ___  !  $  (  )  @  ___ | ___  0  4  5  6  \  F12
lower: ___  %  |  [  ]  +      | -    7  8  9  ~  ___
```

### Layer 2: navigation and actions

Reached by holding Q or P, or latched with `TG(2)`.

```text
top-right outer: bootloader
alpha left:      W=Alt+Ctrl+Left, E=Ctrl+Shift/ Ctrl+Shift+Space, R=RAlt+RCtrl+Right
alpha right:     Y=Left, U=Down, I=Up, O=Right
home left:       S=Cmd+[ , D=TD1, F=Alt+Ctrl+Enter, G=Cmd+]
extra left TD0:  Ctrl+C
right thumbs:    Right GUI, Right Alt
```

The E key is unusual: holding it produces Ctrl+Shift, while tapping it sends
Ctrl+Shift+Space rather than plain Space.

### Layer 3: clipboard layer

Reached by holding S. Only D and F deliberately chord with this same-hand
layer-tap.

```text
S then tap D:   Cmd/Ctrl+C
S then hold D:  Cmd/Ctrl+X
S then F:       Cmd/Ctrl+V
```

### Layer 4: undo/redo layer

Reached by holding D. Only S and F deliberately chord with this same-hand
layer-tap.

```text
D then S:  Cmd/Ctrl+Z
D then F:  Cmd/Ctrl+Shift+Z
```

Order therefore matters: S then D is copy/cut, while D then S is undo.

## Achordion policy

Every QMK tap-hold event passes through Achordion. Its timeout is 200 ms and
modifiers are eager once a chord is accepted.

- S becomes its layer-3 hold only when followed by D or F.
- D becomes its layer-4 hold only when followed by S or F.
- Q and P become layer-2 holds with any following key.
- Other tap-holds become holds for opposite-hand chords. Same-hand rolls stay
  taps unless the tap-hold key is held through the timeout.

This policy is important: it protects common same-hand rolls while retaining
the four intentional editing chords above.

## Tap dances

### TD0: left large thumb

- Single tap or hold: left GUI/Cmd in Mac mode, Ctrl in Windows mode.
- Double tap: Hyper+A (Ctrl+Shift+Alt+GUI+A).
- Three or more taps: repeat Hyper+A for each tap.

### TD1: layer-2 D

- Single tap: Shift+Enter.
- Double tap: GUI+Enter.
- Interrupted/repeated taps: repeated Shift+Enter.
- A single uninterrupted hold has no action in the old implementation.

### TD2: layer-3 D

- Single tap: Cmd/Ctrl+C.
- Single hold: Cmd/Ctrl+X.
- Double or repeated taps: repeated Cmd/Ctrl+C.

## Mac/Windows modifier mapping

The `CG_TOGG` setting is intentionally one-directional rather than QMK's
normal symmetric Ctrl/GUI swap.

- Mac mode: modifiers pass through unchanged.
- Windows mode: GUI/Cmd modifiers become Ctrl.
- Real Ctrl modifiers remain Ctrl, including the dedicated Ctrl mod-taps,
  Meh, and the layer-2 Ctrl+Shift key.

The TD0 and TD2 callbacks explicitly apply this mapping. TD1's double-tap is a
literal GUI+Enter in the old code.

## Combos

```text
J + K       -> "<-"
K + L       -> "->"
M + comma   -> "<="
comma + dot -> "=>"
4 + 5       -> "."   (physical J+K on layer 1)
5 + 6       -> ","   (physical K+L on layer 1)
J + I + L   -> PWD_SECRET
```

## Caps Word

Letters continue Caps Word with Shift applied. Digits, Backspace, Delete,
minus, and underscore continue it without adding Shift; minus deliberately
remains `-`. Any other key ends Caps Word.

## Lighting

The generated keymap used these HSV constants and a per-layer LED map:

```c
#define COLOR_OFF       {0, 0, 0}
#define COLOR_L0_BLUE   {139, 73, 238}
#define COLOR_L1_PURPLE {200, 255, 207}
#define COLOR_L2_ORANGE {25, 255, 255}
```

The exact generated LED-index array is presentation metadata rather than
typing behavior; preserve the semantic scheme when adding Toucan display or
lighting feedback: base blue, layer 1 purple, and layer 2 orange.
