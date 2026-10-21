#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace netignite {

/** A NetIgnite server address, e.g. `https://app.netignite.dev` or `http://192.168.1.10:18232`. */
struct ServerUrl {
	bool secure = true;
	/** Host name or IP address; IPv6 without brackets. */
	std::string host;
	uint16_t port = 443;
	/** Path prefix if the server runs under a sub-path (`/netignite`), without a trailing slash. */
	std::string basePath;

	/** `https://host[:port]` (port only if not the scheme's default). */
	std::string origin() const;
	/** `origin + basePath + path`, e.g. `url("/api/v1/agents/enroll")`. */
	std::string url(std::string_view path) const;
	/** `basePath + path`, for clients that take host, port and path separately. */
	std::string path(std::string_view path) const;
};

/**
 * Parses `http://` and `https://` URLs with an optional port and path. Query strings, fragments
 * and credentials are rejected; a missing scheme means `https://`.
 */
std::optional<ServerUrl> parseServerUrl(std::string_view text);

}  // namespace netignite
