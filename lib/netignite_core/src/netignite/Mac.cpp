#include "netignite/Mac.h"

#include <cstdio>

namespace netignite {

namespace {

int hexValue(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool parsePair(std::string_view text, size_t index, uint8_t& out) {
	const int high = hexValue(text[index]);
	const int low = hexValue(text[index + 1]);
	if (high < 0 || low < 0) return false;
	out = static_cast<uint8_t>((high << 4) | low);
	return true;
}

}  // namespace

bool parseMac(std::string_view text, MacAddress& out) {
	MacAddress mac{};

	if (text.size() == 12) {
		for (size_t i = 0; i < 6; i++) {
			if (!parsePair(text, i * 2, mac[i])) return false;
		}
		out = mac;
		return true;
	}

	if (text.size() != 17) return false;

	const char separator = text[2];
	if (separator != ':' && separator != '-') return false;

	for (size_t i = 0; i < 6; i++) {
		if (!parsePair(text, i * 3, mac[i])) return false;
		if (i < 5 && text[i * 3 + 2] != separator) return false;
	}

	out = mac;
	return true;
}

std::string formatMac(const MacAddress& mac) {
	char buffer[18];
	std::snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
	              mac[4], mac[5]);
	return buffer;
}

IPv6Bytes linkLocalFromMac(const MacAddress& mac) {
	return IPv6Bytes{
		0xfe, 0x80, 0, 0, 0, 0, 0, 0,
		// Modified EUI-64: flip the universal/local bit, insert FF:FE in the middle.
		static_cast<uint8_t>(mac[0] ^ 0x02), mac[1], mac[2], 0xff, 0xfe, mac[3], mac[4], mac[5],
	};
}

std::string formatIPv6(const IPv6Bytes& address) {
	uint16_t groups[8];
	for (int i = 0; i < 8; i++) {
		groups[i] = static_cast<uint16_t>((address[i * 2] << 8) | address[i * 2 + 1]);
	}

	// Longest run (length >= 2) of zero groups is written as "::".
	int bestStart = -1, bestLength = 0;
	for (int i = 0; i < 8;) {
		if (groups[i] != 0) {
			i++;
			continue;
		}
		int j = i;
		while (j < 8 && groups[j] == 0) j++;
		if (j - i > bestLength && j - i >= 2) {
			bestStart = i;
			bestLength = j - i;
		}
		i = j;
	}

	std::string text;
	char part[6];
	for (int i = 0; i < 8; i++) {
		if (i == bestStart) {
			text += "::";
			i += bestLength - 1;
			continue;
		}
		if (!text.empty() && text.back() != ':') text += ':';
		std::snprintf(part, sizeof(part), "%x", groups[i]);
		text += part;
	}
	return text;
}

}  // namespace netignite
