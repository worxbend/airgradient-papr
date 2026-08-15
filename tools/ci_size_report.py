#!/usr/bin/env python3
"""Print a Markdown firmware-size report for one PlatformIO environment.

Usage: python tools/ci_size_report.py <env-name>

Reads .pio/build/<env>/firmware.bin (build the environment first with
`pio run -e <env>`) and reports its size as a percentage of the app
partition. CI appends the output to the GitHub step summary and posts it
as a sticky pull-request comment, so reviewers see flash growth at a
glance without downloading anything.

Why not `pio run --target size`? PlatformIO computes its "Flash" percentage
against `board_upload.maximum_size`, which this project pins to the full
16 MB chip so the bootloader accepts the 16 MB partition table. The real
flashable limit is the `app0` partition (6.25 MB in default_16MB.csv) —
this script reports against that, which is the number that actually
matters when deciding whether the firmware still fits.
"""
import sys
from pathlib import Path

# app0 partition size in default_16MB.csv (the real flashable limit,
# NOT the full 16 MB chip size).
APP_PARTITION_BYTES = 0x640000  # 6,553,600


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: ci_size_report.py <pio-env>", file=sys.stderr)
        return 2
    env = sys.argv[1]
    bin_path = Path(".pio/build") / env / "firmware.bin"
    if not bin_path.exists():
        print(f"error: {bin_path} not found — run `pio run -e {env}` first",
              file=sys.stderr)
        return 1
    size = bin_path.stat().st_size
    pct = 100.0 * size / APP_PARTITION_BYTES
    print(f"### 📏 Firmware size — `{env}`")
    print()
    print("| Metric | Value |")
    print("| --- | --- |")
    print(f"| App image (`firmware.bin`) | {size:,} bytes |")
    print(f"| App partition (`app0`, 16MB table) | {APP_PARTITION_BYTES:,} bytes |")
    print(f"| Flash usage | **{pct:.1f}%** |")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
