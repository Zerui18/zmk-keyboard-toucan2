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
