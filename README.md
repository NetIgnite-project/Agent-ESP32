# NetIgnite ESP32 Agent

Firmware that turns an ESP32 into a [NetIgnite](https://netignite.dev) agent: it joins your Wi-Fi,
keeps a connection to your NetIgnite server, sends Wake-on-LAN packets, checks whether devices in
its network are online and can shut them down over SSH. It updates itself over the network.

> **v2 (in development).** Works with NetIgnite server v2. Configuration no longer lives in
> `config.h`: the agent is set up from the browser with the server's web installer, or over the
> serial port.

## Hardware

- A classic ESP32 board (PlatformIO `esp32dev`, 4 MB flash).
- 2.4 GHz Wi-Fi (WPA/WPA2/WPA3-Personal).
- In the same network (subnet) as the devices it wakes: Wake-on-LAN broadcasts don't cross routers.

## Set up an agent

The easiest way is the server's **web installer**: in the NetIgnite dashboard, **Agents → Add
agent → ESP32**, then the agent's **Install** tab. It flashes the firmware from the browser
(Chrome, Edge or Firefox 151+, over HTTPS) together with an NVS image that holds the server URL,
the agent's name and a single-use enrollment token, and then sends your Wi-Fi settings to the
ESP32 with [Improv Wi-Fi](https://www.improv-wifi.com/serial/). The Wi-Fi password never reaches
the server. The installer also changes the Wi-Fi of an installed agent.

Without the installer, over the serial port:

1. In the NetIgnite dashboard: **Agents → Add agent → ESP32**.
2. Flash the firmware (see [Build and flash](#build-and-flash)) and open the serial console
   (115200 baud, e.g. `pio device monitor`).
3. Connect to Wi-Fi:
   ```text
   wifi "My Network" "my password"
   ```
4. On the agent's page in the dashboard, click **Create setup command** and paste it:
   ```text
   setup https://app.netignite.dev netignite_enroll_…
   ```

The agent enrolls once with that single-use token. It generates its own secret with the hardware
random number generator; the server only stores a hash of it, and it is never shown or printed.

## Console commands

| Command | |
| --- | --- |
| `status` | Firmware, network, server and connection state |
| `setup <server-url> <enrollment-token>` | Connect to a NetIgnite server |
| `wifi <ssid> [password]` | Connect to a network; kept only if the connection works (quote names with spaces) |
| `server <url>` | Change the server address |
| `enroll <enrollment-token>` | Set the agent up again with a new token |
| `ip dhcp` / `ip static <ip> <gateway> <netmask> [dns1] [dns2]` | IPv4 configuration (DHCP is the default) |
| `name <device-name>` | Host name |
| `log <error\|warn\|info\|debug>` | Log level |
| `reboot` | Restart |
| `factory-reset confirm` | Erase Wi-Fi, server and credentials |

## What the agent does

It connects to `<server>/ws/agent` with a WebSocket (`wss://` for `https://` servers) and speaks
NetIgnite protocol v2 (JSON frames; see `lib/netignite_core/src/netignite/Protocol.h`):

- `WAKE`: sends the magic packet 3 times to the subnet broadcast address and `255.255.255.255`.
- `STATUS`: pings the device (IPv4, IPv6, or the IPv6 link-local address derived from its MAC
  address) and/or tries a TCP port. A refused TCP connection also counts as online. Host names
  resolve to IPv4 and IPv6 addresses.
- `PING`: keep-alive. Without any message from the server for 100 s, the agent reconnects.
- `OTA_BEGIN`: downloads a firmware image from its server (with its own credential), checks its
  size and SHA-256 and, if the firmware has a public key compiled in, its signature, writes it to
  the other app slot and restarts. The new firmware is on probation until it reaches the server;
  if it crashes or doesn't get there within 5 minutes, the bootloader starts the previous firmware
  again, and the agent reports the failed version in its HELLO.
- `GET_LOGS`, `LOG_SUBSCRIBE`: the last 16 KB of log lines (also ESP-IDF's own messages), and new
  lines as `LOG` events, for the dashboard's console.
- `CONFIG_SET`: server address, host name, log level and IP settings. A new server address or IP
  setting is tried first and only saved once the agent reaches the server with it; otherwise the
  agent restarts with the previous settings after 2 minutes.
- `SSH_CHECK`, `SSH_POWER`: logs in to a device in its network over SSH (libssh) with the agent's
  own keys: two Ed25519 key pairs, one for shutdown and one for reboot, created on first boot and
  kept in NVS. HELLO reports only the public keys (`ssh_keys`, with the capability `ssh.power`);
  the server never has a credential. A check logs in with each key and never opens a session;
  `SSH_POWER` needs the trusted host key and requests only `[sudo -n ]shutdown -h|-r now` or
  `shutdown /s|/r /t 0` (the device's authorized_keys line forces the same command). Only over an
  `https://` server address, and at most 10 SSH logins a minute. `status` on the serial console
  shows the keys' fingerprints; a factory reset or a new install creates new keys.
- `GET_INFO`, `REBOOT`, `FACTORY_RESET`.

HTTPS servers need a certificate from a public CA. For a private CA, put its certificate (`.pem`
or `.der`) into `data/cert/custom/` before building.

## Build and flash

Requires [PlatformIO](https://platformio.org) Core **6.2 or newer** (`pio upgrade`). The build
uses [pioarduino](https://github.com/pioarduino/platform-espressif32) (Arduino-ESP32 3.3,
ESP-IDF 5.5), pinned in `platformio.ini`.

```bash
pio run -e esp32dev                # build
pio run -e esp32dev -t upload      # flash over USB
pio device monitor                 # serial console
```

The partition table (`partitions.csv`) has two 1.875 MB app slots for over-the-air updates. If the
board ran v1 firmware, flash v2 once over USB.

## Tests

```bash
pio test -e native                 # parsers and encoders, on your computer (needs gcc)
python scripts/check_size.py       # the firmware must stay below 85 % of an OTA slot
```

`test/emulator/` boots the firmware in Espressif's QEMU and tests Improv and the console with the
same client ESP Web Tools uses, and OTA updates (signature checks, probation, rollback) with images
sent over the serial port. QEMU has no Wi-Fi, so networking needs a real board.

## Releases

Pushing a tag `v<version>` runs `.github/workflows/firmware.yml`: unit tests, build, size check and
emulator test, then a GitHub release with `bootloader.bin`, `partitions.bin`, `boot_app0.bin`,
`firmware.bin`, a merged `firmware-factory.bin` (flash at `0x0`) and `release.json`, which
describes the parts for the NetIgnite server's web installer.

Releases are signed when the repository secret `FIRMWARE_SIGNING_KEY` is set; create the key pair
with `sh scripts/generate_signing_key.sh` and commit only the public key
(`keys/firmware-signing.pub.pem`). Once that file exists, `scripts/signing_key.py` compiles it into
the firmware, and the agent only installs OTA updates with a valid signature. Set the same public
key as `NETIGNITE_FIRMWARE_PUBLIC_KEY` on the server, so it only offers signed releases.

## License

[AGPL-3.0](LICENSE)
