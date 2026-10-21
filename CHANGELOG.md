# Changelog

## 2.0.0-beta.2 (unreleased)

Needs NetIgnite server 2.0.0-beta.2 or newer for updates, the console and SSH.

### New

- **Firmware updates over the network** (`OTA_BEGIN`): the agent downloads the image from its
  server, checks size and SHA-256 and, if the firmware has a public key compiled in
  (`keys/firmware-signing.pub.pem`), the ECDSA P-256 signature. The new firmware is on probation
  until it reaches the server; if it crashes or doesn't get there within 5 minutes, the previous
  firmware starts again and the agent reports the failed version.
- **Console** for the dashboard: the last 16 KB of log lines (including ESP-IDF's messages), a live
  log stream, current diagnostics, log level, server address, host name and IP settings (a new
  server address or IP setting is only saved once the agent reaches the server with it), restart
  and factory reset.
- **SSH logins** to devices in the agent's network (libssh): status checks that only authenticate,
  and shutdown/reboot with a trusted host key and fixed commands.
- The web installer can set the agent's name.

### Changed

- Enrollment sends the agent kind; a token of a Linux agent is rejected without being used up.
- Firmware size with libssh: 79% of an OTA slot.

### Tests

- The emulator tests install updates over the serial port and check signature and SHA-256
  rejection, probation and rollback in QEMU; CI runs them.

## 2.0.0-beta.1 (2026-10-20)

Firmware v2 for NetIgnite server v2. Flash it once over USB (or with the server's web installer):
the partition table changed for over-the-air updates.

- Settings in NVS instead of a compiled-in `config.h`: Wi-Fi (Improv or the serial console),
  server URL, enrollment token, static IP, name, log level.
- Enrollment with a single-use token; the agent generates its own secret.
- Agent protocol v2: Wake-on-LAN, status checks by IPv6 link-local (from the MAC), IPv4, IPv6,
  host name and TCP port.
- Serial console commands (`help`, `status`, `setup`, `wifi`, `server`, `enroll`, `ip`, `name`,
  `log`, `reboot`, `factory-reset`).
- Two 1.875 MB app slots; pioarduino (Arduino-ESP32 3.3, ESP-IDF 5.5).
- Releases with the flash images and `release.json` for the server's web installer.
