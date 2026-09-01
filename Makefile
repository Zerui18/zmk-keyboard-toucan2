SHELL := /bin/bash
.DEFAULT_GOAL := help

BOOTLOADER ?= /Volumes/XIAO-BOOT
ZMK_BUILD_IMAGE ?= zmkfirmware/zmk-build-arm:stable
ZMK_BUILD_VOLUME ?= toucan2-zmk-v03
ZMK_TOOL := bash scripts/zmk.sh
PYTHON ?= python3

export BOOTLOADER
export ZMK_BUILD_IMAGE
export ZMK_BUILD_VOLUME
export PYTHON

INSTALL_REQUESTED := $(filter install,$(MAKECMDGOALS))
INSTALL_SIDE_GOALS := $(filter left right,$(MAKECMDGOALS))

.PHONY: help setup doctor left right all install install-left install-right \
	install-both flash-left flash-right flash-both usb-check west-update clean \
	shell sync-upstream format-keymap check-keymap-format install-hooks

help:
	@printf '%s\n' \
		'Toucan2 firmware development' \
		'' \
		'  make left                 Build firmware/toucan_left.uf2' \
		'  make right                Build firmware/toucan_right.uf2' \
		'  make all                  Build both halves' \
		'  make install left         Build and flash the left half' \
		'  make install right        Build and flash the right half' \
		'  make install-left         Alias for make install left' \
		'  make install-right        Alias for make install right' \
		'  make flash-left           Reboot and flash the left half with the existing image' \
		'  make flash-right          Reboot and flash the right half with the existing image' \
		'  make install-both         Build, reboot, and flash both USB-connected halves' \
		'  make flash-both           Reboot and flash both halves with existing images' \
		'  make usb-check            Verify both runtime USB ports are available' \
		'  make format-keymap        Reflow layers to the physical split layout' \
		'  make check-keymap-format  Check formatting without changing files' \
		'  make install-hooks        Enable the repository pre-commit hooks' \
		'' \
		'  make setup                Pull the builder and initialize West' \
		'  make doctor               Check Docker, remotes, and bootloader' \
		'  make west-update          Refresh ZMK and West-managed modules' \
		'  make clean                Remove generated build output' \
		'  make shell                Open a shell in the build container' \
		'  make sync-upstream        Merge upstream/main into local main' \
		'' \
		'Overrides:' \
		'  BOOTLOADER=/Volumes/XIAO-BOOT' \
		'  ZMK_BUILD_IMAGE=zmkfirmware/zmk-build-arm:stable' \
		'  ZMK_BUILD_VOLUME=toucan2-zmk-v03'

setup:
	@$(ZMK_TOOL) setup

doctor:
	@$(ZMK_TOOL) doctor

left:
	@$(ZMK_TOOL) $(if $(INSTALL_REQUESTED),install,build) left

right:
	@$(ZMK_TOOL) $(if $(INSTALL_REQUESTED),install,build) right

all: left right

format-keymap:
	@$(PYTHON) scripts/format-keymap.py

check-keymap-format:
	@$(PYTHON) scripts/format-keymap.py --check

install-hooks:
	@git config --local core.hooksPath .githooks
	@printf '%s\n' 'Git hooks enabled from .githooks'

# Supports both `make install SIDE=left` and `make install left`.
install:
	@if [[ -n "$(SIDE)" ]]; then \
		case "$(SIDE)" in \
			left|right) $(ZMK_TOOL) install "$(SIDE)" ;; \
			*) printf 'SIDE must be left or right\n' >&2; exit 2 ;; \
		esac; \
	elif [[ "$(words $(INSTALL_SIDE_GOALS))" -eq 1 ]]; then \
		:; \
	else \
		printf 'Usage: make install left|right\n' >&2; \
		exit 2; \
	fi

install-left:
	@$(ZMK_TOOL) install left

install-right:
	@$(ZMK_TOOL) install right

flash-left:
	@$(ZMK_TOOL) flash left

flash-right:
	@$(ZMK_TOOL) flash right

install-both:
	@$(ZMK_TOOL) install-both

flash-both:
	@$(ZMK_TOOL) flash-both

usb-check:
	@$(ZMK_TOOL) usb-check

west-update:
	@$(ZMK_TOOL) update

clean:
	@$(ZMK_TOOL) clean

shell:
	@$(ZMK_TOOL) shell

sync-upstream:
	@bash scripts/sync-upstream.sh
