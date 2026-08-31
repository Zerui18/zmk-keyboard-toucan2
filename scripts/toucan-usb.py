#!/usr/bin/env python3
"""Find Toucan halves and perform the guarded USB bootloader handshake."""

from __future__ import annotations

import argparse
import fcntl
import os
import platform
import re
import struct
import subprocess
import sys
import termios
import time


PRODUCTS = {
    "left": "Toucan Left",
    "right": "Toucan Right",
}
ZMK_USB_VID = 0x1D50
ZMK_USB_PID = 0x615E


class UsbToolError(RuntimeError):
    pass


def ioreg_subtree(product: str) -> str:
    if platform.system() != "Darwin":
        raise UsbToolError("automatic USB discovery currently requires macOS")

    try:
        result = subprocess.run(
            [
                "ioreg",
                "-p",
                "IOService",
                "-n",
                product,
                "-r",
                "-l",
                "-w",
                "0",
                "-d",
                "4",
            ],
            check=False,
            capture_output=True,
            text=True,
            timeout=2,
        )
    except FileNotFoundError as exc:
        raise UsbToolError("macOS ioreg is unavailable") from exc
    except subprocess.TimeoutExpired as exc:
        raise UsbToolError(f"ioreg timed out while looking for {product}") from exc

    if result.returncode != 0:
        raise UsbToolError(f"ioreg failed while looking for {product}: {result.stderr.strip()}")

    return result.stdout


def matching_ports(side: str) -> list[str]:
    product = PRODUCTS[side]
    tree = ioreg_subtree(product)
    if not tree:
        return []

    vendor_pattern = rf'"idVendor"\s*=\s*{ZMK_USB_VID}'
    product_pattern = rf'"idProduct"\s*=\s*{ZMK_USB_PID}'
    if not re.search(vendor_pattern, tree) or not re.search(product_pattern, tree):
        return []

    return sorted(set(re.findall(r'"IOCalloutDevice"\s*=\s*"([^"]+)"', tree)))


def find_port(side: str) -> str:
    ports = matching_ports(side)
    product = PRODUCTS[side]
    if not ports:
        raise UsbToolError(
            f"{product} runtime USB port not found; connect that half directly over USB"
        )
    if len(ports) != 1:
        raise UsbToolError(f"expected one {product} runtime USB port, found: {', '.join(ports)}")
    return ports[0]


def print_check() -> None:
    found = {side: find_port(side) for side in PRODUCTS}
    for side, port in found.items():
        print(f"{side}: {port}")


def wait_for_side(side: str, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ports = matching_ports(side)
        if len(ports) == 1:
            print(f"{side}: {ports[0]} (runtime ready)")
            return
        time.sleep(0.1)

    raise UsbToolError(f"{PRODUCTS[side]} did not return within {timeout:g} seconds")


def wait_for_side_to_leave(side: str, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not matching_ports(side):
            return
        time.sleep(0.05)

    raise UsbToolError(
        f"{PRODUCTS[side]} ignored the guarded bootloader handshake; "
        "flash the new firmware manually once"
    )


def set_baud(fd: int, speed: int) -> None:
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = (attrs[2] & ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)) | termios.CS8
    attrs[2] |= termios.CLOCAL | termios.CREAD
    attrs[3] = 0
    attrs[4] = speed
    attrs[5] = speed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)


def set_dtr(fd: int, asserted: bool) -> None:
    operation = termios.TIOCMBIS if asserted else termios.TIOCMBIC
    fcntl.ioctl(fd, operation, struct.pack("I", termios.TIOCM_DTR))


def touch(side: str) -> None:
    port = find_port(side)
    print(f"Touching {PRODUCTS[side]} with guarded 1200/2400-baud handshake: {port}")

    flags = os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK
    try:
        fd = os.open(port, flags)
    except OSError as exc:
        raise UsbToolError(
            f"cannot open {port}: {exc.strerror}; close ZMK Studio or any serial monitor"
        ) from exc

    try:
        if hasattr(termios, "TIOCEXCL"):
            fcntl.ioctl(fd, termios.TIOCEXCL)
        set_baud(fd, termios.B1200)
        set_dtr(fd, True)
        time.sleep(0.25)
        set_dtr(fd, False)
        time.sleep(0.10)

        set_baud(fd, termios.B2400)
        set_dtr(fd, True)
        time.sleep(0.25)
        set_dtr(fd, False)
        time.sleep(0.05)
    except OSError as exc:
        # Firmware from before the guarded handshake reboots after the first
        # 1200-baud DTR drop, so the second stage can legitimately lose its fd
        # during the one-time upgrade. Accept that only if the runtime port is
        # actually gone; otherwise this was a real serial-control failure.
        if matching_ports(side):
            raise UsbToolError(
                f"guarded bootloader touch failed on {port}: {exc.strerror}"
            ) from exc
        print(f"{PRODUCTS[side]} runtime port disappeared during the handshake")
    finally:
        try:
            os.close(fd)
        except OSError:
            # The expected reboot can invalidate the descriptor before close.
            pass

    wait_for_side_to_leave(side, 3.0)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("check", help="verify that both runtime USB ports exist")

    touch_parser = subparsers.add_parser(
        "touch", help="put one half into UF2 with the guarded serial handshake"
    )
    touch_parser.add_argument("side", choices=PRODUCTS)

    wait_parser = subparsers.add_parser("wait", help="wait for one runtime USB port to return")
    wait_parser.add_argument("side", choices=PRODUCTS)
    wait_parser.add_argument("--timeout", type=float, default=15.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "check":
            print_check()
        elif args.command == "touch":
            touch(args.side)
        elif args.command == "wait":
            wait_for_side(args.side, args.timeout)
    except UsbToolError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
