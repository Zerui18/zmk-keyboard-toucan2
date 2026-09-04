# ZMK config for beekeeb Toucan2 Keyboard

[The beekeeb Toucan2 Keyboard](https://beekeeb.com/introducing-toucan2/) is a wireless split 42-key column‑stagger keyboard that a display and a trackpad, with an aggressive stagger on the pinky columns.

## Quick local development

The local toolchain runs in ZMK's official Docker builder. ZMK, Zephyr, and all
West modules are kept in a persistent Docker volume, so they do not clutter this
repository and subsequent builds are incremental.

Prerequisite: install and start [Docker Desktop](https://www.docker.com/products/docker-desktop/).

```sh
make doctor
make left
make right
```

The first build pulls the builder image and downloads the West workspace. Built
images are written to:

```text
firmware/toucan_left.uf2
firmware/toucan_right.uf2
```

To build, automatically reboot, and flash one USB-connected half, run one of:

```sh
make install left
make install right
```

The equivalent aliases are `make install-left` and `make install-right`. To
reuse an image that is already in `firmware/` without rebuilding it, use
`make flash-left` or `make flash-right`.

The single-half commands use that half's side-specific USB identity and the
same guarded bootloader handshake as the two-half command below. The default
bootloader path is `/Volumes/XIAO-BOOT`; override it when needed:

```sh
make install left BOOTLOADER=/Volumes/XIAO-BOOT
```

### One-command two-half flashing

The firmware exposes a side-specific USB serial port and supports a guarded
two-stage bootloader handshake (1200-baud DTR on/off followed by 2400-baud DTR
on/off within two seconds). Once both halves have this firmware, connect both
of them to the Mac with USB data cables, close ZMK Studio or any serial monitor,
and run:

```sh
make install-both
```

That command verifies that exactly one `Toucan Left` and one `Toucan Right`
runtime USB port are present, builds both images, puts the left half into UF2
and flashes it, waits for it to return, then does the same to the right. The
halves are handled sequentially so the shared `/Volumes/XIAO-BOOT` volume name
cannot send an image to the wrong side.

Useful related commands are:

```sh
make usb-check    # only verify and show the two runtime serial ports
make flash-both   # reuse the existing firmware/*.uf2 files
```

This automation currently targets macOS. It uses only the system Python,
`ioreg`, and the USB serial driver; no Python package is required. Override the
UF2 mount path with `BOOTLOADER=...` just as with the one-half commands.

There is one bootstrap step: firmware installed before this feature cannot be
rebooted from the host, and the old right-half firmware has no serial port.
Build that half with `make left` or `make right`, double-tap its reset so
`XIAO-BOOT` mounts, then run `make flash-left` or `make flash-right`. The
single-half command detects an already-mounted bootloader as a manual fallback.
After both halves have the guarded-handshake firmware, all install and flash
commands can enter UF2 without the reset switches.

Run `make help` for all development targets. In particular:

- `make setup` initializes the containerized West workspace without building.
- `make west-update` refreshes ZMK and all modules declared in `config/west.yml`.
- `make clean` removes compiled output while retaining downloaded dependencies.
- `make shell` opens an interactive shell inside the build environment.

# Customizations

- **Keymap and layers**: [config/toucan.keymap](config/toucan.keymap)
- **Implemented Moonlander-derived layout and controls**: [docs/toucan-moonlander-layout.md](docs/toucan-moonlander-layout.md)
- **Combos/chording**: [config/toucan_combos.dtsi](config/toucan_combos.dtsi)
- **Custom behaviors**: [config/toucan_behaviors.dtsi](config/toucan_behaviors.dtsi)
- **Shared firmware options**: [config/toucan.conf](config/toucan.conf)
- **Side-specific configs**: [boards/shields/toucan/toucan_left.conf](boards/shields/toucan/toucan_left.conf) and [boards/shields/toucan/toucan_right.conf](boards/shields/toucan/toucan_right.conf)
- **Display layout and widgets**: [boards/shields/nice_view_gem](boards/shields/nice_view_gem)
- **Display hardware, redraw budget, and dashboard implementation**: [docs/display-redesign-spec.md](docs/display-redesign-spec.md)
- **Swipe shortcuts**: the `swipe_button_mapper` node in [boards/shields/toucan/toucan.dtsi](boards/shields/toucan/toucan.dtsi)
- **Invert scroll / trackpad settings**: the `tps43_trackpad` node in [boards/shields/toucan/toucan_right.overlay](boards/shields/toucan/toucan_right.overlay)

## Battery estimation

Both halves keep the XIAO's stock voltage-divider driver for the hardware ADC
reading, then pass its voltage through the Toucan estimator in
[`src/toucan_battery_estimator.c`](src/toucan_battery_estimator.c). The estimator
takes the median of five readings, uses a nonlinear single-cell LiPo discharge
curve, smooths readings over time, limits the displayed change to two percentage
points per report, and saves its filtered state. The saved state prevents a
restart from replacing a settled estimate with one noisy startup sample.

The first boot after installing this firmware has no saved history yet. Later
restarts restore the previous estimate when it is consistent with the newly
measured voltage. The normal ZMK report interval is 60 seconds, so changes are
intentionally gradual.

The `320 mAh` rating affects runtime, not the voltage-to-percentage curve. The
current per-half calibration maps each observed full-charge raw median to the
curve's 4.2 V endpoint:

```ini
# config/toucan_left.conf: 4200 - 4121
CONFIG_TOUCAN_BATTERY_VOLTAGE_OFFSET_MV=79

# config/toucan_right.conf: 4200 - 4097
CONFIG_TOUCAN_BATTERY_VOLTAGE_OFFSET_MV=103
```

The value is signed millivolts and is added before the discharge curve is
evaluated. These offsets normalize the observed full points; they are not a
claim that the ADC readings match a multimeter. If a trusted physical voltage
measurement is later available, use actual battery voltage minus reported raw
voltage instead. Each half can have a different offset. Calibration cannot turn
voltage-only estimation into a true coulomb-counting fuel gauge.

### On-demand voltage readout

Hold the outer-right `FN` thumb and press the physical `Z` key bound to
`&battery`. It types each half's latest recorded five-sample median through the
currently selected USB or Bluetooth connection. These are the raw, uncalibrated
readings:

```text
left=4121mv
right=4097mv
```

The behavior is global, so the one key press asks each half for its own recorded
voltage. The right relays its result to the left central, which types both lines.
If the right is disconnected, only the left line is available.

## Per-half soft power

Each half has an independent, deliberately armed pseudo-power chord. It uses ZMK
soft off to disconnect Bluetooth, suspend the display or trackpad and enter the
nRF52840's very-low-power System OFF state.

- Left: tap and release the outer-left `SYS` thumb, press and hold it again, then
  press `A`, `X`, `D`, and `V` while continuing to hold the thumb.
- Right: do the same with the outer-right `FN` thumb and the mirrored `;`, `.`,
  `K`, and `M` positions.

Start with no keys held. The first thumb tap must take at most 200 ms; press the
thumb again within 300 ms and keep holding it. This arms a 1.5-second capture
window in which the four character keys must all be pressed within 120 ms. Their
presses and releases are swallowed, so nothing is typed. Any other key, early
release or timeout cancels the attempt. Once recognized, release everything to
enter System OFF.

The listener only observes the outer layer thumb and never delays it: an
ordinary hold still activates and releases SYS or FN normally. Only the
deliberate tap-release-hold sequence starts capturing the shutdown keys. It
powers off only the half on which it was physically entered, so repeat it on
the other half to power off both.

To wake a half, hold its four character keys, then press and hold its outer
layer thumb and keep all five down until the half starts. While the MCU is off,
only the final thumb switch is electrically capable of waking it; after reset,
firmware scans only the key matrix for up to one second. It returns to System
OFF after the entire matrix is released unless exactly the other four keys are
also held. Normal Bluetooth, split, display, and trackpad startup happens only
after this check passes. A reset button press remains an escape hatch,
including for entering the UF2 bootloader.

The timing values are configurable in [Kconfig](Kconfig). The implementation is
in [src/toucan_soft_power.c](src/toucan_soft_power.c), rather than a normal ZMK
combo, so it runs locally and still works on the right peripheral when the left
central is off.

## Staying current with Beekeeb

This checkout uses `origin` for the personal fork and `upstream` for Beekeeb's
repository. Commit or stash current work, then merge upstream updates with:

```sh
make sync-upstream
git push origin main
```

If Git reports a conflict, resolve the marked files, stage them with `git add`,
and finish the merge with `git commit`. GitHub Actions remains enabled, so every
push to the fork also performs the three builds in [build.yaml](build.yaml).

# License

The code in this repo is available under the MIT license.

The included shield nice_view_gem is modified from https://github.com/M165437/nice-view-gem licensed under the MIT License.

The linked trackpad module is based on https://github.com/geeksville/zmk_driver_azoteq

ZMK code snippets are taken from the ZMK documentation under the MIT license.

The embedded font QuinqueFive is designed by GGBotNet, licensed under under the SIL Open Font License, Version 1.1.
