"""
Collects the flash images of a build into a release directory and writes release.json, which the
NetIgnite server reads to offer the firmware in the web installer and for OTA updates.

    python scripts/collect_release.py <version> <output-dir> [--signature firmware.sig]

The files are the standard Arduino-ESP32 layout: bootloader at 0x1000, partition table at 0x8000,
OTA data (boot_app0) at 0xe000 and the app at 0x10000. The web installer adds an NVS image at
0x9000 with the server URL, a single-use enrollment token and the agent's name: the keys listed
in release.json's nvs.keys (the server writes no others).
"""

import argparse
import glob
import hashlib
import json
import os
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, ".pio", "build", "esp32dev")

PARTS = [
    ("bootloader.bin", 0x1000),
    ("partitions.bin", 0x8000),
    ("boot_app0.bin", 0xE000),
    ("firmware.bin", 0x10000),
]


def find_boot_app0():
    core = os.environ.get("PLATFORMIO_CORE_DIR", os.path.join(os.path.expanduser("~"), ".platformio"))
    matches = glob.glob(os.path.join(core, "packages", "framework-arduinoespressif32*", "tools", "partitions", "boot_app0.bin"))
    if not matches:
        raise SystemExit("boot_app0.bin not found in the PlatformIO packages")
    return matches[0]


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("version")
    parser.add_argument("output")
    parser.add_argument("--signature", help="ECDSA P-256 signature (DER) of firmware.bin")
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)
    sources = {
        "bootloader.bin": os.path.join(BUILD, "bootloader.bin"),
        "partitions.bin": os.path.join(BUILD, "partitions.bin"),
        "boot_app0.bin": find_boot_app0(),
        "firmware.bin": os.path.join(BUILD, "firmware.bin"),
    }

    parts = []
    for name, offset in PARTS:
        target = os.path.join(args.output, name)
        shutil.copyfile(sources[name], target)
        parts.append({"file": name, "offset": offset, "size": os.path.getsize(target), "sha256": sha256(target)})

    signature = None
    if args.signature:
        shutil.copyfile(args.signature, os.path.join(args.output, "firmware.sig"))
        signature = "firmware.sig"

    release = {
        "format": 1,
        "name": "NetIgnite Agent",
        "version": args.version.lstrip("v"),
        "chip_family": "ESP32",
        "board": "esp32dev",
        "protocol": 2,
        "parts": parts,
        "nvs": {"offset": 0x9000, "size": 0x5000, "namespace": "netignite", "keys": ["server", "enroll", "name"]},
        "app": {"file": "firmware.bin", "signature": signature},
    }
    with open(os.path.join(args.output, "release.json"), "w") as handle:
        json.dump(release, handle, indent=2)
        handle.write("\n")

    print(json.dumps(release, indent=2))


if __name__ == "__main__":
    main()
