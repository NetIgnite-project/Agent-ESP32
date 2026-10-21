#include "netignite/MagicPacket.h"

namespace netignite {

std::array<uint8_t, MAGIC_PACKET_SIZE> buildMagicPacket(const MacAddress& mac) {
	std::array<uint8_t, MAGIC_PACKET_SIZE> packet{};
	for (size_t i = 0; i < 6; i++) packet[i] = 0xff;
	for (size_t repeat = 0; repeat < 16; repeat++) {
		for (size_t i = 0; i < 6; i++) packet[6 + repeat * 6 + i] = mac[i];
	}
	return packet;
}

uint32_t directedBroadcast(uint32_t address, uint32_t netmask) {
	return (address & netmask) | ~netmask;
}

}  // namespace netignite
