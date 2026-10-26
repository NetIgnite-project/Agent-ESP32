# Changelog

## Unreleased

SSH needs the NetIgnite server release after 2.0.0. With server 2.0.0, everything else works as
before, but without SSH.

- **SSH with the agent's own keys.** The firmware no longer gets passwords or private keys from the
  server. On first boot it creates two Ed25519 key pairs, one for shutdown and one for reboot, and
  keeps them in NVS; HELLO reports only the public keys (`ssh_keys`), and the dashboard shows them
  as `authorized_keys` lines with forced commands. Requests are renamed: `SSH_EXEC` is now
  `SSH_POWER`, the capability `ssh` is now `ssh.power`. `SSH_CHECK` logs in with both keys and
  reports which ones the device accepts.
- SSH only over an `https://` server address, and at most 10 SSH logins a minute.
- `status` on the serial console shows the fingerprints of the SSH keys. A factory reset or
  installing again with the web installer creates new keys.
- Text from other machines reaches the server as valid UTF-8: a device's SSH error output (one
  line), the Wi-Fi name in HELLO and log lines. Before, a Windows error message in its code page
  or a Wi-Fi name in Latin-1 could get the connection to the server dropped.
- `SSH_CHECK` reports a device that answers but whose SSH handshake fails (no common algorithm, a
  timeout) as online, with the error, like the Linux agent; a host name that doesn't resolve says
  so, and a link-local address with a zone (`fe80::1%eth0`) works. When both logins see different
  host keys, it reports the first one with `host_key_mismatch`.
- A failed `SSH_POWER` also reports Windows' error text, which it prints to stdout.
- Every SSH login ends within the server's wait: 10 s per login of a check, 12 s for `SSH_POWER`.
- The SSH keys are only used once they are stored in flash, so they can't change with the next
  boot. A stored key that can't be read is replaced on its own, with a warning; the other key
  stays.
- Without enough free memory, an SSH request says so (instead of "Another SSH login is running")
  and doesn't count toward the 10 logins a minute.

## 2.0.0 (2026-10-21)

The first stable release of the ESP32 firmware v2, for NetIgnite server 2.0.0. No changes since
2.0.0-beta.2; everything it can do is listed below.

### Upgrading from v1

Boards with the v1 firmware (built with PlatformIO and `config.h`) can't connect to a v2 server.
Install 2.0.0 once over USB with the dashboard's web installer (the agent's **Install** tab, in
Chrome, Edge or Firefox 151+): the partition table changed for updates over the network. Later
versions install over the network.

## 2.0.0-beta.2 (2026-10-21)

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
