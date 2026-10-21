#pragma once

#include <string_view>

namespace netignite {

/** A network host name label: letters, digits and `-`, 1–63 characters, no `-` at either end. */
bool isValidHostname(std::string_view name);

/** A dotted-quad IPv4 address (`192.168.1.5`); no leading zeros, no shorthand forms. */
bool isValidIpv4(std::string_view text);

}  // namespace netignite
