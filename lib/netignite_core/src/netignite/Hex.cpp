#include "netignite/Hex.h"

namespace netignite {

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

}  // namespace netignite
