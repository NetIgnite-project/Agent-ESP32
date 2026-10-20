#include "netignite/ServerUrl.h"

#include <cctype>

namespace netignite {

namespace {

std::string lower(std::string_view text) {
	std::string out(text);
	for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

bool validHostChar(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.' || c == '_';
}

}  // namespace

std::optional<ServerUrl> parseServerUrl(std::string_view text) {
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
	while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
	if (text.empty() || text.size() > 200) return std::nullopt;

	ServerUrl url;

	const size_t schemeEnd = text.find("://");
	if (schemeEnd != std::string_view::npos) {
		const std::string scheme = lower(text.substr(0, schemeEnd));
		if (scheme == "https") {
			url.secure = true;
		} else if (scheme == "http") {
			url.secure = false;
		} else {
			return std::nullopt;
		}
		text.remove_prefix(schemeEnd + 3);
	}
	url.port = url.secure ? 443 : 80;

	if (text.find_first_of("?#@ ") != std::string_view::npos) return std::nullopt;

	const size_t pathStart = text.find('/');
	std::string_view authority = text.substr(0, pathStart);
	std::string_view path = pathStart == std::string_view::npos ? std::string_view{} : text.substr(pathStart);

	std::string_view portText;
	bool hasPort = false;
	if (!authority.empty() && authority.front() == '[') {
		const size_t close = authority.find(']');
		if (close == std::string_view::npos || close == 1) return std::nullopt;
		url.host = lower(authority.substr(1, close - 1));
		for (char c : url.host) {
			if (!std::isxdigit(static_cast<unsigned char>(c)) && c != ':' && c != '.') return std::nullopt;
		}
		std::string_view rest = authority.substr(close + 1);
		if (!rest.empty()) {
			if (rest.front() != ':') return std::nullopt;
			hasPort = true;
			portText = rest.substr(1);
		}
	} else {
		const size_t colon = authority.find(':');
		url.host = lower(authority.substr(0, colon));
		if (colon != std::string_view::npos) {
			hasPort = true;
			portText = authority.substr(colon + 1);
		}
		if (url.host.empty()) return std::nullopt;
		for (char c : url.host) {
			if (!validHostChar(c)) return std::nullopt;
		}
	}
	if (url.host.empty()) return std::nullopt;

	if (hasPort) {
		if (portText.empty() || portText.size() > 5) return std::nullopt;
		uint32_t port = 0;
		for (char c : portText) {
			if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
			port = port * 10 + static_cast<uint32_t>(c - '0');
		}
		if (port == 0 || port > 65535) return std::nullopt;
		url.port = static_cast<uint16_t>(port);
	}

	while (!path.empty() && path.back() == '/') path.remove_suffix(1);
	url.basePath = std::string(path);

	return url;
}

std::string ServerUrl::origin() const {
	std::string out = secure ? "https://" : "http://";
	const bool ipv6 = host.find(':') != std::string::npos;
	out += ipv6 ? "[" + host + "]" : host;
	if (port != (secure ? 443 : 80)) out += ":" + std::to_string(port);
	return out;
}

std::string ServerUrl::url(std::string_view p) const {
	return origin() + path(p);
}

std::string ServerUrl::path(std::string_view p) const {
	return basePath + std::string(p);
}

}  // namespace netignite
