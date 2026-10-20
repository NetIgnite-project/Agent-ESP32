"""
Fails if the firmware takes more than 85 % of an OTA app slot, so OTA updates keep room to grow.

    python scripts/check_size.py [.pio/build/esp32dev/firmware.bin]
"""

import csv
import os
import sys

LIMIT = 0.85
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def app_slot_size():
    with open(os.path.join(ROOT, "partitions.csv"), newline="") as handle:
        rows = (row for row in csv.reader(handle) if row and not row[0].strip().startswith("#"))
        for row in rows:
            name, kind, subtype, _offset, size = (cell.strip() for cell in row[:5])
            if kind == "app" and subtype == "ota_0":
                return int(size, 0)
    raise SystemExit("partitions.csv has no ota_0 app partition")


def main():
    firmware = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".pio", "build", "esp32dev", "firmware.bin")
    size = os.path.getsize(firmware)
    slot = app_slot_size()
    used = size / slot
    print(f"Firmware: {size:,} bytes = {used:.1%} of the {slot:,}-byte OTA slot (limit {LIMIT:.0%})")
    if used > LIMIT:
        raise SystemExit("The firmware is too large for OTA updates to keep working.")


if __name__ == "__main__":
    main()
