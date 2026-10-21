#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace netignite {

using MacAddress = std::array<uint8_t, 6>;
using IPv6Bytes = std::array<uint8_t, 16>;

/**
 * Parses a MAC address: six hex byte pairs separated by `:` or `-` (`AA:BB:CC:DD:EE:FF`,
 * `aa-bb-cc-dd-ee-ff`), or twelve hex digits without separators (`AABBCCDDEEFF`).
 */
bool parseMac(std::string_view text, MacAddress& out);

/** `AA:BB:CC:DD:EE:FF` */
std::string formatMac(const MacAddress& mac);

/** The IPv6 link-local address with the modified EUI-64 interface id derived from `mac`. */
IPv6Bytes linkLocalFromMac(const MacAddress& mac);

/** Compact IPv6 text form (`fe80::1a2b:3cff:fe4d:5e6f`), for logs. */
std::string formatIPv6(const IPv6Bytes& address);

}  // namespace netignite
