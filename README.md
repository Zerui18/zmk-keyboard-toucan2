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

To build and flash a half, double-tap reset on that half so the XIAO bootloader
is mounted, then run one of:

```sh
make install left
make install right
```

The equivalent aliases are `make install-left` and `make install-right`. The
default bootloader path is `/Volumes/XIAO-BOOT`; override it when needed:

```sh
make install left BOOTLOADER=/Volumes/XIAO-BOOT
```

Run `make help` for all development targets. In particular:

- `make setup` initializes the containerized West workspace without building.
- `make west-update` refreshes ZMK and all modules declared in `config/west.yml`.
- `make clean` removes compiled output while retaining downloaded dependencies.
- `make shell` opens an interactive shell inside the build environment.

# Customizations

- **Keymap and layers**: [config/toucan.keymap](config/toucan.keymap)
- **Combos/chording**: [config/toucan_combos.dtsi](config/toucan_combos.dtsi)
- **Custom behaviors**: [config/toucan_behaviors.dtsi](config/toucan_behaviors.dtsi)
- **Shared firmware options**: [config/toucan.conf](config/toucan.conf)
- **Side-specific configs**: [boards/shields/toucan/toucan_left.conf](boards/shields/toucan/toucan_left.conf) and [boards/shields/toucan/toucan_right.conf](boards/shields/toucan/toucan_right.conf)
- **Display layout and widgets**: [boards/shields/nice_view_gem](boards/shields/nice_view_gem)
- **Display style selection**: `CONFIG_TOUCAN_STATUS_SCREEN` in [boards/shields/toucan/toucan_left.conf](boards/shields/toucan/toucan_left.conf)
- **Swipe shortcuts**: the `swipe_button_mapper` node in [boards/shields/toucan/toucan.dtsi](boards/shields/toucan/toucan.dtsi)
- **Invert scroll / trackpad settings**: the `tps43_trackpad` node in [boards/shields/toucan/toucan_right.overlay](boards/shields/toucan/toucan_right.overlay)

## Per-half soft power

Each half has an independent, deliberately armed pseudo-power chord. It uses ZMK
soft off to disconnect Bluetooth, suspend the display or trackpad and enter the
nRF52840's very-low-power System OFF state.

- Left: tap and release the `&mo 1` (NAV) thumb, press and hold it again, then
  press `A`, `X`, `D`, and `V` while continuing to hold the thumb.
- Right: do the same with the `&mo 2` (SYM) thumb and the mirrored `;`, `.`, `K`,
  and `M` positions.

Start with no keys held. The first thumb tap must take at most 200 ms; press the
thumb again within 300 ms and keep holding it. This arms a 1.5-second capture
window in which the four character keys must all be pressed within 120 ms. Their
presses and releases are swallowed, so nothing is typed. Any other key, early
release or timeout cancels the attempt. Once recognized, release everything to
enter System OFF.

The listener only observes the layer thumb and never delays it: an ordinary tap
or hold still activates and releases NAV or SYM exactly as before. Only the
deliberate tap-release-hold sequence starts capturing the shutdown keys. It
powers off only the half on which it was physically entered, so repeat it on the
other half to power off both.

To wake a half, hold its four character keys, then press and hold its layer thumb
and keep all five down until the half starts. While the MCU is off, only the
final thumb switch is electrically capable of waking it; after reset, firmware
scans only the key matrix for up to one second. It returns to System OFF after
the entire matrix is released unless exactly the other four keys are also held.
Normal Bluetooth, split, display, and trackpad startup happens only after this
check passes. A reset button press remains an escape hatch, including for
entering the UF2 bootloader.

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
