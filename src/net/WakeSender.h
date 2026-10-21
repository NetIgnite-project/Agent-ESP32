#pragma once

#include <Arduino.h>

#include "netignite/Mac.h"

namespace wake {

/**
 * Sends Wake-on-LAN magic packets to the subnet's broadcast address and to 255.255.255.255,
 * `config::WOL_ROUNDS` times. Returns false (with `error`) if no packet could be sent.
 */
bool send(const netignite::MacAddress& mac, uint16_t port, String& error);

}  // namespace wake
