# Emulator test

Boots the firmware in [Espressif's QEMU](https://github.com/espressif/qemu) and checks it without
hardware:

- `improv-test.ts`: Improv Wi-Fi (with `improv-wifi-serial-sdk`, the client ESP Web Tools uses),
  the console commands, and that settings survive a restart and a factory reset.
- `ota-test.ts`: OTA updates. Emulator builds accept an image over the serial port (`ota-begin`,
  `ota-data`, `ota-end`) and install it with the same code as a download. The test checks that
  unsigned, wrongly signed and corrupted images are rejected, that a signed image boots from the
  other slot on probation, and that it rolls back because it can't reach a server. The emulator
  firmware has the test key in `keys/` compiled in; see `keys/README.md`.

QEMU has no Wi-Fi radio, so this uses the `esp32dev-qemu` build, which is the normal firmware with
Wi-Fi switched off. Networking (enrollment, the server connection, Wake-on-LAN, status checks) needs
a real board.

```bash
pio run -e esp32dev-qemu
cd test/emulator
bun install
bun improv-test.ts /path/to/qemu-system-xtensa ../../.pio/build/esp32dev-qemu/firmware.factory.bin /tmp
bun ota-test.ts /path/to/qemu-system-xtensa ../../.pio/build/esp32dev-qemu/firmware.factory.bin ../../.pio/build/esp32dev-qemu/firmware.bin /tmp
```

The OTA upload over the emulated serial port takes a few minutes.

Download QEMU from the [releases](https://github.com/espressif/qemu/releases)
(`qemu-xtensa-softmmu-*`). The tests start it with the timer-group watchdogs disabled, like
ESP-IDF's own QEMU runner; with them enabled, slow hosts reset or crash the emulated chip during
boot. After the firmware has used the bignum accelerator (the ECDSA signature check of an update), a
software restart crashes in QEMU: the next boot takes a stale interrupt in the task watchdog's
handler. Real chips use the same accelerator for every TLS handshake and restart fine, so
`ota-test.ts` power-cycles QEMU instead whenever the device restarts (the flash file keeps what was
written). If a check fails, the test prints the device's serial output, and CI keeps it as the
`emulator-serial-log` artifact.
