#include "netignite/Hex.h"

namespace netignite {

namespace {

int nibble(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

}  // namespace

std::string toHex(const uint8_t* data, size_t length) {
	static constexpr char DIGITS[] = "0123456789abcdef";
	std::string out;
	out.reserve(length * 2);
	for (size_t i = 0; i < length; i++) {
		out += DIGITS[data[i] >> 4];
		out += DIGITS[data[i] & 0x0f];
	}
	return out;
}

std::string toBase64(const uint8_t* data, size_t length, bool pad) {
	static constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((length + 2) / 3 * 4);
	for (size_t i = 0; i < length; i += 3) {
		const uint32_t chunk = (static_cast<uint32_t>(data[i]) << 16) |
		                       (i + 1 < length ? static_cast<uint32_t>(data[i + 1]) << 8 : 0) |
		                       (i + 2 < length ? static_cast<uint32_t>(data[i + 2]) : 0);
		out += ALPHABET[(chunk >> 18) & 0x3f];
		out += ALPHABET[(chunk >> 12) & 0x3f];
		if (i + 1 < length) {
			out += ALPHABET[(chunk >> 6) & 0x3f];
		} else if (pad) {
			out += '=';
		}
		if (i + 2 < length) {
			out += ALPHABET[chunk & 0x3f];
		} else if (pad) {
			out += '=';
		}
	}
	return out;
}

bool fromHex(std::string_view text, std::vector<uint8_t>& out) {
	out.clear();
	if (text.size() % 2 != 0) return false;
	out.reserve(text.size() / 2);
	for (size_t i = 0; i < text.size(); i += 2) {
		const int high = nibble(text[i]);
		const int low = nibble(text[i + 1]);
		if (high < 0 || low < 0) {
			out.clear();
			return false;
		}
		out.push_back(static_cast<uint8_t>((high << 4) | low));
	}
	return true;
}

}  // namespace netignite
