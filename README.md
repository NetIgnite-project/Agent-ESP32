# NetIgnite ESP32 Agent

Firmware that turns an ESP32 into a [NetIgnite](https://netignite.dev) agent: it joins your Wi-Fi,
keeps a connection to your NetIgnite server, sends Wake-on-LAN packets and checks whether devices
in its network are online.

> **v2 (in development).** Works with NetIgnite server v2. Configuration no longer lives in
> `config.h`: the agent is set up over the serial port (and, soon, from the browser).

## Hardware

- A classic ESP32 board (PlatformIO `esp32dev`, 4 MB flash).
- 2.4 GHz Wi-Fi (WPA/WPA2/WPA3-Personal).
- In the same network (subnet) as the devices it wakes: Wake-on-LAN broadcasts don't cross routers.

## Set up an agent

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

The web installer will do steps 2–4 from the browser: it writes the server URL and a token into
the agent's NVS partition while flashing, and sends the Wi-Fi settings with
[Improv Wi-Fi](https://www.improv-wifi.com/serial/), which the firmware already supports.

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
same client ESP Web Tools uses. QEMU has no Wi-Fi, so networking needs a real board.

## Releases

Pushing a tag `v<version>` runs `.github/workflows/firmware.yml`: unit tests, build, size check and
emulator test, then a GitHub release with `bootloader.bin`, `partitions.bin`, `boot_app0.bin`,
`firmware.bin`, a merged `firmware-factory.bin` (flash at `0x0`) and `release.json`, which
describes the parts for the NetIgnite server's web installer.

Releases are signed when the repository secret `FIRMWARE_SIGNING_KEY` is set; create the key pair
with `sh scripts/generate_signing_key.sh` and commit only the public key.

## License

[AGPL-3.0](LICENSE)
