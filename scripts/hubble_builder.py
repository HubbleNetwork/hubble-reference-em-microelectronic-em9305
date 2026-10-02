#!/usr/bin/env python3
#
# Copyright (c) 2026 Hubble Network, Inc.
#
# SPDX-License-Identifier: Apache-2.0

"""Build, flash and log the Hubble beacon on an EM9305 DVK.

    python scripts/hubble_builder.py build   # configure and build into build/
    python scripts/hubble_builder.py flash   # flash a board with the latest build
    python scripts/hubble_builder.py log     # print hubble_log() from the DVK

`build` runs CMake on this repository's CMakeLists.txt, which sets up the
EM9305 SDK's toolchain, macros and libraries and builds app/ against them.
Output goes to build/; the image is build/app/app.ihex.

`flash` programs the last image `build` produced, through EM's BLEngine
on the DVK's USB port.

`log` opens BLEngine's terminal on the same port, which shows every
PrintfBleu message the firmware sends.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "build"
TARGET = "app"
IMAGE = BUILD_DIR / TARGET / f"{TARGET}.ihex"

DVK_USB_ID = (0x1FC9, 0x0083)


def fail(message: str):
    sys.exit(f"error: {message}")


def run(command: "list[str]", cwd: Path) -> None:
    print("+ " + " ".join(command), flush=True)
    if subprocess.run(command, cwd=cwd).returncode != 0:
        fail(f"{Path(command[0]).name} failed")


def find_sdk() -> Path:
    sdk = Path(os.environ.get("EM9305_SDK_ROOT") or REPO_ROOT / "external" / "EM9305_SDK")
    if not (sdk / "cmake" / "toolchains" / "arcv2em.cmake").is_file():
        fail(
            f"EM9305 SDK not found at {sdk}. Unpack it into "
            "external/EM9305_SDK or set EM9305_SDK_ROOT."
        )
    return sdk.resolve()


def find_blengine(sdk: Path) -> str:
    installer = sdk / "tools" / "blengine" / "installer"
    if os.name == "nt":
        name, bundled = "blengine", installer / "windows" / "blengine.dist" / "blengine.exe"
    else:
        name, bundled = "blengine.bin", installer / "linux" / "blengine.dist" / "blengine.bin"

    for candidate in (shutil.which(name), bundled):
        if candidate and Path(candidate).is_file():
            return str(candidate)

    fail(f"BLEngine not found on PATH or under {installer}")


def find_port(explicit: "str | None") -> str:
    port = explicit or os.environ.get("EM9305_PORT")
    if port:
        return port

    try:
        from serial.tools import list_ports
    except ImportError:
        fail("pass --port, or `pip install pyserial` to detect the DVK")

    ports = list(list_ports.comports())
    matches = [p.device for p in ports if (p.vid, p.pid) == DVK_USB_ID]
    if len(matches) == 1:
        return matches[0]

    listing = "\n".join(f"  {p.device}  {p.description}" for p in ports)
    problem = "more than one DVK found" if matches else "no DVK found"
    fail(f"{problem}; pass --port. Ports:\n{listing or '  (none)'}")


def cmd_build(args) -> None:
    run(
        [
            "cmake",
            "-S",
            str(REPO_ROOT),
            "-B",
            str(BUILD_DIR),
            "-G",
            "Ninja",
            "-Wno-dev",
        ],
        cwd=REPO_ROOT,
    )
    run(["cmake", "--build", str(BUILD_DIR), "--target", TARGET, "-j"], cwd=REPO_ROOT)

    print(f"\nbuilt {IMAGE}")


def blengine(sdk: Path, port: "str | None", *command: str) -> None:
    config = sdk / "tools" / "blengine" / "config.cfg"
    argv = [find_blengine(sdk), "--config", str(config), "--port", find_port(port)]

    # BLEngine resolves ./nvm_infos against its cwd, and fails with "category
    # configurations not found in config" from anywhere else.
    run(argv + list(command), cwd=config.parent)


def cmd_flash(args) -> None:
    sdk = find_sdk()
    if not IMAGE.is_file():
        fail(f"{IMAGE} does not exist; run `hubble_builder.py build`")
    blengine(sdk, args.port, "run", "emsystem_prog", "--progress", str(IMAGE))


def cmd_log(args) -> None:
    # hubble_log() goes out as PrintfBleu messages over the DVK's SPI link;
    # BLEngine's terminal prints them. Ctrl-C to stop.
    try:
        blengine(find_sdk(), args.port, "terminal")
    except KeyboardInterrupt:
        pass


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = parser.add_subparsers(dest="command", required=True)

    port = argparse.ArgumentParser(add_help=False)
    port.add_argument("--port", help="DVK serial port (default: autodetect)")

    sub.add_parser("build", help="configure and build into build/").set_defaults(
        func=cmd_build
    )

    sub.add_parser(
        "flash", parents=[port], help="program the DVK with the last build"
    ).set_defaults(func=cmd_flash)

    sub.add_parser(
        "log", parents=[port], help="print hubble_log() from the DVK"
    ).set_defaults(func=cmd_log)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
