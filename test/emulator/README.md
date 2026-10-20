# Emulator test

Boots the firmware in [Espressif's QEMU](https://github.com/espressif/qemu) and checks the serial
side without hardware: Improv Wi-Fi (with `improv-wifi-serial-sdk`, the client ESP Web Tools uses),
the console commands, and that settings survive a restart and a factory reset.

QEMU has no Wi-Fi radio, so this uses the `esp32dev-qemu` build, which is the normal firmware with
Wi-Fi switched off. Networking (enrollment, the server connection, Wake-on-LAN, status checks) needs
a real board.

```bash
pio run -e esp32dev-qemu
cd test/emulator
bun install
bun improv-test.ts /path/to/qemu-system-xtensa ../../.pio/build/esp32dev-qemu/firmware.factory.bin /tmp
```

Download QEMU from the [releases](https://github.com/espressif/qemu/releases)
(`qemu-xtensa-softmmu-*`). The test starts it with the timer-group watchdogs disabled, like
ESP-IDF's own QEMU runner; with them enabled, slow hosts reset or crash the emulated chip during
boot. If a check fails, the test prints the device's serial output, and CI keeps it as the
`emulator-serial-log` artifact.
