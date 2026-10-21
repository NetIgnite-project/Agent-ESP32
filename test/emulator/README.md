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
boot. Two QEMU quirks the tests work around (real chips have neither):

- QEMU keeps peripheral state across a software restart. An interrupt left over from before (the
  bignum accelerator after an update's ECDSA check, or serial input that is still arriving) crashes
  the next boot in the task watchdog's handler. So the tests check that the device announces the
  restart, then power-cycle QEMU instead; the flash file keeps what was written.
- The emulated UART hands received bytes to the firmware only in blocks of its FIFO threshold, so a
  short line can wait for more input. The tests send empty lines after every command. If a check fails, the test prints the device's serial output, and CI keeps it as the
`emulator-serial-log` artifact.
