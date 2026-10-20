#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace netignite {

/** Lower-case hex encoding. */
std::string toHex(const uint8_t* data, size_t length);

}  // namespace netignite
