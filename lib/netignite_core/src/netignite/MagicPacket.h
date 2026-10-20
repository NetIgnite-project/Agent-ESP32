#pragma once

#include <array>
#include <cstdint>

#include "netignite/Mac.h"

namespace netignite {

constexpr size_t MAGIC_PACKET_SIZE = 102;

/** Wake-on-LAN magic packet: 6 × 0xFF followed by the MAC address 16 times. */
std::array<uint8_t, MAGIC_PACKET_SIZE> buildMagicPacket(const MacAddress& mac);

/** The IPv4 subnet broadcast address (host byte order) for an address and netmask. */
uint32_t directedBroadcast(uint32_t address, uint32_t netmask);

}  // namespace netignite
