#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace netignite {

/** Lower-case hex encoding. */
std::string toHex(const uint8_t* data, size_t length);

/** Decodes hex (either case). False for odd lengths or other characters. */
bool fromHex(std::string_view text, std::vector<uint8_t>& out);

/** Standard base64; without `=` padding if `pad` is false (OpenSSH fingerprints). */
std::string toBase64(const uint8_t* data, size_t length, bool pad = true);

}  // namespace netignite
