#include "netignite/Validation.h"

#include <cctype>

namespace netignite {

bool isValidHostname(std::string_view name) {
	if (name.empty() || name.size() > 63 || name.front() == '-' || name.back() == '-') return false;
	for (char c : name) {
		if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') return false;
	}
	return true;
}

bool isValidIpv4(std::string_view text) {
	int parts = 0;
	size_t start = 0;
	while (true) {
		const size_t end = text.find('.', start);
		const std::string_view part =
			end == std::string_view::npos ? text.substr(start) : text.substr(start, end - start);
		if (part.empty() || part.size() > 3 || (part.size() > 1 && part[0] == '0')) return false;

		int value = 0;
		for (char c : part) {
			if (c < '0' || c > '9') return false;
			value = value * 10 + (c - '0');
		}
		if (value > 255) return false;

		parts++;
		if (end == std::string_view::npos) break;
		start = end + 1;
	}
	return parts == 4;
}

}  // namespace netignite
